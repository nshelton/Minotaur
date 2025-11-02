#pragma once

#include <string>
#include "core/Pathset.h"

struct Entity
{
    int id;
    std::string name;
    PathSet pathset;

    bool contains(const Vec2 &point) const
    {
       pathset.computeAABB();
       return pathset.aabb.contains(point);
    }
};