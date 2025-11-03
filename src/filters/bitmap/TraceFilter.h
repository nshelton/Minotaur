#pragma once

#include <atomic>

#include "../Filter.h"

// Very simple threshold-based row tracer: produces rough polylines
struct TraceFilter : public FilterTyped<Bitmap, PathSet>
{
    TraceFilter()
    {
        m_parameters["threshold"] = FilterParameter{
            "Threshold",
            0.0f,
            255.0f,
            128.0f
        };
    }

    const char *name() const override { return "Trace"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void setThreshold(uint8_t t) { setParameter("threshold", static_cast<float>(t)); }

    void applyTyped(const Bitmap &in, PathSet &out) const override;
};


