#pragma once

#include <vector>
#include "core/core.h"


struct Path {
    std::vector<Vec2> points;
    bool closed{false};
};

struct PathSet {
    Transform2D transform{}; // page-space transform (mm)
    std::vector<Path> paths;
    Color color {1.0f, 1.0f, 1.0f, 1.0f};
};

struct PlotModel {
    std::vector<PathSet> entities;
};

