#include "BitmapGenerator.h"

#include <cmath>

namespace BitmapGenerator
{

Bitmap Gradient(int w, int h, float pixel_size_mm)
{
    Bitmap b;
    b.width_px = w;
    b.height_px = h;
    b.pixel_size_mm = pixel_size_mm;
    b.pixels.resize(static_cast<size_t>(w * h));
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            float t = (static_cast<float>(x) / static_cast<float>(w - 1));
            uint8_t g = static_cast<uint8_t>(std::round(t * 255.0f));
            b.pixels[static_cast<size_t>(y * w + x)] = g;
        }
    }
    return b;
}

Bitmap Checkerboard(int w, int h, int block_px, float pixel_size_mm)
{
    Bitmap b;
    b.width_px = w;
    b.height_px = h;
    b.pixel_size_mm = pixel_size_mm;
    b.pixels.resize(static_cast<size_t>(w * h));
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            int cx = (x / block_px) & 1;
            int cy = (y / block_px) & 1;
            bool on = (cx ^ cy) == 0;
            b.pixels[static_cast<size_t>(y * w + x)] = on ? 200 : 40;
        }
    }
    return b;
}

Bitmap Radial(int w, int h, float pixel_size_mm)
{
    Bitmap b;
    b.width_px = w;
    b.height_px = h;
    b.pixel_size_mm = pixel_size_mm;
    b.pixels.resize(static_cast<size_t>(w * h));
    float cx = (w - 1) * 0.5f;
    float cy = (h - 1) * 0.5f;
    float maxR = std::sqrt(cx * cx + cy * cy);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            float dx = static_cast<float>(x) - cx;
            float dy = static_cast<float>(y) - cy;
            float r = std::sqrt(dx * dx + dy * dy);
            float t = r / maxR;
            uint8_t g = static_cast<uint8_t>(std::round((1.0f - t) * 255.0f));
            b.pixels[static_cast<size_t>(y * w + x)] = g;
        }
    }
    return b;
}

} // namespace BitmapGenerator


