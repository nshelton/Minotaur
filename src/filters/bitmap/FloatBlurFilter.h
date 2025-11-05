#pragma once

#include "../Filter.h"

struct FloatBlurFilter : public FilterTyped<FloatImage, FloatImage>
{
    FloatBlurFilter()
    {
        m_parameters["radius"] = FilterParameter{"radius", 0.0f, 20.0f, 2.0f};
    }

    const char *name() const override { return "Float Blur"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const FloatImage &in, FloatImage &out) const override;
};


