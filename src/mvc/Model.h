#pragma once

#include <vector>
#include "core/core.h"

struct PlotModel {
    Vec2 mouse_pixel;
    Vec2 mouse_page_mm;
    std::vector<PathSet> entities;
};

