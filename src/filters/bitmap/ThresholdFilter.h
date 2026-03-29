#pragma once


#include "../Filter.h"

struct ThresholdFilter : public FilterTyped<Bitmap, Bitmap>
{
    ThresholdFilter()
    {
        m_parameters["min"] = FilterParameter{
            "Min",
            0.0f,
            255.0f,
            50.0f,
            FilterParameter::Int
        };
        m_parameters["max"] = FilterParameter{
            "Max",
            0.0f,
            255.0f,
            150.0f,
            FilterParameter::Int
        };
    }

    const char *name() const override { return "Threshold"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const Bitmap &in, Bitmap &out) const override;


};


