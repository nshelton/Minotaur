#pragma once

#include <atomic>

#include "../Filter.h"

struct BlurFilter : public FilterTyped<Bitmap, Bitmap>
{
    BlurFilter()
    {
        m_parameters["radius"] = FilterParameter{
            "radius",
            0.0f,
            10.0f,
            2.0f};
    }

    const char *name() const override { return "Blur"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const Bitmap &in, Bitmap &out) const override;
};
