#pragma once

#include "../Filter.h"
#include <cmath>

struct ColorPickerFilter : public FilterTyped<ColorImage, Bitmap>
{
    ColorPickerFilter()
    {
        m_parameters["r"] = FilterParameter{
            "Red",
            0.0f,
            255.0f,
            128.0f
        };
        m_parameters["g"] = FilterParameter{
            "Green",
            0.0f,
            255.0f,
            128.0f
        };
        m_parameters["b"] = FilterParameter{
            "Blue",
            0.0f,
            255.0f,
            128.0f
        };
        m_parameters["threshold"] = FilterParameter{
            "Threshold",
            0.0f,
            441.67f,  // sqrt(3 * 255^2) = max Euclidean distance in RGB space
            50.0f
        };
    }

    const char *name() const override { return "Color Picker"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const ColorImage &in, Bitmap &out) const override
    {
        const float target_r = m_parameters.at("r").value;
        const float target_g = m_parameters.at("g").value;
        const float target_b = m_parameters.at("b").value;
        const float threshold = m_parameters.at("threshold").value;

        out.width_px = static_cast<int>(in.width_px);
        out.height_px = static_cast<int>(in.height_px);
        out.pixel_size_mm = in.pixel_size_mm;

        const size_t numPixels = in.width_px * in.height_px;
        out.pixels.resize(numPixels);

        // For each pixel, calculate Euclidean distance to target color
        for (size_t i = 0; i < numPixels; ++i)
        {
            size_t rgbIdx = i * 3;
            float r = static_cast<float>(in.pixels[rgbIdx + 0]);
            float g = static_cast<float>(in.pixels[rgbIdx + 1]);
            float b = static_cast<float>(in.pixels[rgbIdx + 2]);

            // Calculate Euclidean distance in RGB space
            float dr = r - target_r;
            float dg = g - target_g;
            float db = b - target_b;
            float distance = std::sqrt(dr * dr + dg * dg + db * db);

            // Output binary: 255 if within threshold, 0 otherwise
            out.pixels[i] = (distance <= threshold) ? 255 : 0;
        }
    }
};
