#include "MainScreen.h"
#include "app/App.h"
#include <GLFW/glfw3.h>

void MainScreen::onAttach(App& app) {
    m_app = &app;
    m_lines.init();
    m_lines.setLineWidth(1.5f);
    m_plot.init();
    m_plot.bindRenderers(&m_lines, &m_pageRenderer);
    m_plot.addCircle(297.0f/2.0f, 420.0f/2.0f, 50.0f, 96, 0.2f, 0.8f, 0.3f, 1.0f);
}

void MainScreen::onResize(int width, int height) {
    m_viewport.setSize(width, height);
    m_pageRenderer.setWindowSize(width, height);
}

void MainScreen::onUpdate(double /*dt*/) {}

void MainScreen::onRender() {
    // A3 page grid + outline using line renderer with aspect-correct mapping
    m_lines.clear();
    m_lines.setTransform(m_viewport.translateX(), m_viewport.translateY(), m_viewport.scale());
    // 1 cm grid (10 mm)
    m_pageRenderer.addGrid(m_page, m_lines, 10.0f, 0.35f, 0.35f, 0.4f, 0.35f);
    m_pageRenderer.addOutline(m_page, m_lines, 1.0f, 1.0f, 1.0f, 1.0f);
    m_lines.draw();
}

void MainScreen::onDetach() {
    m_lines.shutdown();
}

void MainScreen::onMouseButton(int button, int action, int /*mods*/, double x, double y) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            m_viewport.beginDrag(x, y);
        } else if (action == GLFW_RELEASE) {
            m_viewport.endDrag();
        }
    }
}

void MainScreen::onCursorPos(double x, double y) {
    m_viewport.onCursor(x, y);
}

void MainScreen::onScroll(double xoffset, double yoffset, double x, double y) {
    (void)xoffset;
    m_viewport.onScroll(xoffset, yoffset, x, y);
}



