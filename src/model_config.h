#ifndef MODEL_CONFIG_H
#define MODEL_CONFIG_H

#include <string>
#include <stdio.h>
#include "filesystem_utils.h"

struct ModelConfig
{
    path_t model_dir;
    int noise;
    int scale;
    int prepadding;
    path_t param_path;
    path_t model_path;
};

static bool resolve_model_config(const path_t& model_dir, int noise, int scale, ModelConfig& out)
{
    out.model_dir = model_dir;
    out.noise = noise;
    out.scale = scale;

    // determine prepadding
    if (model_dir.find(PATHSTR("models-cunet")) != path_t::npos)
    {
        if (noise == -1)
            out.prepadding = 18;
        else if (scale == 1)
            out.prepadding = 28;
        else if (scale == 2 || scale == 4 || scale == 8 || scale == 16 || scale == 32)
            out.prepadding = 18;
        else
            return false;
    }
    else if (model_dir.find(PATHSTR("models-upconv_7_anime_style_art_rgb")) != path_t::npos)
    {
        out.prepadding = 7;
    }
    else if (model_dir.find(PATHSTR("models-upconv_7_photo")) != path_t::npos)
    {
        out.prepadding = 7;
    }
    else
    {
        return false;
    }

    // build param and model file paths
#if _WIN32
    wchar_t parambuf[256];
    wchar_t modelbuf[256];
    if (noise == -1)
    {
        swprintf(parambuf, 256, L"%s/scale2.0x_model.param", model_dir.c_str());
        swprintf(modelbuf, 256, L"%s/scale2.0x_model.bin", model_dir.c_str());
    }
    else if (scale == 1)
    {
        swprintf(parambuf, 256, L"%s/noise%d_model.param", model_dir.c_str(), noise);
        swprintf(modelbuf, 256, L"%s/noise%d_model.bin", model_dir.c_str(), noise);
    }
    else if (scale == 2 || scale == 4 || scale == 8 || scale == 16 || scale == 32)
    {
        swprintf(parambuf, 256, L"%s/noise%d_scale2.0x_model.param", model_dir.c_str(), noise);
        swprintf(modelbuf, 256, L"%s/noise%d_scale2.0x_model.bin", model_dir.c_str(), noise);
    }
    else
    {
        return false;
    }
#else
    char parambuf[256];
    char modelbuf[256];
    if (noise == -1)
    {
        sprintf(parambuf, "%s/scale2.0x_model.param", model_dir.c_str());
        sprintf(modelbuf, "%s/scale2.0x_model.bin", model_dir.c_str());
    }
    else if (scale == 1)
    {
        sprintf(parambuf, "%s/noise%d_model.param", model_dir.c_str(), noise);
        sprintf(modelbuf, "%s/noise%d_model.bin", model_dir.c_str(), noise);
    }
    else if (scale == 2 || scale == 4 || scale == 8 || scale == 16 || scale == 32)
    {
        sprintf(parambuf, "%s/noise%d_scale2.0x_model.param", model_dir.c_str(), noise);
        sprintf(modelbuf, "%s/noise%d_scale2.0x_model.bin", model_dir.c_str(), noise);
    }
    else
    {
        return false;
    }
#endif

    out.param_path = sanitize_filepath(parambuf);
    out.model_path = sanitize_filepath(modelbuf);
    return true;
}

// Number of 2x passes needed to achieve the target scale
static int scale_pass_count(int scale)
{
    if (scale == 1) return 1;
    if (scale == 2) return 1;
    if (scale == 4) return 2;
    if (scale == 8) return 3;
    if (scale == 16) return 4;
    if (scale == 32) return 5;
    return 1;
}

#endif // MODEL_CONFIG_H
