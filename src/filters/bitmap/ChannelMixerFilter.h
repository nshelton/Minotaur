#pragma once

#include "../Filter.h"

struct ChannelMixerFilter : public FilterTyped<ColorImage, Bitmap>
{
    ChannelMixerFilter()
    {
        // Default weights use Rec. 709 luma coefficients
        m_parameters["r"] = FilterParameter{
            "Red",
            0.0f,
            1.0f,
            0.2126f
        };
        m_parameters["g"] = FilterParameter{
            "Green",
            0.0f,
            1.0f,
            0.7152f
        };
        m_parameters["b"] = FilterParameter{
            "Blue",
            0.0f,
            1.0f,
            0.0722f
        };
    }

    const char *name() const override { return "RGB to Grayscale"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const ColorImage &in, Bitmap &out) const override
    {
        const float r_weight = m_parameters.at("r").value;
        const float g_weight = m_parameters.at("g").value;
        const float b_weight = m_parameters.at("b").value;

        out.width_px = static_cast<int>(in.width_px);
        out.height_px = static_cast<int>(in.height_px);
        out.pixel_size_mm = in.pixel_size_mm;

        const size_t numPixels = in.width_px * in.height_px;
        out.pixels.resize(numPixels);

        // Convert RGB to grayscale using channel mixer weights
        for (size_t i = 0; i < numPixels; ++i)
        {
            size_t rgbIdx = i * 3;
            float r = static_cast<float>(in.pixels[rgbIdx + 0]);
            float g = static_cast<float>(in.pixels[rgbIdx + 1]);
            float b = static_cast<float>(in.pixels[rgbIdx + 2]);

            float gray = r * r_weight + g * g_weight + b * b_weight;

            // Clamp to [0, 255]
            gray = std::max(0.0f, std::min(255.0f, gray));

            out.pixels[i] = static_cast<uint8_t>(gray + 0.5f);
        }
    }
};
