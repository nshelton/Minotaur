#pragma once

#include "../Filter.h"

// Contrast Limited Adaptive Histogram Equalization for 8-bit grayscale bitmaps
struct ClaheFilter : public FilterTyped<Bitmap, Bitmap>
{
    ClaheFilter()
    {
        m_parameters["tilesX"] = FilterParameter{
            "Tiles X",
            1.0f,
            64.0f,
            8.0f
        };
        m_parameters["tilesY"] = FilterParameter{
            "Tiles Y",
            1.0f,
            64.0f,
            8.0f
        };
        m_parameters["clipLimit"] = FilterParameter{
            "Clip Limit (0=off)",
            0.0f,
            4.0f,
            2.0f
        };
    }

    const char *name() const override { return "CLAHE"; }
    uint64_t paramVersion() const override { return m_version.load(); }

    void applyTyped(const Bitmap &in, Bitmap &out) const override;
};


