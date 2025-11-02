#include "MainScreen.h"
#include "app/App.h"
#include <GLFW/glfw3.h>
#include "utils/PathSetGenerator.h"
#include <iostream>
#include <glog/logging.h>

void MainScreen::onAttach(App &app)
{
    google::InitGoogleLogging("Minotaur");
    m_app = &app;
    m_page.addPathSet(
        PathSetGenerator::Circle(
            Vec2(297.0f / 2.0f, 420.0f / 2.0f),
            50.0f,
            96,
            Color(0.2f, 0.8f, 0.3f, 1.0f)));
}

void MainScreen::onResize(int width, int height)
{
    m_renderer.setSize(width, height);
    m_camera.setSize(width, height);
}

void MainScreen::onUpdate(double /*dt*/)
{
}

void MainScreen::onRender()
{
    m_renderer.render(m_camera, m_page, m_interaction.state());
}

void MainScreen::onDetach()
{
    m_renderer.shutdown();
}

void MainScreen::onMouseButton(int button, int action, int /*mods*/, Vec2 px)
{
    m_page.mouse_page_mm = m_camera.screenToWorld(px);
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        m_interaction.onMouseDown(m_page, m_camera, m_page.mouse_page_mm);
        LOG(INFO) << "MouseDown at pixel (" << px.x << ", " << px.y << ")";
    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
    {
        m_interaction.onMouseUp();
    }
}

void MainScreen::onCursorPos(Vec2 px)
{
    m_page.mouse_pixel = px;
    m_page.mouse_page_mm = m_camera.screenToWorld(px);
    m_interaction.onCursorPos(m_page, m_camera, m_page.mouse_page_mm);
}

void MainScreen::onScroll(double xoffset, double yoffset, Vec2 px)
{
    m_interaction.onScroll(m_page, m_camera, static_cast<float>(yoffset), px);
}
