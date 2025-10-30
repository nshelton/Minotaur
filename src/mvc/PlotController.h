#pragma once

#include "Model.h"
#include "PlotView.h"
#include "render/LineRenderer.h"
#include "render/A3PageRenderer.h"

class PlotController {
public:
    bool init();
    void update(double /*dt*/);
    void render();
    void shutdown();
    void setTransform(float tx, float ty, float scale);

    // Bind external renderers used by the view
    void bindRenderers(LineRenderer* lines, A3PageRenderer* pageRenderer);

    // API to add entities
    // Adds a path set shaped as a circle in mm coordinates
    void addCircle(float cx_mm, float cy_mm, float radius_mm, int segments = 64,
                   float r = 0.9f, float g = 0.2f, float b = 0.2f, float a = 1.0f);

private:
    PlotModel m_model{};
    PlotView m_view{};
    LineRenderer* m_lines{nullptr};
    A3PageRenderer* m_pageRenderer{nullptr};
};
