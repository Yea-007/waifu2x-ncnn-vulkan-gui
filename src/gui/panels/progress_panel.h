#ifndef GUI_PROGRESS_PANEL_H
#define GUI_PROGRESS_PANEL_H

#include "imgui.h"
#include <cstdio>
#include "../app_state.h"
#include "../processing_thread.h"
#include "../widgets.h"

#if _WIN32
#include <shellapi.h>
#endif

inline void render_progress_panel(AppState& state, ProcessingThread& worker)
{
    const bool is_processing = (state.status == AppState::Status::Processing);
    const float font_h = ImGui::GetFontSize();

    ImGui::TextColored(ImVec4(0.18f, 0.35f, 0.24f, 1.0f), "处理进度");
    ImGui::Separator();
    ImGui::Spacing();

    const char* status_text = "空闲";
    ImVec4 status_color(0.45f, 0.45f, 0.42f, 1.0f);
    switch (state.status.load())
    {
    case AppState::Status::Idle:      status_text = "空闲"; break;
    case AppState::Status::Processing:status_text = "处理中…"; status_color = ImVec4(0.18f, 0.35f, 0.24f, 1.0f); break;
    case AppState::Status::Completed: status_text = "完成"; status_color = ImVec4(0.18f, 0.40f, 0.24f, 1.0f); break;
    case AppState::Status::Cancelled: status_text = "已取消"; status_color = ImVec4(0.72f, 0.45f, 0.10f, 1.0f); break;
    case AppState::Status::Error:     status_text = "出错"; status_color = ImVec4(0.80f, 0.20f, 0.20f, 1.0f); break;
    }
    ImGui::Text("状态：");
    ImGui::SameLine(0, 4);
    ImGui::TextColored(status_color, "%s", status_text);

    // 进度条
    int total = state.total_count;
    int current = state.processed_count;
    float fraction = total > 0 ? (float)current / (float)total : 0.0f;
    char overlay[64];
    snprintf(overlay, sizeof(overlay), "%d / %d", current, total);
    ImGui::ProgressBar(fraction, ImVec2(-1, font_h * 1.4f), overlay);
    ImGui::Spacing();

    if (is_processing)
    {
        // 取消按钮
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

        if (ImGui::Button("取消处理", ImVec2(-1, font_h * 2.0f)))
        {
            worker.cancel();
            state.cancel_requested = true;
        }

        ImGui::PopStyleColor(4);
    }
    else
    {
        // 开始按钮
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.35f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.42f, 0.29f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.28f, 0.19f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

        if (ImGui::Button("开始处理", ImVec2(-1, font_h * 2.0f)))
        {
            if (state.input_files.empty())
            {
                state.error_message = "请先添加要处理的图片。";
                state.status = AppState::Status::Error;
            }
            else if (state.output_dir.empty() || !state.output_is_dir)
            {
                state.error_message = "请先设置输出目录。";
                state.status = AppState::Status::Error;
            }
            else if (!create_directory_recursive(state.output_dir))
            {
                state.error_message = "无法创建输出目录，请检查路径是否合法。";
                state.status = AppState::Status::Error;
            }
            else
            {
                regenerate_output_files(state);
                state.status = AppState::Status::Processing;
                state.processed_count = 0;
                state.total_count = (int)state.input_files.size();
                state.cancel_requested = false;
                state.error_message.clear();
                worker.start(state);
            }
        }

        ImGui::PopStyleColor(4);
    }

    // 完成提示 + 打开输出目录
    if (state.status.load() == AppState::Status::Completed && !state.input_files.empty())
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.18f, 0.40f, 0.24f, 1.0f), "全部处理完成！");
        if (!state.output_dir.empty())
        {
            ImGui::SameLine(0, 12);
            if (ImGui::Button("打开输出目录"))
            {
#if _WIN32
                if (!create_directory_recursive(state.output_dir))
                {
                    state.error_message = "输出目录不存在且无法创建。";
                }
                else if ((intptr_t)ShellExecuteW(NULL, L"open", state.output_dir.c_str(), NULL, NULL, SW_SHOWNORMAL) <= 32)
                {
                    state.error_message = "无法打开输出目录。";
                }
#endif
            }
        }
    }

    // 错误提示
    if (!state.error_message.empty())
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.80f, 0.20f, 0.20f, 1.0f), "%s", state.error_message.c_str());
        ImGui::SameLine(0, 12);
        if (ImGui::Button("知道了"))
        {
            state.error_message.clear();
            if (state.status.load() == AppState::Status::Error)
                state.status = AppState::Status::Idle;
        }
    }
}

#endif // GUI_PROGRESS_PANEL_H
