#pragma once
#include <vector>
#include "core/Vec2.h"

struct Path {
    std::vector<Vec2> points;
    bool closed{false};
};


struct BoundingBox {
    Vec2 min;
    Vec2 max;

    BoundingBox() : min{0.0f, 0.0f}, max{0.0f, 0.0f} {}
    BoundingBox(const Vec2& min, const Vec2& max) : min(min), max(max) {}
    void expandToInclude(const Vec2& point) {
        if (point.x < min.x) min.x = point.x;
        if (point.y < min.y) min.y = point.y;
        if (point.x > max.x) max.x = point.x;
        if (point.y > max.y) max.y = point.y;
    }

    bool contains(const Vec2& point, float margin_mm=0) const {
        return (point.x >= min.x - margin_mm && point.x <= max.x + margin_mm &&
                point.y >= min.y - margin_mm && point.y <= max.y + margin_mm);
    }
};

struct PathSet {
    std::vector<Path> paths;
    Color color {1.0f, 1.0f, 1.0f, 1.0f};
    mutable BoundingBox aabb;

    PathSet() = default;

    /// @brief Compute the axis-aligned bounding box of the pathset, in local space
    void computeAABB() const  {
        aabb.min = Vec2(0,0);
        aabb.max = Vec2(0,0);
        bool first = true;
        for (const auto& path : paths) {
            for (const auto& p : path.points) {
                if (first) {
                    aabb.min = p;
                    aabb.max = p;
                    first = false;
                } else {
                    aabb.expandToInclude(p);
                }
            }
        }
    }
};