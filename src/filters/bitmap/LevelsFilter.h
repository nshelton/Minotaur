#pragma once

#include "../Filter.h"

struct LevelsFilter : public FilterTyped<Bitmap, Bitmap>
{
    LevelsFilter()
    {
        m_parameters["bias"] = FilterParameter{
            "Bias",
            -1.0f,
            1.0f,
            0.0f
        };
        m_parameters["gain"] = FilterParameter{
            "Gain",
            0.0f,
            4.0f,
            1.0f
        };
        m_parameters["invert"] = FilterParameter{
            "Invert (>=0.5 on)",
            0.0f,
            1.0f,
            0.0f
        };
    }

    const char *name() const override { return "Levels"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const Bitmap &in, Bitmap &out) const override;
};


