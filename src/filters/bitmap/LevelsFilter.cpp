#include "filters/bitmap/LevelsFilter.h"

#include <algorithm>

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void LevelsFilter::applyTyped(const Bitmap &in, Bitmap &out) const
{
    out.width_px = in.width_px;
    out.height_px = in.height_px;
    out.pixel_size_mm = in.pixel_size_mm;
    out.pixels.resize(static_cast<size_t>(out.width_px) * static_cast<size_t>(out.height_px));

    if (in.width_px == 0 || in.height_px == 0 || in.pixels.empty())
    {
        out.pixels.clear();
        return;
    }

    const float bias = m_parameters.at("bias").value;
    const float gain = m_parameters.at("gain").value;
    const bool invert = m_parameters.at("invert").value > 0.5f;

    const size_t total = static_cast<size_t>(in.width_px) * static_cast<size_t>(in.height_px);
    for (size_t i = 0; i < total; ++i)
    {
        float v = static_cast<float>(in.pixels[i]) * (1.0f / 255.0f);
        v = (v + bias) * gain;
        v = clamp01(v);
        if (invert)
        {
            v = 1.0f - v;
        }
        const int outv = static_cast<int>(std::lround(v * 255.0f));
        out.pixels[i] = static_cast<uint8_t>(std::clamp(outv, 0, 255));
    }
}


