#pragma once


#include "../Filter.h"

struct ThresholdFilter : public FilterTyped<Bitmap, Bitmap>
{
    ThresholdFilter()
    {
        m_parameters["threshold"] = FilterParameter{
            "Threshold",
            0.0f,
            255.0f,
            128.0f
        };
    }

    const char *name() const override { return "Threshold"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const Bitmap &in, Bitmap &out) const override;


};


