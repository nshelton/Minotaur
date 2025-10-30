#include "MainScreen.h"
#include "app/App.h"
#include <GLFW/glfw3.h>

void MainScreen::onAttach(App& app) {
    m_app = &app;
    m_triangle.init();
}

void MainScreen::onResize(int width, int height) {
    m_viewport.setSize(width, height);
}

void MainScreen::onUpdate(double /*dt*/) {
    m_triangle.update(0.0);
}

void MainScreen::onRender() {
    m_triangle.setTransform(m_viewport.translateX(), m_viewport.translateY(), m_viewport.scale());
    m_triangle.render();
}

void MainScreen::onDetach() {
    m_triangle.shutdown();
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

