#pragma once

#include <atomic>

#include "../Filter.h"

struct SimplifyFilter : public FilterTyped<PathSet, PathSet>
{
    SimplifyFilter()
    {
        m_parameters["toleranceMm"] = FilterParameter{
            "Tolerance (mm)",
            0.0f,
            100.0f,
            0.0f
        };
    }

    const char *name() const override { return "Simplify"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void setTolerance(float t) { if (t < 0.0f) t = 0.0f; setParameter("toleranceMm", t); }

    void applyTyped(const PathSet &in, PathSet &out) const override;
};


