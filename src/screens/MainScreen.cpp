#include "MainScreen.h"
#include "app/App.h"
#include <GLFW/glfw3.h>
#include "mvc/PathSetGenerator.h"

void MainScreen::onAttach(App& app) {
    m_app = &app;
    m_lines.init();
    m_lines.setLineWidth(1.5f);
    m_plot.init();
    m_plot.bindRenderers(&m_lines, &m_pageRenderer);
    m_plot.addPathSet(PathSetGenerator::Circle(Vec2(297.0f/2.0f, 420.0f/2.0f), 50.0f, 96, Color(0.2f, 0.8f, 0.3f, 1.0f)));
}

void MainScreen::onResize(int width, int height) {
    m_viewport.setSize(width, height);
    m_pageRenderer.setWindowSize(width, height);
}

void MainScreen::onUpdate(double /*dt*/) {}

void MainScreen::onRender() {
    // A3 page grid + outline using line renderer with aspect-correct mapping
    m_lines.clear();
    m_plot.setTransform( Vec2(m_viewport.translateX(), m_viewport.translateY()),  m_viewport.scale());
    // 1 cm grid (10 mm)
    m_pageRenderer.addGrid(m_page, m_lines, 10.0f, Color(0.35f, 0.35f, 0.4f, 0.35f));
    m_pageRenderer.addOutline(m_page, m_lines, Color(1.0f, 1.0f, 1.0f, 1.0f));
    // Render plot entities onto the same line buffer
    m_plot.render(m_page);
    // Overlays: selection bounds and handles
    m_plot.renderOverlays(m_page);
    m_lines.draw();
}

void MainScreen::onDetach() {
    m_lines.shutdown();
}

void MainScreen::onMouseButton(int button, int action, int /*mods*/, Vec2 px) {
    // Give plot interactions first dibs
    if (m_plot.onMouseButton(m_page, button, action, 0, px)) {
        return;
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            m_viewport.beginDrag(px);
            m_viewDragging = true;
        } else if (action == GLFW_RELEASE) {
            if (m_viewDragging) {
                m_viewport.endDrag();
                m_viewDragging = false;
            }
        }
    }
}

void MainScreen::onCursorPos(Vec2 px) {
    if (!m_plot.onCursorPos(m_page, px)) {
        m_viewport.onCursor(px);
    }
}

void MainScreen::onScroll(double xoffset, double yoffset, Vec2 px) {
    (void)xoffset;
    m_viewport.onScroll(xoffset, yoffset, px);
}



