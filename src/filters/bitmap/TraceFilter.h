#pragma once

#include <atomic>

#include "../Filter.h"

// Very simple threshold-based row tracer: produces rough polylines
struct TraceFilter : public FilterTyped<Bitmap, PathSet>
{
    uint8_t threshold{128};

    const char *name() const override { return "Trace"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void setThreshold(uint8_t t)
    {
        if (t != threshold)
        {
            threshold = t;
            m_version.fetch_add(1);
        }
    }

    void applyTyped(const Bitmap &in, PathSet &out) const override;

private:
    std::atomic<uint64_t> m_version{1};
};


