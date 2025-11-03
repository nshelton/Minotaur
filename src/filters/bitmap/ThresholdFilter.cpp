#include "filters/bitmap/ThresholdFilter.h"

void ThresholdFilter::applyTyped(const Bitmap &in, Bitmap &out) const
{
    out.width_px = in.width_px;
    out.height_px = in.height_px;
    out.pixel_size_mm = in.pixel_size_mm;
    out.pixels.resize(static_cast<size_t>(out.width_px) * static_cast<size_t>(out.height_px));

    if (in.width_px <= 0 || in.height_px <= 0 || in.pixels.empty())
    {
        out.pixels.clear();
        return;
    }

    const int w = in.width_px;
    const int h = in.height_px;
    const uint8_t th = m_parameters.at("threshold").value;

    for (size_t y = 0; y < h; ++y)
    {
        for (size_t x = 0; x < w; ++x)
        {
            int idx = y * w + x;
            uint8_t v = in.pixels[idx];
            out.pixels[idx] = (v >= th) ? 255 : 0;
        }
    }
}


