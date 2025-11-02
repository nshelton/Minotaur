#pragma once

#include <string>
#include "core/Pathset.h"

struct Entity
{
    int id;
    std::string name;
    PathSet pathset;

    // takes a point from local entity space (mm) to page space (mm)
    Mat3 localToPage;

    bool contains(const Vec2 &point) const
    {
       pathset.computeAABB();
       return pathset.aabb.contains(localToPage / point);
    }
};