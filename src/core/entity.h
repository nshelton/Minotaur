#pragma once

#include <string>
#include <variant>
#include "core/Pathset.h"
#include "core/Bitmap.h"
#include "filters/FilterChain.h"

enum class EntityType
{
    PathSet,
    Bitmap
};

struct Entity
{
    int id;
    std::string name;
    std::variant<PathSet, Bitmap> payload;
    uint64_t payloadVersion{1};
    bool visible{true};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};

    // takes a point from local entity space (mm) to page space (mm)
    Mat3 localToPage;

    EntityType type() const
    {
        return std::holds_alternative<PathSet>(payload) ? EntityType::PathSet : EntityType::Bitmap;
    }

    BoundingBox boundsLocal() const
    {
        if (auto ps = std::get_if<PathSet>(&payload))
        {
            ps->computeAABB();
            return ps->aabb;
        }
        const Bitmap &bm = std::get<Bitmap>(payload);
        return bm.aabb();
    }

    bool contains(const Vec2 &point, float margin_mm = 0) const
    {
        return boundsLocal().contains(localToPage / point, margin_mm);
    }

    const PathSet *pathset() const { return std::get_if<PathSet>(&payload); }
    PathSet *pathset() { return std::get_if<PathSet>(&payload); }
    const Bitmap *bitmap() const { return std::get_if<Bitmap>(&payload); }
    Bitmap *bitmap() { return std::get_if<Bitmap>(&payload); }

    // Filter chain: transforms from base payload to display/output layer
    FilterChain filterChain;

    // Helper to package current payload into a LayerPtr
    LayerPtr baseLayer() const
    {
        if (auto ps = std::get_if<PathSet>(&payload))
            return makeLayerFrom(*ps);
        const Bitmap &bm = std::get<Bitmap>(payload);
        return makeLayerFrom(bm);
    }

    void refreshFilterBase()
    {
        filterChain.setBase(baseLayer(), payloadVersion);
    }
};