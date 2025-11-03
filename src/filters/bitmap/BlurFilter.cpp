#include "filters/bitmap/BlurFilter.h"

static inline uint8_t sampleClamped(const Bitmap &img, int x, int y)
{
    if (x < 0) x = 0; else if (x >= img.width_px) x = img.width_px - 1;
    if (y < 0) y = 0; else if (y >= img.height_px) y = img.height_px - 1;
    return img.pixels[static_cast<size_t>(y) * static_cast<size_t>(img.width_px) + static_cast<size_t>(x)];
}

void BlurFilter::applyTyped(const Bitmap &in, Bitmap &out) const
{
    out.width_px = in.width_px;
    out.height_px = in.height_px;
    out.pixel_size_mm = in.pixel_size_mm;
    out.pixels.resize(static_cast<size_t>(out.width_px) * static_cast<size_t>(out.height_px));

    if (radiusPx <= 0 || in.width_px <= 0 || in.height_px <= 0)
    {
        out.pixels = in.pixels;
        return;
    }

    const int r = radiusPx;
    const int w = in.width_px;
    const int h = in.height_px;
    const int diam = 2 * r + 1;
    const int kernelArea = diam * diam;

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            int sum = 0;
            for (int ky = -r; ky <= r; ++ky)
            {
                for (int kx = -r; kx <= r; ++kx)
                {
                    sum += sampleClamped(in, x + kx, y + ky);
                }
            }
            uint8_t v = static_cast<uint8_t>(sum / kernelArea);
            out.pixels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = v;
        }
    }
}


