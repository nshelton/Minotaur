#pragma once

#include <string>
#include <variant>
#include "core/Pathset.h"
#include "core/Bitmap.h"
#include "core/FloatImage.h"
#include "filters/FilterChain.h"

enum class EntityType
{
    PathSet,
    Bitmap,
    FloatImage
};

struct Entity
{
    int id;
    std::string name;
    std::variant<PathSet, Bitmap, FloatImage> payload;
    uint64_t payloadVersion{1};
    bool visible{true};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};

    // takes a point from local entity space (mm) to page space (mm)
    Mat3 localToPage;

    EntityType type() const
    {
        if (std::holds_alternative<PathSet>(payload)) return EntityType::PathSet;
        if (std::holds_alternative<Bitmap>(payload)) return EntityType::Bitmap;
        return EntityType::FloatImage;
    }

    BoundingBox boundsLocal() const
    {
        if (auto ps = std::get_if<PathSet>(&payload))
        {
            ps->computeAABB();
            return ps->aabb;
        }
        if (auto bmp = std::get_if<Bitmap>(&payload))
        {
            return bmp->aabb();
        }
        const FloatImage &fi = std::get<FloatImage>(payload);
        return fi.aabb();
    }

    bool contains(const Vec2 &point, float margin_mm = 0) const
    {
        return boundsLocal().contains(localToPage / point, margin_mm);
    }

    const PathSet *pathset() const { return std::get_if<PathSet>(&payload); }
    PathSet *pathset() { return std::get_if<PathSet>(&payload); }
    const Bitmap *bitmap() const { return std::get_if<Bitmap>(&payload); }
    Bitmap *bitmap() { return std::get_if<Bitmap>(&payload); }
    const FloatImage *floatImage() const { return std::get_if<FloatImage>(&payload); }
    FloatImage *floatImage() { return std::get_if<FloatImage>(&payload); }

    // Filter chain: transforms from base payload to display/output layer
    FilterChain filterChain;

    // Helper to package current payload into a LayerPtr
    LayerPtr baseLayer() const
    {
        if (auto ps = std::get_if<PathSet>(&payload))
            return makeLayerFrom(*ps);
        if (auto bmp = std::get_if<Bitmap>(&payload))
            return makeLayerFrom(*bmp);
        const FloatImage &fi = std::get<FloatImage>(payload);
        return makeLayerFrom(fi);
    }

    void refreshFilterBase()
    {
        filterChain.setBase(baseLayer(), payloadVersion);
    }
};