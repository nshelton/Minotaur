#pragma once

#include "../Filter.h"

// Crosshatch line generator: draws parallel line segments over dark pixels
// Parameters:
//  - step_px: spacing between parallel lines in pixels
//  - angle_deg: direction of hatch lines in degrees
//  - threshold: 0..255, pixels <= threshold are considered dark (drawn)
struct LineHatchFilter : public FilterTyped<Bitmap, PathSet>
{
    LineHatchFilter()
    {
        m_parameters["step_px"] = FilterParameter{
            "Step (px)",
            1.0f,
            20.0f,
            8.0f
        };
        m_parameters["angle_deg"] = FilterParameter{
            "Angle (deg)",
            -180.0f,
            180.0f,
            45.0f
        };
        m_parameters["threshold"] = FilterParameter{
            "Threshold",
            0.0f,
            255.0f,
            128.0f
        };
    }

    const char *name() const override { return "Line Hatch"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void setStepPx(float s) { if (s < 1.0f) s = 1.0f; setParameter("step_px", s); }
    void setAngleDeg(float a) { setParameter("angle_deg", a); }
    void setThreshold(uint8_t t) { setParameter("threshold", static_cast<float>(t)); }

    void applyTyped(const Bitmap &in, PathSet &out) const override;
};


