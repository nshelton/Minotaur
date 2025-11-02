#pragma once

#include <string>
#include "core/core.h"

namespace ImageLoader
{
    // Minimal loader: support Portable Graymap (PGM, P5) into 8-bit grayscale Bitmap
    // Returns true on success; errorOut (optional) filled on failure
    bool loadPGM(const std::string &filePath, Bitmap &out, std::string *errorOut = nullptr, float pixel_size_mm = 0.5f);

    // Windows (WIC) loader for common formats (PNG, JPEG, BMP, etc.)
    // Converts image to 8-bit grayscale into Bitmap
    bool loadImage(const std::string &filePath, Bitmap &out, std::string *errorOut = nullptr, float pixel_size_mm = 0.5f);
}


