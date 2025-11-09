#pragma once
#include "../Filter.h"

// Bitmap -> Bitmap morphological erosion/dilation
struct ErodeDilateFilter : public FilterTyped<Bitmap, Bitmap>
{
    ErodeDilateFilter()
    {
        m_parameters["operation"] = FilterParameter{"operation", 0.0f, 1.0f, 1.0f};
        m_parameters["iterations"] = FilterParameter{"iterations", 1.0f, 8.0f, 1.0f};
    }

    const char *name() const override { return "Erode/Dilate"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const Bitmap &in, Bitmap &out) const override;
};