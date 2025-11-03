#pragma once

#include <atomic>

#include "../Filter.h"

struct SimplifyFilter : public FilterTyped<PathSet, PathSet>
{
    float toleranceMm{0.0f};

    const char *name() const override { return "Simplify"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void setTolerance(float t)
    {
        if (t < 0.0f) t = 0.0f;
        if (t != toleranceMm)
        {
            toleranceMm = t;
            m_version.fetch_add(1);
        }
    }

    void applyTyped(const PathSet &in, PathSet &out) const override;

private:
    std::atomic<uint64_t> m_version{1};
};


