#include "MainScreen.h"
#include "app/App.h"

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
    m_triangle.render();
}

void MainScreen::onDetach() {
    m_triangle.shutdown();
}

