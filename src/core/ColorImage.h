#pragma once

#include <vector>
#include <cstdint>
#include "core/Vec2.h"
#include "core/Pathset.h" // for BoundingBox
#include "filters/LayerBase.h"

// RGB color image with 3 channels (8-bit per channel)
struct ColorImage : public ILayerData
{
    size_t width_px{0};
    size_t height_px{0};
    float pixel_size_mm{1.0f};

    // Row-major, top-to-bottom, left-to-right
    // Interleaved RGB: [R0, G0, B0, R1, G1, B1, ...]
    std::vector<uint8_t> pixels;

    // Local-space bounds in millimeters
    BoundingBox aabb() const
    {
        Vec2 max(static_cast<float>(width_px) * pixel_size_mm,
                 static_cast<float>(height_px) * pixel_size_mm);
        return BoundingBox(Vec2(0.0f, 0.0f), max);
    }

    LayerKind kind() const override { return LayerKind::ColorImage; }
};
