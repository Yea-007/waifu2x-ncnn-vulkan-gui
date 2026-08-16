#ifndef GUI_FILE_LIST_PANEL_H
#define GUI_FILE_LIST_PANEL_H

#include "imgui.h"
#include <vector>
#include <string>
#include <algorithm>
#include "../app_state.h"
#include "../widgets.h"
#include "filesystem_utils.h"

inline void regenerate_output_files(AppState& state)
{
    state.output_files.clear();
    state.file_statuses.clear();

    int ext_len = (int)state.output_format.size();
    path_t ext;
    ext.reserve(ext_len);
    for (int i = 0; i < ext_len; i++)
        ext.push_back((wchar_t)(unsigned char)state.output_format[i]);

    for (size_t i = 0; i < state.input_files.size(); i++)
    {
        // input_files 里保存的是完整路径，先取出文件名再拼接输出路径
        path_t filename = get_file_name(state.input_files[i]);
        path_t filename_noext = get_file_name_without_extension(filename);
        path_t output_filename = filename_noext + PATHSTR('.') + ext;

        if (state.output_is_dir && !state.output_dir.empty())
            state.output_files.push_back(state.output_dir + PATHSTR('/') + output_filename);
        else
            state.output_files.push_back(output_filename);

        state.file_statuses.push_back(AppState::FileStatus::Pending);
    }

    if (state.selected_file_index >= (int)state.input_files.size())
        state.selected_file_index = -1;
}

inline void render_file_list_panel(AppState& state, void* native_window = nullptr)
{
    const float full_w = ImGui::GetContentRegionAvail().x;
    const float font_h = ImGui::GetFontSize();

    ImGui::TextColored(ImVec4(0.18f, 0.35f, 0.24f, 1.0f), "文件列表（%d 个）", (int)state.input_files.size());
    ImGui::Separator();
    ImGui::Spacing();

    // 添加 / 清空
    float btn_w = (full_w - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
    if (ImGui::Button("添加图片…", ImVec2(btn_w, 0)))
    {
#if _WIN32
        std::vector<path_t> paths;
        if (browse_files(paths, (HWND)native_window))
        {
            for (auto& p : paths)
                state.input_files.push_back(p);
            regenerate_output_files(state);
        }
#endif
    }
    ImGui::SameLine();

    if (ImGui::Button("添加文件夹…", ImVec2(btn_w, 0)))
    {
#if _WIN32
        path_t dirpath;
        if (browse_folder(dirpath, (HWND)native_window))
        {
            std::vector<path_t> filenames;
            if (list_directory(dirpath, filenames) == 0)
            {
                for (auto& fn : filenames)
                {
                    // 只添加图片文件
                    if (is_image_file(fn))
                        state.input_files.push_back(dirpath + PATHSTR('/') + fn);
                }
                regenerate_output_files(state);
            }
        }
#endif
    }
    ImGui::SameLine();

    if (ImGui::Button("清空列表", ImVec2(btn_w, 0)))
        state.reset_files();

    // 移除选中（无选中时置灰）
    bool has_selection = state.selected_file_index >= 0 &&
                         state.selected_file_index < (int)state.input_files.size();
    ImGui::BeginDisabled(!has_selection);
    if (ImGui::Button("移除选中文件", ImVec2(full_w, 0)))
    {
        state.input_files.erase(state.input_files.begin() + state.selected_file_index);
        state.selected_file_index = -1;
        regenerate_output_files(state);
    }
    ImGui::EndDisabled();

    ImGui::Spacing();

    if (state.input_files.empty())
    {
        ImGui::TextDisabled("把图片直接拖进窗口，或点击上方按钮添加。");
        return;
    }

    // 表格高度：占满剩余空间，但给下方的进度面板留出位置
    float progress_reserve = font_h * 8.5f;
    if (!state.error_message.empty())
        progress_reserve += font_h * 2.5f;
    float table_h = ImGui::GetContentRegionAvail().y - progress_reserve;
    if (table_h < font_h * 5.0f)
        table_h = font_h * 5.0f;

    if (ImGui::BeginTable("files", 3,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, table_h)))
    {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, font_h * 1.6f);
        ImGui::TableSetupColumn("文件", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthFixed, font_h * 5.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)state.input_files.size(); i++)
        {
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text("%d", i + 1);

            ImGui::TableNextColumn();
            std::string path_str = path_to_utf8(state.input_files[i]);
            bool selected = (state.selected_file_index == i);
            if (ImGui::Selectable(path_str.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                state.selected_file_index = i;

            ImGui::TableNextColumn();
            if (i < (int)state.file_statuses.size())
            {
                switch (state.file_statuses[i])
                {
                case AppState::FileStatus::Pending:
                    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.50f, 1.0f), "等待中");
                    break;
                case AppState::FileStatus::Processing:
                    ImGui::TextColored(ImVec4(0.18f, 0.35f, 0.24f, 1.0f), "处理中…");
                    break;
                case AppState::FileStatus::Completed:
                    ImGui::TextColored(ImVec4(0.18f, 0.40f, 0.24f, 1.0f), "完成");
                    break;
                case AppState::FileStatus::Failed:
                    ImGui::TextColored(ImVec4(0.80f, 0.20f, 0.20f, 1.0f), "失败");
                    break;
                }
            }
        }

        ImGui::EndTable();
    }
}

#endif // GUI_FILE_LIST_PANEL_H
