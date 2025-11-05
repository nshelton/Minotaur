#pragma once

#include <vector>
#include <cstdint>
#include "core/Vec2.h"
#include "core/Pathset.h" // for BoundingBox
#include "filters/LayerBase.h"

struct FloatImage : public ILayerData
{
    size_t width_px{0};
    size_t height_px{0};
    // millimeters per pixel (square pixels)
    float pixel_size_mm{1.0f};
    // row-major, top-to-bottom, left-to-right
    std::vector<float> pixels; // grayscale in [0,1]
    // cached range of pixel values
    float minValue{0.0f};
    float maxValue{1.0f};

    // Local-space bounds in millimeters
    BoundingBox aabb() const
    {
        Vec2 max(static_cast<float>(width_px) * pixel_size_mm,
                 static_cast<float>(height_px) * pixel_size_mm);
        return BoundingBox(Vec2(0.0f, 0.0f), max);
    }

    LayerKind kind() const override { return LayerKind::FloatImage; }

    void computeRange()
    {
        if (pixels.empty())
        {
            minValue = 0.0f;
            maxValue = 1.0f;
            return;
        }
        float mn = pixels[0];
        float mx = pixels[0];
        for (size_t i = 1; i < pixels.size(); ++i)
        {
            float v = pixels[i];
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        minValue = mn;
        maxValue = mx;
    }
};



