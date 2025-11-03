#pragma once

#include <vector>
#include <cstdint>
#include "core/Vec2.h"
#include "core/Pathset.h" // for BoundingBox
#include "filters/LayerBase.h"

struct Bitmap : public ILayerData
{
    size_t width_px{0};
    size_t height_px{0};
    // millimeters per pixel (square pixels)
    float pixel_size_mm{1.0f};
    // row-major, top-to-bottom, left-to-right
    std::vector<uint8_t> pixels;

    // Local-space bounds in millimeters
    BoundingBox aabb() const
    {
        Vec2 max(static_cast<float>(width_px) * pixel_size_mm,
                 static_cast<float>(height_px) * pixel_size_mm);
        return BoundingBox(Vec2(0.0f, 0.0f), max);
    }

    LayerKind kind() const override { return LayerKind::Bitmap; }
};


