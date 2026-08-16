#ifndef GUI_MAIN_MENU_BAR_H
#define GUI_MAIN_MENU_BAR_H

#include "imgui.h"
#include <vector>
#include "../app_state.h"
#include "../widgets.h"
#include "file_list_panel.h"

inline void render_main_menu_bar(AppState& state, bool* show_preview, bool* show_about,
                                 bool* quit_requested, void* native_hwnd = nullptr)
{
    // Ctrl+O 快捷键：添加图片
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O) && !ImGui::GetIO().WantTextInput)
    {
#if _WIN32
        std::vector<path_t> paths;
        if (browse_files(paths, (HWND)native_hwnd))
        {
            for (auto& p : paths)
                state.input_files.push_back(p);
            regenerate_output_files(state);
        }
#endif
    }

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("文件"))
        {
            if (ImGui::MenuItem("添加图片…", "Ctrl+O"))
            {
#if _WIN32
                std::vector<path_t> paths;
                if (browse_files(paths, (HWND)native_hwnd))
                {
                    for (auto& p : paths)
                        state.input_files.push_back(p);
                    regenerate_output_files(state);
                }
#endif
            }
            if (ImGui::MenuItem("添加文件夹…"))
            {
#if _WIN32
                path_t dirpath;
                if (browse_folder(dirpath, (HWND)native_hwnd))
                {
                    std::vector<path_t> filenames;
                    if (list_directory(dirpath, filenames) == 0)
                    {
                        for (auto& fn : filenames)
                        {
                            if (is_image_file(fn))
                                state.input_files.push_back(dirpath + PATHSTR('/') + fn);
                        }
                        regenerate_output_files(state);
                    }
                }
#endif
            }
            ImGui::Separator();
            if (ImGui::MenuItem("退出", "Alt+F4"))
                *quit_requested = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("视图"))
        {
            ImGui::MenuItem("预览窗口", NULL, show_preview);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("帮助"))
        {
            if (ImGui::MenuItem("关于…"))
            {
                *show_about = true;
                ImGui::OpenPopup("关于");
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

inline void render_about_modal(const AppState& state, bool* open)
{
    if (!*open)
        return;

    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 24.0f, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("关于", open, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("waifu2x-ncnn-vulkan GUI");
        ImGui::TextWrapped("基于 ncnn + Vulkan 的图片降噪与放大工具。");
        ImGui::Spacing();
        ImGui::Text("模型目录：%s", state.model_dir.c_str());
        ImGui::Text("GPU 数量：%d", state.gpu_count);
        ImGui::Text("GPU 设备：%s", state.gpu_id == -1 ? "CPU" :
                    (state.gpu_id < (int)state.gpu_names.size() ? state.gpu_names[state.gpu_id].c_str() : "自动"));
        ImGui::Spacing();
        if (ImGui::Button("关闭", ImVec2(ImGui::GetFontSize() * 8.0f, 0)))
        {
            *open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

#endif // GUI_MAIN_MENU_BAR_H
