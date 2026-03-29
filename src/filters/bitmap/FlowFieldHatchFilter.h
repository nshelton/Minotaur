#pragma once

#include "../Filter.h"

// Flow-field hatching: seeds dark pixels on a grid, then chains them
// along a direction field derived from the image.
// Parameters:
//  - flow_field_type: 0 = Sobel tangent, 1 = Clamped Distance Field tangent
//  - threshold: 0..255, pixels <= threshold are dark (seeded)
//  - seed_spacing_px: grid spacing for seed placement
//  - max_connect_distance_px: max distance to chain two seeds
//  - flow_strength: 0 = pure fixed angle, 1 = pure flow field
//  - hatch_angle_deg: fallback hatch direction
//  - clamp_radius_px: how far distance-field gradient propagates (mode 1)
struct FlowFieldHatchFilter : public FilterTyped<Bitmap, PathSet>
{
    FlowFieldHatchFilter()
    {
        m_parameters["flow_field_type"] = FilterParameter{
            "Flow Field Type",
            0.0f,
            1.0f,
            0.0f
        };
        m_parameters["threshold"] = FilterParameter{
            "Threshold",
            0.0f,
            255.0f,
            128.0f,
            FilterParameter::Int
        };
        m_parameters["seed_spacing_px"] = FilterParameter{
            "Seed Spacing (px)",
            2.0f,
            50.0f,
            8.0f,
            FilterParameter::Int
        };
        m_parameters["max_connect_distance_px"] = FilterParameter{
            "Max Connect (px)",
            1.0f,
            100.0f,
            20.0f,
            FilterParameter::Int
        };
        m_parameters["flow_strength"] = FilterParameter{
            "Flow Strength",
            0.0f,
            1.0f,
            0.8f
        };
        m_parameters["hatch_angle_deg"] = FilterParameter{
            "Hatch Angle (deg)",
            -180.0f,
            180.0f,
            45.0f
        };
        m_parameters["clamp_radius_px"] = FilterParameter{
            "Clamp Radius (px)",
            1.0f,
            100.0f,
            20.0f,
            FilterParameter::Int
        };
    }

    const char *name() const override { return "Flow Field Hatch"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const Bitmap &in, PathSet &out) const override;
};