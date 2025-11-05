#pragma once

#include "../Filter.h"

struct BitmapToFloatFilter : public FilterTyped<Bitmap, FloatImage>
{
    const char *name() const override { return "Bitmap to Float"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const Bitmap &in, FloatImage &out) const override
    {
        out.width_px = in.width_px;
        out.height_px = in.height_px;
        out.pixel_size_mm = in.pixel_size_mm;
        out.pixels.resize(out.width_px * out.height_px);
        const float inv255 = 1.0f / 255.0f;
        for (size_t i = 0; i < out.pixels.size(); ++i)
        {
            out.pixels[i] = static_cast<float>(in.pixels[i]) * inv255;
        }
        out.computeRange();
    }
};


