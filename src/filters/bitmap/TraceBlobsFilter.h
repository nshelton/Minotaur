#pragma once

#include <atomic>

#include "../Filter.h"

// Trace connected pixel blobs into closed polylines with optional RDP simplification
struct TraceBlobsFilter : public FilterTyped<Bitmap, PathSet>
{
    TraceBlobsFilter()
    {
        m_parameters["tolerancePx"] = FilterParameter{
            "Tolerance (px)",
            0.0f,
            10.0f,
            1.0f
        };
        m_parameters["turdSizePx"] = FilterParameter{
            "Min Area (px)",
            0.0f,
            100.0f,
            8.0f
        };
        // When enabled (>0.5), also trace inner contours (holes) fully enclosed by a blob
        m_parameters["traceHoles"] = FilterParameter{
            "Trace Holes (0/1)",
            0.0f,
            1.0f,
            1.0f
        };
    }

    const char *name() const override { return "Blobs"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void setTolerancePx(float t) { if (t < 0.0f) t = 0.0f; setParameter("tolerancePx", t); }
    void setTurdSizePx(float s) { if (s < 0.0f) s = 0.0f; setParameter("turdSizePx", s); }
    void setTraceHoles(bool on) { setParameter("traceHoles", on ? 1.0f : 0.0f); }

    void applyTyped(const Bitmap &in, PathSet &out) const override;
};



