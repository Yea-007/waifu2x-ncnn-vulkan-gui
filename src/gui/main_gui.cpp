// waifu2x-ncnn-vulkan GUI
// Dear ImGui + GLFW + OpenGL 3 backend

#if _WIN32
// ncnn 会把目标系统版本定义为 Windows XP（_WIN32_WINNT=0x0501），
// 这会隐藏 IFileOpenDialog 等 Vista+ 的 COM 接口。
// 必须在包含任何 Windows 头之前把目标版本提升到 Windows 10。
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#endif

#include <stdio.h>
#include <string>
#include <vector>
#include <algorithm>

// ncnn
#include "gpu.h"

// GLFW (must come before glfw3native.h)
#include "GLFW/glfw3.h"

#if _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"
#include <objbase.h>
#endif

// Dear ImGui
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// App
#include "app_state.h"
#include "processing_thread.h"
#include "image_codec.h"
#include "widgets.h"
#include "panels/main_menu_bar.h"
#include "panels/settings_panel.h"
#include "panels/file_list_panel.h"
#include "panels/progress_panel.h"
#include "panels/preview_window.h"

static AppState g_state;
static ProcessingThread g_worker;
static bool g_show_preview = false;
static bool g_show_about = false;
static bool g_quit_requested = false;
static GLFWwindow* g_window = nullptr;

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static void setup_dpi_awareness()
{
#if _WIN32
    // 高分屏下若不做 DPI 感知，窗口会被系统拉伸导致文字模糊、
    // 字号看起来偏小。优先按显示器感知（Per-Monitor v2），失败则退回系统级。
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (!user32)
        return;
    typedef BOOL(WINAPI* SetProcessDpiAwarenessContextFn)(HANDLE);
    SetProcessDpiAwarenessContextFn fn =
        (SetProcessDpiAwarenessContextFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (fn && fn((HANDLE)-4))
    {
        FreeLibrary(user32);
        return;
    }
    typedef BOOL(WINAPI* SetProcessDPIAwareFn)();
    SetProcessDPIAwareFn fallback = (SetProcessDPIAwareFn)GetProcAddress(user32, "SetProcessDPIAware");
    if (fallback)
        fallback();
    FreeLibrary(user32);
#endif
}

static void glfw_drop_callback(GLFWwindow* window, int count, const char** paths)
{
    for (int i = 0; i < count; i++)
    {
        g_state.input_files.push_back(path_t(paths[i], paths[i] + strlen(paths[i])));
    }
    regenerate_output_files(g_state);
}

// 把相对模型目录解析为实际存在的绝对路径。
// 程序可能从 build/Release 启动，而模型放在仓库的 models/ 下，
// 直接使用相对路径会导致模型加载失败。
static void resolve_model_dir(AppState& state)
{
    if (state.model_dir.empty())
        return;

#if _WIN32
    path_t rel(state.model_dir.begin(), state.model_dir.end());

    wchar_t exepath[1024];
    GetModuleFileNameW(NULL, exepath, 1024);
    path_t exe_dir(exepath);
    size_t slash = exe_dir.find_last_of(PATHSTR('\\'));
    if (slash != path_t::npos)
        exe_dir = exe_dir.substr(0, slash);

    std::vector<path_t> candidates;
    candidates.push_back(exe_dir + PATHSTR('/') + rel);
    candidates.push_back(exe_dir + PATHSTR("/../models/") + rel);
    candidates.push_back(exe_dir + PATHSTR("/../../models/") + rel);
    candidates.push_back(rel); // 当前工作目录

    for (const path_t& c : candidates)
    {
        if (path_is_directory(c))
        {
            // 规范化路径：去掉 /../ 段，避免设置文件里出现难看的相对回退
            path_t normalized;
            std::vector<path_t> segs;
            size_t pos = 0;
            while (pos < c.size())
            {
                size_t slash = c.find_first_of(PATHSTR("\\/"), pos);
                path_t seg = (slash == path_t::npos) ? c.substr(pos) : c.substr(pos, slash - pos);
                if (seg == PATHSTR(".."))
                {
                    if (!segs.empty() && segs.back() != PATHSTR(".."))
                        segs.pop_back();
                    else
                        segs.push_back(seg);
                }
                else if (!seg.empty() && seg != PATHSTR("."))
                {
                    segs.push_back(seg);
                }
                if (slash == path_t::npos)
                    break;
                pos = slash + 1;
            }
            for (size_t i = 0; i < segs.size(); i++)
            {
                if (i > 0)
                    normalized += PATHSTR('\\');
                normalized += segs[i];
            }
            state.model_dir = path_to_utf8(normalized);
            return;
        }
    }
#endif
}

// --- Settings persistence ---

static void load_settings(AppState& state)
{
    path_t settings_path = state.settings_path;
    if (settings_path.empty())
    {
#if _WIN32
        wchar_t exepath[512];
        GetModuleFileNameW(NULL, exepath, 512);
        wchar_t* slash = wcsrchr(exepath, L'\\');
        if (slash) *(slash + 1) = L'\0';
        settings_path = path_t(exepath) + PATHSTR("waifu2x-gui.ini");
        state.settings_path = settings_path;
#else
        settings_path = PATHSTR("waifu2x-gui.ini");
        state.settings_path = settings_path;
#endif
    }

#if _WIN32
    FILE* fp = _wfopen(settings_path.c_str(), L"rb");
#else
    FILE* fp = fopen(settings_path.c_str(), "rb");
#endif
    if (!fp) return;

    char line[4096];
    while (fgets(line, sizeof(line), fp))
    {
        char key[256] = {};
        char value[2048] = {};
        if (sscanf(line, "%255[^=]=%2047[^\r\n]", key, value) == 2)
        {
            std::string k(key);
            std::string v(value);
            if (k == "noise") state.noise = atoi(value);
            else if (k == "scale") state.scale = atoi(value);
            else if (k == "tilesize") state.tilesize = atoi(value);
            else if (k == "model_dir") state.model_dir = v;
            else if (k == "gpu_id") state.gpu_id = atoi(value);
            else if (k == "tta_mode") state.tta_mode = (atoi(value) != 0);
            else if (k == "jobs_load") state.jobs_load = atoi(value);
            else if (k == "jobs_proc") state.jobs_proc = atoi(value);
            else if (k == "jobs_save") state.jobs_save = atoi(value);
            else if (k == "output_format") state.output_format = v;
            else if (k == "output_dir")
            {
                state.output_dir = utf8_to_path(v);
                state.output_dir_utf8 = v;
                state.output_is_dir = true;
            }
        }
    }
    fclose(fp);
}

static void save_settings(const AppState& state)
{
    if (state.settings_path.empty()) return;

#if _WIN32
    FILE* fp = _wfopen(state.settings_path.c_str(), L"wb");
#else
    FILE* fp = fopen(state.settings_path.c_str(), "wb");
#endif
    if (!fp) return;

    fprintf(fp, "model_dir=%s\n", state.model_dir.c_str());
    fprintf(fp, "noise=%d\n", state.noise);
    fprintf(fp, "scale=%d\n", state.scale);
    fprintf(fp, "tilesize=%d\n", state.tilesize);
    fprintf(fp, "gpu_id=%d\n", state.gpu_id);
    fprintf(fp, "tta_mode=%d\n", state.tta_mode ? 1 : 0);
    fprintf(fp, "jobs_load=%d\n", state.jobs_load);
    fprintf(fp, "jobs_proc=%d\n", state.jobs_proc);
    fprintf(fp, "jobs_save=%d\n", state.jobs_save);
    fprintf(fp, "output_format=%s\n", state.output_format.c_str());
    std::string odir = path_to_utf8(state.output_dir);
    fprintf(fp, "output_dir=%s\n", odir.c_str());

    fclose(fp);
}

// --- Custom bamboo/paper style ---

static void apply_bamboo_style()
{
    ImGuiStyle& s = ImGui::GetStyle();

    // Color palette inspired by floral-notepaper
    ImVec4 paper      = ImVec4(0.965f, 0.953f, 0.925f, 1.00f);  // #f6f3ec
    ImVec4 paper_warm = ImVec4(0.941f, 0.922f, 0.878f, 1.00f);  // #f0ebe0
    ImVec4 paper_deep = ImVec4(0.910f, 0.882f, 0.827f, 1.00f);  // #e8e1d3
    ImVec4 ink        = ImVec4(0.102f, 0.102f, 0.094f, 1.00f);  // #1a1a18
    ImVec4 ink_soft   = ImVec4(0.239f, 0.239f, 0.220f, 1.00f);  // #3d3d38
    ImVec4 ink_faint  = ImVec4(0.541f, 0.541f, 0.502f, 1.00f);  // #8a8a80
    ImVec4 ink_ghost  = ImVec4(0.722f, 0.722f, 0.682f, 1.00f);  // #b8b8ae
    ImVec4 bamboo     = ImVec4(0.176f, 0.353f, 0.239f, 1.00f);  // #2d5a3d
    ImVec4 bamboo_mist= ImVec4(0.910f, 0.941f, 0.922f, 1.00f);  // #e8f0eb
    ImVec4 bamboo_glow= ImVec4(0.831f, 0.910f, 0.855f, 1.00f);  // #d4e8da
    ImVec4 cloud      = ImVec4(1.000f, 1.000f, 1.000f, 1.00f);  // #ffffff

    s.WindowRounding     = 10.0f;
    s.ChildRounding      = 8.0f;
    s.FrameRounding      = 6.0f;
    s.GrabRounding       = 6.0f;
    s.PopupRounding      = 8.0f;
    s.ScrollbarRounding  = 6.0f;
    s.TabRounding        = 6.0f;

    s.WindowPadding      = ImVec2(16, 16);
    s.FramePadding       = ImVec2(10, 6);
    s.CellPadding        = ImVec2(8, 4);
    s.ItemSpacing        = ImVec2(10, 8);
    s.ItemInnerSpacing   = ImVec2(8, 6);
    s.IndentSpacing      = 22.0f;

    s.WindowBorderSize   = 0.0f;
    s.ChildBorderSize    = 1.0f;
    s.FrameBorderSize    = 1.0f;
    s.PopupBorderSize    = 1.0f;

    // Main colors
    s.Colors[ImGuiCol_WindowBg]         = paper;
    s.Colors[ImGuiCol_ChildBg]          = paper_warm;
    s.Colors[ImGuiCol_PopupBg]          = cloud;
    s.Colors[ImGuiCol_Border]           = paper_deep;

    // Text
    s.Colors[ImGuiCol_Text]             = ink;
    s.Colors[ImGuiCol_TextDisabled]     = ink_ghost;

    // Headers
    s.Colors[ImGuiCol_Header]           = paper_warm;
    s.Colors[ImGuiCol_HeaderHovered]    = bamboo_mist;
    s.Colors[ImGuiCol_HeaderActive]     = bamboo_glow;

    // Title bar
    s.Colors[ImGuiCol_TitleBg]          = paper;
    s.Colors[ImGuiCol_TitleBgActive]    = paper_warm;
    s.Colors[ImGuiCol_TitleBgCollapsed] = paper;

    // Menu bar
    s.Colors[ImGuiCol_MenuBarBg]        = paper_warm;

    // Frame (inputs, combos)
    s.Colors[ImGuiCol_FrameBg]          = cloud;
    s.Colors[ImGuiCol_FrameBgHovered]   = bamboo_mist;
    s.Colors[ImGuiCol_FrameBgActive]    = bamboo_glow;

    // Buttons
    s.Colors[ImGuiCol_Button]           = paper_warm;
    s.Colors[ImGuiCol_ButtonHovered]    = bamboo_mist;
    s.Colors[ImGuiCol_ButtonActive]     = bamboo_glow;

    // Check mark / toggle
    s.Colors[ImGuiCol_CheckMark]        = bamboo;

    // Slider
    s.Colors[ImGuiCol_SliderGrab]       = bamboo;
    s.Colors[ImGuiCol_SliderGrabActive] = bamboo;

    // Separator
    s.Colors[ImGuiCol_Separator]        = paper_deep;
    s.Colors[ImGuiCol_SeparatorHovered] = bamboo;
    s.Colors[ImGuiCol_SeparatorActive]  = bamboo;

    // Resize grip
    s.Colors[ImGuiCol_ResizeGrip]       = paper_deep;
    s.Colors[ImGuiCol_ResizeGripHovered]= bamboo;
    s.Colors[ImGuiCol_ResizeGripActive] = bamboo;

    // Scrollbar
    s.Colors[ImGuiCol_ScrollbarBg]      = paper;
    s.Colors[ImGuiCol_ScrollbarGrab]    = ink_ghost;
    s.Colors[ImGuiCol_ScrollbarGrabHovered] = ink_faint;
    s.Colors[ImGuiCol_ScrollbarGrabActive]  = ink_soft;

    // Table
    s.Colors[ImGuiCol_TableHeaderBg]    = paper_warm;
    s.Colors[ImGuiCol_TableBorderStrong]= paper_deep;
    s.Colors[ImGuiCol_TableBorderLight] = paper_deep;
    s.Colors[ImGuiCol_TableRowBg]       = cloud;
    s.Colors[ImGuiCol_TableRowBgAlt]    = paper;

    // Tab
    s.Colors[ImGuiCol_Tab]              = paper_warm;
    s.Colors[ImGuiCol_TabHovered]       = bamboo_mist;
    s.Colors[ImGuiCol_TabActive]        = bamboo_glow;
    s.Colors[ImGuiCol_TabUnfocused]     = paper;
    s.Colors[ImGuiCol_TabUnfocusedActive] = paper_warm;

    // Nav highlight
    s.Colors[ImGuiCol_NavHighlight]     = bamboo;

    // Plot
    s.Colors[ImGuiCol_PlotHistogram]    = bamboo;
    s.Colors[ImGuiCol_PlotHistogramHovered] = bamboo;
    s.Colors[ImGuiCol_PlotLines]        = bamboo;
    s.Colors[ImGuiCol_PlotLinesHovered] = bamboo;
}

// --- Font loading ---

static void load_fonts(float font_size)
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // Try to load Microsoft YaHei (ships with Windows 10/11, good CJK coverage)
    const char* font_paths[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyhbd.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
    };

    ImFontConfig cfg;
    cfg.MergeMode = false;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = true;

    // Use simplified Chinese glyph range for CJK coverage
    const ImWchar* glyph_range = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();

    bool loaded = false;
    for (const char* fp : font_paths)
    {
        FILE* test = fopen(fp, "rb");
        if (test)
        {
            fclose(test);
            io.Fonts->AddFontFromFileTTF(fp, font_size, &cfg, glyph_range);
            loaded = true;
            break;
        }
    }

    if (!loaded)
    {
        // Fallback: build a larger default font (no CJK but at least readable)
        io.Fonts->AddFontDefault();
    }
}

