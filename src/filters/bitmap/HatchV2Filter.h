#pragma once

#include "../Filter.h"

// Boustrophedon (serpentine) hatch generator.
//
// Like LineHatch, sweeps parallel scan lines across the image and keeps the dark
// runs, but instead of emitting anonymous disconnected segments it records the
// row/interval structure and stitches the runs into continuous back-and-forth
// strokes. Each blob that forms a simple top-to-bottom chain of rows becomes a
// single zig-zag stroke (optimal, no pen lift). Branches, holes and gaps split
// into additional strokes but never produce closed loops or paired rectangles
// (the artifact you get from running LineHatch + Optimize Paths).
//
// Parameters:
//  - step_px:       spacing between parallel scan lines in pixels
//  - angle_deg:     direction of hatch lines in degrees
//  - threshold:     0..255, pixels <= threshold are considered dark (drawn)
//  - connect_scale: max inter-row connector length, as a multiple of row spacing.
//                   Rows whose nearest endpoints are farther apart than this are
//                   left as separate strokes (pen up) instead of being bridged.
struct HatchV2Filter : public FilterTyped<Bitmap, PathSet>
{
    HatchV2Filter()
    {
        m_parameters["step_px"] = FilterParameter{
            "Step (px)",
            1.0f,
            20.0f,
            8.0f,
            FilterParameter::Int
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
            128.0f,
            FilterParameter::Int
        };
        m_parameters["connect_scale"] = FilterParameter{
            "Connect x row",
            1.0f,
            8.0f,
            1.5f
        };
    }

    const char *name() const override { return "Hatch V2 (Serpentine)"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const Bitmap &in, PathSet &out) const override;
};
