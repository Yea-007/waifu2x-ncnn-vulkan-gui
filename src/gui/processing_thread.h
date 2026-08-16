#ifndef GUI_PROCESSING_THREAD_H
#define GUI_PROCESSING_THREAD_H

#include <thread>
#include <mutex>
#include <queue>
#include <string>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include "app_state.h"
#include "../model_config.h"
#include "image_codec.h"
#include "waifu2x.h"
#include "gpu.h"

// Snapshot of AppState for the worker thread (no atomics/mutexes)
struct ProcessingSnapshot
{
    int noise = 0;
    int scale = 2;
    int tilesize = 0;
    std::string model_dir;
    int gpu_id = 0;
    bool tta_mode = false;
    int jobs_proc = 2;
    std::string output_format;
    std::vector<path_t> input_files;
    std::vector<path_t> output_files;
    bool capture_preview = false;
};

struct ProcessEvent
{
    enum Type { FileStarted, FileCompleted, Error, AllCompleted, PreviewReady };
    Type type;
    int file_index;
    std::string message;
    // 预览数据（由 UI 线程接管并释放）
    unsigned char* preview_input = nullptr;
    unsigned char* preview_output = nullptr;
    int preview_iw = 0, preview_ih = 0, preview_ic = 0;
    int preview_ow = 0, preview_oh = 0, preview_oc = 0;
};

// 为预览生成一份（必要时缩小）的图像副本，返回 malloc 内存
static inline unsigned char* make_preview_copy(const unsigned char* src, int sw, int sh, int c,
                                               int* out_w, int* out_h)
{
    const int kMaxSide = 1600;
    int dw = sw;
    int dh = sh;
    if (sw > kMaxSide || sh > kMaxSide)
    {
        if (sw >= sh)
        {
            dw = kMaxSide;
            dh = std::max(1, (sh * kMaxSide + sw / 2) / sw);
        }
        else
        {
            dh = kMaxSide;
            dw = std::max(1, (sw * kMaxSide + sh / 2) / sh);
        }
    }

    unsigned char* dst = (unsigned char*)malloc((size_t)dw * dh * c);
    if (!dst)
        return nullptr;

    if (dw == sw && dh == sh)
    {
        memcpy(dst, src, (size_t)dw * dh * c);
    }
    else
    {
        // 双线性缩小
        for (int y = 0; y < dh; y++)
        {
            float sy = ((float)y + 0.5f) * sh / dh - 0.5f;
            if (sy < 0.0f) sy = 0.0f;
            int y0 = (int)sy;
            int y1 = std::min(y0 + 1, sh - 1);
            if (y0 > sh - 1) y0 = sh - 1;
            float wy = sy - y0;
            const unsigned char* row0 = src + (size_t)y0 * sw * c;
            const unsigned char* row1 = src + (size_t)y1 * sw * c;
            unsigned char* out_row = dst + (size_t)y * dw * c;

            for (int x = 0; x < dw; x++)
            {
                float sx = ((float)x + 0.5f) * sw / dw - 0.5f;
                if (sx < 0.0f) sx = 0.0f;
                int x0 = (int)sx;
                int x1 = std::min(x0 + 1, sw - 1);
                if (x0 > sw - 1) x0 = sw - 1;
                float wx = sx - x0;

                for (int k = 0; k < c; k++)
                {
                    float v =
                        row0[x0 * c + k] * (1.0f - wx) * (1.0f - wy) +
                        row0[x1 * c + k] * wx * (1.0f - wy) +
                        row1[x0 * c + k] * (1.0f - wx) * wy +
                        row1[x1 * c + k] * wx * wy;
                    out_row[x * c + k] = (unsigned char)(v + 0.5f);
                }
            }
        }
    }

    *out_w = dw;
    *out_h = dh;
    return dst;
}

class ProcessingThread
{
public:
    ProcessingThread() {}
    ~ProcessingThread() { stop(); }

    void start(const AppState& state)
    {
        if (running_)
            return;

        snap_.noise = state.noise;
        snap_.scale = state.scale;
        snap_.tilesize = state.tilesize;
        snap_.model_dir = state.model_dir;
        snap_.gpu_id = state.gpu_id;
        snap_.tta_mode = state.tta_mode;
        snap_.jobs_proc = state.jobs_proc;
        snap_.output_format = state.output_format;
        snap_.input_files = state.input_files;
        snap_.output_files = state.output_files;
        snap_.capture_preview = (state.input_files.size() == 1);

        running_ = true;
        done_ = false;
        has_error_ = false;
        cancel_requested_ = false;
        progress_current_ = 0;
        progress_total_ = (int)snap_.input_files.size();

        worker_ = std::thread(&ProcessingThread::run, this);
    }

