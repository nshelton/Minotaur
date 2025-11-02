#pragma once

#include "core/core.h"

namespace BitmapGenerator
{
    Bitmap Gradient(int w, int h, float pixel_size_mm);
    Bitmap Checkerboard(int w, int h, int block_px, float pixel_size_mm);
    Bitmap Radial(int w, int h, float pixel_size_mm);
}


