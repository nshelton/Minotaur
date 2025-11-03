#pragma once

#include "../Filter.h"

struct SmoothFilter : public FilterTyped<PathSet, PathSet>
{
    SmoothFilter()
    {
        m_parameters["iterations"] = FilterParameter{
            "Iterations",
            0.0f,
            10.0f,
            1.0f
        };
    }

    const char *name() const override { return "Smooth"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const PathSet &in, PathSet &out) const override;
};


