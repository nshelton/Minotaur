#pragma once

#include <atomic>
#include "../Filter.h"

// Voronoi stippling: converts image to weighted point distribution using Lloyd's relaxation
struct VoronoiStipplingFilter : public FilterTyped<Bitmap, PathSet>
{
    VoronoiStipplingFilter()
    {
        m_parameters["num_points"] = FilterParameter{
            "Number of Points",
            10.0f,
            100000.0f,
            500.0f
        };
        m_parameters["iterations"] = FilterParameter{
            "Iterations",
            1.0f,
            100.0f,
            20.0f
        };
        m_parameters["circle_radius"] = FilterParameter{
            "Circle Radius (mm)",
            0.1f,
            10.0f,
            0.5f
        };
        m_parameters["size_variation"] = FilterParameter{
            "Size Variation",
            0.0f,
            1.0f,
            0.0f
        };
        m_parameters["circle_segments"] = FilterParameter{
            "Circle Segments",
            3.0f,
            10.0f,
            5.0f
        };
    }

    const char *name() const override { return "Voronoi Stippling"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const Bitmap &in, PathSet &out) const override;
};
