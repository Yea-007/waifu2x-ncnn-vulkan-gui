#ifndef GUI_PREVIEW_WINDOW_H
#define GUI_PREVIEW_WINDOW_H

#include "GLFW/glfw3.h"  // for OpenGL types
#include "imgui.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#include <vector>
#include <mutex>
#include "../app_state.h"

inline void create_preview_textures(AppState& state)
{
    if (!state.preview_input_data || !state.preview_output_data)
        return;

    std::lock_guard<std::mutex> lock(state.preview_mutex);

    auto upload_texture = [](unsigned int& tex, const unsigned char* data, int w, int h, int c) {
        if (tex != 0)
            glDeleteTextures(1, &tex);

        // Convert BGR/BGRA to RGB/RGBA for OpenGL
        std::vector<unsigned char> rgb(w * h * c);
        for (int i = 0; i < w * h; i++)
        {
            rgb[i * c + 0] = data[i * c + 2]; // R
            rgb[i * c + 1] = data[i * c + 1]; // G
            rgb[i * c + 2] = data[i * c + 0]; // B
            if (c == 4)
                rgb[i * c + 3] = data[i * c + 3]; // A
        }

        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        GLenum format = (c == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, rgb.data());
    };

    upload_texture(state.preview_input_tex, state.preview_input_data,
                   state.preview_input_w, state.preview_input_h, state.preview_input_c);
    upload_texture(state.preview_output_tex, state.preview_output_data,
                   state.preview_output_w, state.preview_output_h, state.preview_output_c);

    state.preview_available = true;
}

inline void render_preview_window(AppState& state, bool* show)
{
    if (!*show) return;

    ImGui::Begin("预览", show, ImGuiWindowFlags_AlwaysAutoResize);

    if (!state.preview_available)
    {
        ImGui::TextWrapped("暂无预览。\n添加一张图片并点击“开始处理”，完成后即可在这里查看前后对比。");
        ImGui::End();
        return;
    }

    std::lock_guard<std::mutex> lock(state.preview_mutex);

    float avail_w = ImGui::GetContentRegionAvail().x;
    float panel_w = (avail_w - 20) / 2;
    const float kMaxSide = ImGui::GetFontSize() * 20.0f; // 限制预览尺寸，避免窗口过大
    if (panel_w > kMaxSide)
        panel_w = kMaxSide;
    float input_aspect = state.preview_input_h > 0 ? (float)state.preview_input_w / (float)state.preview_input_h : 1.0f;
    float output_aspect = state.preview_output_h > 0 ? (float)state.preview_output_w / (float)state.preview_output_h : 1.0f;

    if (ImGui::BeginChild("Before", ImVec2(panel_w, 0), true))
    {
        ImGui::Text("原图 (%dx%d)", state.preview_input_w, state.preview_input_h);
        float h = panel_w / input_aspect;
        if (h > kMaxSide)
        {
            h = kMaxSide;
            panel_w = h * input_aspect;
        }
        ImGui::Image((ImTextureID)(intptr_t)state.preview_input_tex, ImVec2(panel_w, h));
    }
    ImGui::EndChild();

    ImGui::SameLine();

    float out_w = panel_w;
    if (ImGui::BeginChild("After", ImVec2(out_w, 0), true))
    {
        ImGui::Text("处理后 (%dx%d)", state.preview_output_w, state.preview_output_h);
        float h = out_w / output_aspect;
        if (h > kMaxSide)
        {
            h = kMaxSide;
            out_w = h * output_aspect;
        }
        ImGui::Image((ImTextureID)(intptr_t)state.preview_output_tex, ImVec2(out_w, h));
    }
    ImGui::EndChild();

    ImGui::End();
}

#endif // GUI_PREVIEW_WINDOW_H
