#pragma once

#include <atomic>
#include "../Filter.h"

// Subdivides paths to ensure at least one node every N mm
struct RegularSubdivisionFilter : public FilterTyped<PathSet, PathSet>
{
    RegularSubdivisionFilter()
    {
        m_parameters["spacing"] = FilterParameter{
            "Node Spacing (mm)",
            0.1f,
            10.0f,
            1.0f
        };
    }

    const char *name() const override { return "Regular Subdivision"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const PathSet &in, PathSet &out) const override;
};