// --- Main ---

int main(int argc, char** argv)
{
    setup_dpi_awareness();

#if _WIN32
    // 主线程必须是 STA（单线程单元）：系统文件夹对话框（IFileOpenDialog）
    // 在 MTA（多线程单元）下调用 Show() 会死锁卡死，STA 下正常。
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
#endif

    // Setup GLFW
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // 按显示器缩放比例放大窗口，使布局在 100% / 125% / 150% 缩放下保持一致
    float monitor_scale_x = 1.0f, monitor_scale_y = 1.0f;
    GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor();
    if (primary_monitor)
        glfwGetMonitorContentScale(primary_monitor, &monitor_scale_x, &monitor_scale_y);
    float dpi_scale = monitor_scale_x > monitor_scale_y ? monitor_scale_x : monitor_scale_y;
    if (dpi_scale < 1.0f)
        dpi_scale = 1.0f;

    int win_w = (int)(1120.0f * dpi_scale);
    int win_h = (int)(700.0f * dpi_scale);
    GLFWwindow* window = glfwCreateWindow(win_w, win_h, "waifu2x-ncnn-vulkan 图片放大工具", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        return 1;
    }
    g_window = window;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync
    glfwSetDropCallback(window, glfw_drop_callback);

    // Setup Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // We handle settings ourselves

    // 基础字号 16，随显示器缩放比例放大（100% 缩放下即为 16 号）
    load_fonts(16.0f * dpi_scale);
    apply_bamboo_style();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Enumerate GPUs
    {
        ncnn::create_gpu_instance();
        int gpu_count = ncnn::get_gpu_count();
        g_state.gpu_count = gpu_count;
        g_state.gpu_names.clear();
        for (int i = 0; i < gpu_count; i++)
        {
            const char* name = ncnn::get_gpu_info(i).device_name();
            g_state.gpu_names.push_back(name ? name : "Unknown GPU");
        }
        if (gpu_count > 0)
            g_state.gpu_id = 0;
        else
            g_state.gpu_id = -1;
    }

    // Load saved settings
    load_settings(g_state);

    // 解析模型目录（相对路径 -> 绝对路径）
    resolve_model_dir(g_state);

    // 本机 CPU 模式下 ncnn 推理会崩溃（上游问题），有 GPU 时优先使用 GPU，
    // 避免把 -1（CPU）当成默认值保存下来。
    if (g_state.gpu_count > 0 && g_state.gpu_id == -1)
        g_state.gpu_id = 0;

    // Get native window handle for file dialogs
#if _WIN32
    HWND native_hwnd = glfwGetWin32Window(window);
#else
    void* native_hwnd = nullptr;
#endif

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // --- Consume worker events ---
        {
            ProcessEvent ev;
            while (g_worker.pop_event(ev))
            {
                switch (ev.type)
                {
                case ProcessEvent::FileStarted:
                    if (ev.file_index >= 0 && ev.file_index < (int)g_state.file_statuses.size())
                    {
                        g_state.file_statuses[ev.file_index] = AppState::FileStatus::Processing;
                        g_state.current_file_index = ev.file_index;
                    }
                    break;

                case ProcessEvent::FileCompleted:
                    if (ev.file_index >= 0 && ev.file_index < (int)g_state.file_statuses.size())
                    {
                        g_state.file_statuses[ev.file_index] = AppState::FileStatus::Completed;
                        g_state.processed_count = ev.file_index + 1;
                    }
                    break;

                case ProcessEvent::PreviewReady:
                {
                    // 接管预览数据（释放旧数据，保存新数据）
                    {
                        std::lock_guard<std::mutex> lock(g_state.preview_mutex);
                        if (g_state.preview_input_data)
                        {
                            free(g_state.preview_input_data);
                            g_state.preview_input_data = nullptr;
                        }
                        if (g_state.preview_output_data)
                        {
                            free(g_state.preview_output_data);
                            g_state.preview_output_data = nullptr;
                        }
                        g_state.preview_input_data = ev.preview_input;
                        g_state.preview_output_data = ev.preview_output;
                        g_state.preview_input_w = ev.preview_iw;
                        g_state.preview_input_h = ev.preview_ih;
                        g_state.preview_input_c = ev.preview_ic;
                        g_state.preview_output_w = ev.preview_ow;
                        g_state.preview_output_h = ev.preview_oh;
                        g_state.preview_output_c = ev.preview_oc;
                        g_state.preview_available = false;
                        ev.preview_input = nullptr;
                        ev.preview_output = nullptr;
                    }
                    create_preview_textures(g_state);
                    break;
                }

                case ProcessEvent::Error:
                    g_state.error_message = ev.message;
                    if (ev.file_index >= 0 && ev.file_index < (int)g_state.file_statuses.size())
                        g_state.file_statuses[ev.file_index] = AppState::FileStatus::Failed;
                    break;

                case ProcessEvent::AllCompleted:
                    g_state.status = g_state.cancel_requested.load()
                        ? AppState::Status::Cancelled
                        : (g_worker.had_error() ? AppState::Status::Error : AppState::Status::Completed);
                    g_state.processed_count = g_worker.progress_total();
                    break;
                }
            }
        }

        // Check for completion via atomic (safety net)
        if (g_state.status == AppState::Status::Processing && g_worker.is_finished())
        {
            g_state.status = g_state.cancel_requested.load()
                ? AppState::Status::Cancelled
                : (g_worker.had_error() ? AppState::Status::Error : AppState::Status::Completed);
            g_state.processed_count = g_worker.progress_total();
        }

        // Update progress from atomic
        if (g_state.status == AppState::Status::Processing)
            g_state.processed_count = g_worker.progress_current();

        // --- Menu bar ---
        render_main_menu_bar(g_state, &g_show_preview, &g_show_about, &g_quit_requested, native_hwnd);
        render_about_modal(g_state, &g_show_about);
        if (g_quit_requested)
            glfwSetWindowShouldClose(window, true);

        // --- Main layout: full window with two-column split ---
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);

            ImGuiWindowFlags main_flags =
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoSavedSettings;

            ImGui::Begin("##MainContainer", nullptr, main_flags);

            // Left panel: Settings（宽度随字号缩放）
            float left_width = ImGui::GetFontSize() * 20.0f;
            float right_width = ImGui::GetContentRegionAvail().x - left_width - ImGui::GetStyle().ItemSpacing.x;
            if (right_width < ImGui::GetFontSize() * 12.0f)
                right_width = ImGui::GetFontSize() * 12.0f;

            ImGui::BeginChild("##LeftPanel", ImVec2(left_width, 0), true);
            render_settings_panel(g_state, native_hwnd);
            ImGui::EndChild();

            ImGui::SameLine();

            // Right panel: File List + Progress
            ImGui::BeginChild("##RightPanel", ImVec2(right_width, 0), true);

            render_file_list_panel(g_state, native_hwnd);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            render_progress_panel(g_state, g_worker);

            ImGui::EndChild();

            ImGui::End();
        }

        // Preview window
        render_preview_window(g_state, &g_show_preview);

        // --- Render ---
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.965f, 0.953f, 0.925f, 1.0f); // paper color
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Save settings on exit
    save_settings(g_state);

    // Stop worker
    g_worker.stop();

    // 清空事件队列里尚未消费的预览数据
    {
        ProcessEvent ev;
        while (g_worker.pop_event(ev))
        {
            if (ev.type == ProcessEvent::PreviewReady)
            {
                if (ev.preview_input) free(ev.preview_input);
                if (ev.preview_output) free(ev.preview_output);
            }
        }
    }

    // Cleanup preview textures
    g_state.free_preview_data();
    if (g_state.preview_input_tex) glDeleteTextures(1, &g_state.preview_input_tex);
    if (g_state.preview_output_tex) glDeleteTextures(1, &g_state.preview_output_tex);

    // Cleanup ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
