#pragma once

#include "../Filter.h"

// Concentric outline generator: creates concentric outlines of filled shapes
// Uses distance transform to efficiently extract boundaries at different depths
// Parameters:
//  - threshold: 0..255, pixels <= threshold are considered dark (filled)
//  - spacing_px: spacing between outlines in pixels (1 = every pixel, 2 = every 2 pixels, etc.)
struct ConcentricOutlineFilter : public FilterTyped<Bitmap, PathSet>
{
    ConcentricOutlineFilter()
    {
        m_parameters["threshold"] = FilterParameter{
            "Threshold",
            0.0f,
            255.0f,
            128.0f
        };
        m_parameters["spacing_px"] = FilterParameter{
            "Spacing (px)",
            1.0f,
            50.0f,
            2.0f
        };
    }

    const char *name() const override { return "Concentric Outline"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void setThreshold(uint8_t t) { setParameter("threshold", static_cast<float>(t)); }
    void setSpacingPx(float s) { if (s < 1.0f) s = 1.0f; setParameter("spacing_px", s); }

    void applyTyped(const Bitmap &in, PathSet &out) const override;
};
