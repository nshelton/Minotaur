#include "TriangleController.h"

bool TriangleController::init() {
    return m_view.init();
}

void TriangleController::update(double /*dt*/) {
    // Update model state if needed
}

void TriangleController::render() {
    m_view.render();
}

void TriangleController::shutdown() {
    m_view.shutdown();
}

