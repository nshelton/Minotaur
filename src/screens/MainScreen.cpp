#include "MainScreen.h"
#include "app/App.h"
#include <GLFW/glfw3.h>
#include "utils/PathSetGenerator.h"
#include "utils/ImageLoader.h"
#include <iostream>
#include <glog/logging.h>
#include "utils/Serialization.h"
#include "plotters/PlotterConfig.h"

void MainScreen::onAttach(App &app)
{
    google::InitGoogleLogging("Minotaur");
    google::SetStderrLogging(google::GLOG_INFO);

    m_app = &app;
    std::string err;
    PlotterConfig plotterCfg{};
    plotterCfg.penUpPos = m_axState.penUpPos;
    plotterCfg.penDownPos = m_axState.penDownPos;
    if (!serialization::loadProject(m_page, m_camera, m_renderer, plotterCfg, "page.json", &err))
    {
        if (!err.empty())
        {
            LOG(WARNING) << "Failed to load page.json: " << err;
        }
        // Fallback demo content
        m_page.addPathSet(
            PathSetGenerator::Circle(
                Vec2(297.0f / 2.0f, 420.0f / 2.0f),
                50.0f,
                96,
                Color(0.2f, 0.8f, 0.3f, 1.0f)));
    }
    else
    {
        m_axState.penUpPos = plotterCfg.penUpPos;
        m_axState.penDownPos = plotterCfg.penDownPos;
    }
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
    std::string err;
    PlotterConfig plotterCfg{};
    plotterCfg.penUpPos = m_axState.penUpPos;
    plotterCfg.penDownPos = m_axState.penDownPos;
    if (!serialization::saveProject(m_page, m_camera, m_renderer, plotterCfg, "page.json", &err))
    {
        LOG(ERROR) << "Failed to save page.json: " << err;
    }
}

void MainScreen::onFilesDropped(const std::vector<std::string>& paths)
{
    for (const auto &p : paths)
    {
        // Try PGM first for minimal dependency
        Bitmap bm;
        std::string err;
        if (ImageLoader::loadPGM(p, bm, &err, 0.5f))
        {
            m_page.addBitmap(bm);
            LOG(INFO) << "Loaded PGM and added as bitmap: " << p;
        }
        else
        {
            // Try WIC (PNG, JPG, etc.)
            if (ImageLoader::loadImage(p, bm, &err, 0.5f))
            {
                m_page.addBitmap(bm);
                LOG(INFO) << "Loaded image and added as bitmap: " << p;
            }
            else
            {
                LOG(WARNING) << "Unsupported image or failed to load ('" << p << "'): " << err;
            }
        }
    }
}

void MainScreen::onMouseButton(int button, int action, int /*mods*/, Vec2 px)
{
    LOG(INFO) << "MouseDown at pixel (" << px.x << ", " << px.y << ")";

    m_page.mouse_page_mm = m_camera.screenToWorld(px);
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        m_interaction.onMouseDown(m_page, m_camera, m_page.mouse_page_mm);
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
