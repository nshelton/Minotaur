#pragma once

#include "../Filter.h"

struct CannyFilter : public FilterTyped<Bitmap, Bitmap>
{
    CannyFilter()
    {
        m_parameters["blur_radius_px"] = FilterParameter{
            "Blur Radius (px)",
            0.0f,
            5.0f,
            1.0f
        };
        m_parameters["low_threshold"] = FilterParameter{
            "Low Threshold",
            0.0f,
            255.0f,
            50.0f
        };
        m_parameters["high_threshold"] = FilterParameter{
            "High Threshold",
            0.0f,
            255.0f,
            150.0f
        };
    }

    const char *name() const override { return "Canny"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const Bitmap &in, Bitmap &out) const override;
};


