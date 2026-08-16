#ifndef GUI_SETTINGS_PANEL_H
#define GUI_SETTINGS_PANEL_H

#include "imgui.h"
#include "imgui_stdlib.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include "../app_state.h"
#include "../widgets.h"

static const char* kModelOptions[] = {
    "models-cunet",
    "models-upconv_7_anime_style_art_rgb",
    "models-upconv_7_photo"
};
static const char* kModelLabels[] = {
    "cunet（高画质）",
    "upconv-7 动漫风格",
    "upconv-7 照片"
};
static const int kNoiseOptions[] = { -1, 0, 1, 2, 3 };
static const char* kNoiseLabels[] = {
    "不降噪（仅放大）",
    "轻度降噪（0）",
    "降噪 1 级",
    "降噪 2 级",
    "强力降噪（3）"
};
static const int kScaleOptions[] = { 1, 2, 4, 8, 16, 32 };
static const char* kScaleLabels = "1 倍（不放大）\0002 倍\0004 倍\0008 倍\00016 倍\00032 倍\000";
static const char* kFormatOptions[] = { "png", "jpg", "webp" };
static const char* kFormatLabels[] = { "PNG（无损）", "JPG（体积小）", "WebP（体积小）" };

static void settings_section(const char* title)
{
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.18f, 0.35f, 0.24f, 1.0f), "%s", title);
    ImGui::Separator();
    ImGui::Spacing();
}

inline void render_settings_panel(AppState& state, void* native_hwnd = nullptr)
{
    const float full_w = ImGui::GetContentRegionAvail().x;
    const float font_h = ImGui::GetFontSize();

    // 说明：所有控件都使用“标签在上、控件占满整行”的布局，
    // 避免标签画在控件右侧时超出面板宽度、被右栏遮挡。

    // === Section: 模型 ===
    settings_section("模型");
    {
        int current = 0;
        for (int i = 0; i < 3; i++)
        {
            if (state.model_dir == kModelOptions[i]) { current = i; break; }
        }
        ImGui::Text("模型类型");
        ImGui::SetNextItemWidth(full_w);
        if (ImGui::Combo("##model", &current, kModelLabels, 3))
            state.model_dir = kModelOptions[current];
        ImGui::SetItemTooltip("模型决定画质与速度：\ncunet 画质最高；upconv-7 更快。");
    }

    // === Section: 处理 ===
    settings_section("处理");

    // 降噪等级
    {
        int current = 0;
        for (int i = 0; i < 5; i++)
        {
            if (state.noise == kNoiseOptions[i]) { current = i; break; }
        }
        ImGui::Text("降噪等级");
        ImGui::SetNextItemWidth(full_w);
        if (ImGui::Combo("##noise", &current, kNoiseLabels, 5))
            state.noise = kNoiseOptions[current];
        ImGui::SetItemTooltip("原图越模糊、噪点越多，等级应越高；\n干净的图片请选“不降噪”。");
    }

    // 放大倍数
    {
        int current = 1;
        for (int i = 0; i < 6; i++)
        {
            if (state.scale == kScaleOptions[i]) { current = i; break; }
        }
        ImGui::Text("放大倍数");
        ImGui::SetNextItemWidth(full_w);
        if (ImGui::Combo("##scale", &current, kScaleLabels, 6))
            state.scale = kScaleOptions[current];
        ImGui::SetItemTooltip("2 倍及以上会逐级翻倍；倍数越大，耗时越长。");
    }

    // 分块大小
    ImGui::Text("分块大小");
    ImGui::SetNextItemWidth(full_w * 0.45f);
    ImGui::InputInt("##tilesize", &state.tilesize);
    if (state.tilesize < 0) state.tilesize = 0;
    ImGui::SetItemTooltip("0 为自动选择；显存或内存不足时可手动调小（如 100）。");
    ImGui::SameLine(0, 8);
    ImGui::TextDisabled("(0 = 自动)");

    // 线程数（读取 : 处理 : 保存）
    ImGui::Text("线程数（读取 : 处理 : 保存）");
    // 与“分块大小”同款输入框，宽度约为其一半
    const float thread_box_w = full_w * 0.225f;
    ImGui::SetNextItemWidth(thread_box_w);
    ImGui::InputInt("##load", &state.jobs_load);
    ImGui::SameLine(0, 4);
    ImGui::Text(":");
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(thread_box_w);
    ImGui::InputInt("##proc", &state.jobs_proc);
    ImGui::SameLine(0, 4);
    ImGui::Text(":");
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(thread_box_w);
    ImGui::InputInt("##save", &state.jobs_save);
    ImGui::SetItemTooltip("一般保持默认即可；CPU 处理时调大“处理”可提速。");
    if (state.jobs_load < 1) state.jobs_load = 1;
    if (state.jobs_proc < 1) state.jobs_proc = 1;
    if (state.jobs_save < 1) state.jobs_save = 1;

    // TTA 模式
    ImGui::Checkbox("TTA 模式（更慢，质量更高）", &state.tta_mode);
    ImGui::SetItemTooltip("开启后对图像做 8 个方向变换取平均，\n效果更好，但耗时约 8 倍。");

    // === Section: 硬件 ===
    settings_section("硬件");

    // GPU 设备
    ImGui::Text("GPU 设备");
    ImGui::SetNextItemWidth(full_w);
    if (ImGui::BeginCombo("##gpu", state.gpu_id == -1 ? "CPU" :
                          (state.gpu_id < (int)state.gpu_names.size() ? state.gpu_names[state.gpu_id].c_str() : "Auto")))
    {
        if (ImGui::Selectable("CPU", state.gpu_id == -1))
            state.gpu_id = -1;
        for (int i = 0; i < (int)state.gpu_names.size(); i++)
        {
            bool selected = (state.gpu_id == i);
            if (ImGui::Selectable(state.gpu_names[i].c_str(), selected))
                state.gpu_id = i;
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("选择用于计算的显卡；\n列表为空或没有显卡时请选 CPU（会慢很多）。");

    // === Section: 输出 ===
    settings_section("输出");

    // 输出格式
    {
        int current = 0;
        for (int i = 0; i < 3; i++)
        {
            if (state.output_format == kFormatOptions[i]) { current = i; break; }
        }
        ImGui::Text("输出格式");
        ImGui::SetNextItemWidth(full_w);
        if (ImGui::Combo("##format", &current, kFormatLabels, 3))
            state.output_format = kFormatOptions[current];
        ImGui::SetItemTooltip("PNG 无损但文件大；JPG/WebP 更小，适合分享。");
    }

    // 输出目录
    {
        // 保持显示缓冲与路径状态同步（路径按 UTF-8 保存，中文不会乱码）
        std::string current_utf8 = path_to_utf8(state.output_dir);
        if (current_utf8 != state.output_dir_utf8)
            state.output_dir_utf8 = current_utf8;

        ImGui::Text("输出目录");
        float btn_w = font_h * 4.5f;
        ImGui::SetNextItemWidth(full_w - btn_w - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::InputText("##outdir", &state.output_dir_utf8))
        {
            state.output_dir = utf8_to_path(state.output_dir_utf8);
            state.output_is_dir = true;
        }
        ImGui::SameLine(0, 6);
        if (ImGui::Button("浏览…", ImVec2(btn_w, 0)))
        {
#if _WIN32
            path_t path;
            if (browse_folder(path, (HWND)native_hwnd))
            {
                state.output_dir = path;
                state.output_dir_utf8 = path_to_utf8(path);
                state.output_is_dir = true;
            }
#endif
        }
    }
}

#endif // GUI_SETTINGS_PANEL_H