    void cancel()
    {
        cancel_requested_ = true;
    }

    void stop()
    {
        cancel_requested_ = true;
        if (worker_.joinable())
            worker_.join();
    }

    bool is_running() const { return running_; }
    bool is_finished() const { return done_; }
    bool had_error() const { return has_error_; }
    int progress_current() const { return progress_current_; }
    int progress_total() const { return progress_total_; }
    std::string last_error() const { return last_error_; }

    bool pop_event(ProcessEvent& out)
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        if (events_.empty())
            return false;
        out = events_.front();
        events_.pop();
        return true;
    }

private:
    void run()
    {
#if _WIN32
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
#endif

        ncnn::create_gpu_instance();

        path_t model_dir(snap_.model_dir.begin(), snap_.model_dir.end());
        ModelConfig modelcfg;
        if (!resolve_model_config(model_dir, snap_.noise, snap_.scale, modelcfg))
        {
            last_error_ = "无法解析模型配置，请检查模型目录是否正确。";
            has_error_ = true;
            push_event({ProcessEvent::Error, -1, last_error_});
        }
        else
        {
            int gpu_id = snap_.gpu_id;
            if (gpu_id < -1) gpu_id = ncnn::get_default_gpu_index();

            int num_threads = (gpu_id == -1) ? snap_.jobs_proc : 1;

            // Waifu2x 放在单独作用域内：必须在其析构完成后再销毁 GPU 实例，
            // 否则析构会访问已销毁的 GPU 资源导致程序崩溃退出。
            {
                Waifu2x waifu2x(gpu_id, snap_.tta_mode, num_threads);

                if (waifu2x.load(modelcfg.param_path, modelcfg.model_path) != 0)
                {
                    last_error_ = "模型加载失败，请检查模型文件是否完整。";
                    has_error_ = true;
                    push_event({ProcessEvent::Error, -1, last_error_});
                }
                else
                {
                    waifu2x.noise = snap_.noise;
                    waifu2x.scale = (snap_.scale >= 2) ? 2 : snap_.scale;
                    waifu2x.prepadding = modelcfg.prepadding;

                    int tilesize = snap_.tilesize;
                    if (tilesize == 0)
                    {
                        if (gpu_id == -1)
                        {
                            tilesize = 400;
                        }
                        else
                        {
                            uint32_t heap_budget = ncnn::get_gpu_device(gpu_id)->get_heap_budget();
                            if (snap_.model_dir.find("models-cunet") != std::string::npos)
                            {
                                if (heap_budget > 2600) tilesize = 400;
                                else if (heap_budget > 740) tilesize = 200;
                                else if (heap_budget > 250) tilesize = 100;
                                else tilesize = 32;
                            }
                            else
                            {
                                if (heap_budget > 1900) tilesize = 400;
                                else if (heap_budget > 550) tilesize = 200;
                                else if (heap_budget > 190) tilesize = 100;
                                else tilesize = 32;
                            }
                        }
                    }
                    waifu2x.tilesize = tilesize;

                    int output_scale = snap_.scale;
                    int passes = scale_pass_count(output_scale);
                    std::string out_fmt = snap_.output_format;
                    unsigned char* preview_in = nullptr;
                    int piw = 0, pih = 0, pic = 0;
                    bool preview_captured = false;

                    for (int i = 0; i < (int)snap_.input_files.size(); i++)
                    {
                        if (cancel_requested_)
                            break;

                        progress_current_ = i;
                        push_event({ProcessEvent::FileStarted, i, ""});

                        const path_t& inpath = snap_.input_files[i];
                        const path_t& outpath = snap_.output_files[i];

                        int w, h, c;
                        unsigned char* pixeldata = decode_image(inpath, &w, &h, &c);
                        if (!pixeldata)
                        {
                            push_event({ProcessEvent::Error, i, "图片解码失败（文件可能已损坏或不支持该格式）。"});
                            continue;
                        }

                        // 单张图片时抓取输入用于预览
                        bool want_preview = snap_.capture_preview && !preview_captured;
                        if (want_preview)
                        {
                            preview_in = make_preview_copy(pixeldata, w, h, c, &piw, &pih);
                            pic = c;
                            preview_captured = true;
                        }

                        unsigned char* preview_out = nullptr;
                        int pow_ = 0, poh_ = 0, poc_ = 0;

                        if (c == 4 && (out_fmt == "jpg" || out_fmt == "jpeg"))
                        {
                            path_t actual_outpath = outpath + PATHSTR(".png");
                            ncnn::Mat inimage(w, h, (void*)pixeldata, (size_t)c, c);
                            bool saved = process_and_save(inimage, actual_outpath, output_scale, passes, waifu2x, out_fmt,
                                                          want_preview ? &preview_out : nullptr, &pow_, &poh_, &poc_);
                            if (!saved)
                                push_event({ProcessEvent::Error, i, "输出文件保存失败，请检查输出目录的写入权限。"});
                        }
                        else
                        {
                            ncnn::Mat inimage(w, h, (void*)pixeldata, (size_t)c, c);
                            bool saved = process_and_save(inimage, outpath, output_scale, passes, waifu2x, out_fmt,
                                                          want_preview ? &preview_out : nullptr, &pow_, &poh_, &poc_);
                            if (!saved)
                                push_event({ProcessEvent::Error, i, "输出文件保存失败，请检查输出目录的写入权限。"});
                        }

                        if (preview_out && preview_in)
                        {
                            ProcessEvent ev;
                            ev.type = ProcessEvent::PreviewReady;
                            ev.file_index = -1;
                            ev.preview_input = preview_in;
                            ev.preview_output = preview_out;
                            ev.preview_iw = piw; ev.preview_ih = pih; ev.preview_ic = pic;
                            ev.preview_ow = pow_; ev.preview_oh = poh_; ev.preview_oc = poc_;
                            push_event(ev);
                            preview_in = nullptr;
                        }
                        else
                        {
                            if (preview_in) { free(preview_in); preview_in = nullptr; }
                            if (preview_out) free(preview_out);
                        }

                        free(pixeldata);
                        push_event({ProcessEvent::FileCompleted, i, ""});
                    }

                    // 若中途取消，清理尚未交给 UI 的预览数据
                    if (preview_in)
                    {
                        free(preview_in);
                        preview_in = nullptr;
                    }

                    progress_current_ = (int)snap_.input_files.size();
                    push_event({ProcessEvent::AllCompleted, -1, ""});
                }
            }
        }

        // 推理对象已析构完毕，此时再销毁 GPU 实例是安全的
        ncnn::destroy_gpu_instance();
        running_ = false;
        done_ = true;
    }

    bool process_and_save(const ncnn::Mat& inimage, const path_t& outpath, int output_scale, int passes,
                          Waifu2x& waifu2x, const std::string& out_fmt,
                          unsigned char** preview_out = nullptr,
                          int* pw = nullptr, int* ph = nullptr, int* pc = nullptr)
    {
        bool ok = false;
        if (output_scale == 1)
        {
            ncnn::Mat outimage(inimage.w, inimage.h, (size_t)inimage.elemsize, (int)inimage.elemsize);
            waifu2x.process(inimage, outimage);
            capture_preview_data(outimage, preview_out, pw, ph, pc);
            ok = encode_image(outpath, outimage.w, outimage.h, outimage.elempack,
                              (const unsigned char*)outimage.data, out_fmt);
        }
        else
        {
            ncnn::Mat outimage(inimage.w * 2, inimage.h * 2, (size_t)inimage.elemsize, (int)inimage.elemsize);
            waifu2x.process(inimage, outimage);

            for (int pass = 1; pass < passes; pass++)
            {
                ncnn::Mat tmp = outimage;
                outimage = ncnn::Mat(tmp.w * 2, tmp.h * 2, (size_t)inimage.elemsize, (int)inimage.elemsize);
                waifu2x.process(tmp, outimage);
            }

            capture_preview_data(outimage, preview_out, pw, ph, pc);
            ok = encode_image(outpath, outimage.w, outimage.h, outimage.elempack,
                              (const unsigned char*)outimage.data, out_fmt);
        }
        return ok;
    }

    static void capture_preview_data(const ncnn::Mat& outimage,
                                     unsigned char** preview_out,
                                     int* pw, int* ph, int* pc)
    {
        if (!preview_out)
            return;
        *preview_out = nullptr;
        if (!outimage.data)
            return;

        int w = outimage.w;
        int h = outimage.h;
        int c = outimage.elempack;
        // 输出超过 8K 时不再抓预览，避免占用过多内存
        if (w > 8192 || h > 8192)
            return;
        unsigned char* buf = (unsigned char*)malloc((size_t)w * h * c);
        if (!buf)
            return;
        memcpy(buf, outimage.data, (size_t)w * h * c);
        *preview_out = buf;
        if (pw) *pw = w;
        if (ph) *ph = h;
        if (pc) *pc = c;
    }

    void push_event(const ProcessEvent& ev)
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        events_.push(ev);
    }

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> done_{false};
    std::atomic<bool> cancel_requested_{false};
    std::atomic<bool> has_error_{false};
    std::atomic<int> progress_current_{0};
    std::atomic<int> progress_total_{0};
    std::string last_error_;
    ProcessingSnapshot snap_;

    mutable std::mutex event_mutex_;
    std::queue<ProcessEvent> events_;
};

#endif // GUI_PROCESSING_THREAD_H
