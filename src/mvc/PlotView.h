#pragma once

#include "Model.h"
#include "render/LineRenderer.h"
#include "render/A3PageRenderer.h"

class PlotView {
public:
    bool init() { return true; }
    void shutdown() {}

    // Emits line geometry for all path sets into the given LineRenderer
    void render(const PlotModel& model, LineRenderer& lines, const A3PageRenderer& pageRenderer);
};
