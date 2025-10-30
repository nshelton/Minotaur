#include <cmath>
#include "PlotController.h"

bool PlotController::init() {
    return m_view.init();
}

void PlotController::update(double /*dt*/) {
    // Update model state if needed
}

void PlotController::render() {
    if (m_lines && m_pageRenderer) {
        m_view.render(m_model, *m_lines, *m_pageRenderer);
    }
}

void PlotController::shutdown() {
    m_view.shutdown();
}

void PlotController::setTransform(float tx, float ty, float scale) {
    if (m_lines) m_lines->setTransform(tx, ty, scale);
}

void PlotController::bindRenderers(LineRenderer* lines, A3PageRenderer* pageRenderer) {
    m_lines = lines;
    m_pageRenderer = pageRenderer;
}

void PlotController::addCircle(float cx_mm, float cy_mm, float radius_mm, int segments,
                               float r, float g, float b, float a) {
    if (segments < 3) segments = 3;
    PathSet ps{};
    ps.transform = Transform2D{}; // identity in page space
    ps.r = r; ps.g = g; ps.b = b; ps.a = a;
    Path path{}; path.closed = true;
    const float twoPi = 6.28318530718f;
    for (int i = 0; i < segments; ++i) {
        float t = (float)i / (float)segments;
        float ang = t * twoPi;
        float x = cx_mm + radius_mm * cosf(ang);
        float y = cy_mm + radius_mm * sinf(ang);
        path.points.push_back(Vec2{x, y});
    }
    ps.paths.push_back(std::move(path));
    m_model.entities.push_back(std::move(ps));
}
