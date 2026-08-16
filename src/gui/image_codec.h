#ifndef GUI_IMAGE_CODEC_H
#define GUI_IMAGE_CODEC_H

#include <stdio.h>
#include <stdlib.h>
#include <string>

#if _WIN32
#include "wic_image.h"
#else
#include "jpeg_image.h"
#include "png_image.h"
#endif
#include "webp_image.h"
#include "filesystem_utils.h"

// Decode image from file. Returns malloc'd buffer, or nullptr on failure.
// pixeldata is BGR/BGRA on Windows, RGB/RGBA elsewhere.
// Caller must free() the returned buffer.
static unsigned char* decode_image(const path_t& filepath, int* w, int* h, int* c)
{
#if _WIN32
    FILE* fp = _wfopen(filepath.c_str(), L"rb");
#else
    FILE* fp = fopen(filepath.c_str(), "rb");
#endif
    if (!fp)
        return 0;

    fseek(fp, 0, SEEK_END);
    int length = ftell(fp);
    rewind(fp);
    unsigned char* filedata = (unsigned char*)malloc(length);
    if (filedata)
    {
        fread(filedata, 1, length, fp);
    }
    fclose(fp);

    if (!filedata)
        return 0;

    unsigned char* pixeldata = webp_load(filedata, length, w, h, c);
    if (!pixeldata)
    {
#if _WIN32
        pixeldata = wic_decode_image(filepath.c_str(), w, h, c);
#else
        pixeldata = jpeg_load(filedata, length, w, h, c);
        if (!pixeldata)
        {
            pixeldata = png_load(filedata, length, w, h, c);
        }
#endif
    }

    free(filedata);
    return pixeldata;
}

// Encode image to file. format is "png", "jpg", or "webp".
// pixeldata is BGR/BGRA on Windows, RGB/RGBA elsewhere.
// Returns true on success.
static bool encode_image(const path_t& filepath, int w, int h, int c,
                         const unsigned char* pixeldata, const std::string& format)
{
    if (format == "webp")
    {
#if _WIN32
        return webp_save(filepath.c_str(), w, h, c, pixeldata) != 0;
#else
        return webp_save(filepath.c_str(), w, h, c, pixeldata) != 0;
#endif
    }
    else if (format == "jpg" || format == "jpeg")
    {
#if _WIN32
        return wic_encode_jpeg_image(filepath.c_str(), w, h, c, (void*)pixeldata) != 0;
#else
        return jpeg_save(filepath.c_str(), w, h, c, pixeldata) != 0;
#endif
    }
    else // png
    {
#if _WIN32
        return wic_encode_image(filepath.c_str(), w, h, c, (void*)pixeldata) != 0;
#else
        return png_save(filepath.c_str(), w, h, c, pixeldata) != 0;
#endif
    }
}

#endif // GUI_IMAGE_CODEC_H
