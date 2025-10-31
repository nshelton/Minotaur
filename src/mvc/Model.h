#pragma once

#include <vector>
#include "core/core.h"

struct PlotModel {
    Vec2 mousePos;
    Vec2 mouseMm;
    std::vector<PathSet> entities;
};

