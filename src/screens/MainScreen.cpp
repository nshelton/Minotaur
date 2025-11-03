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
    // Seed plotter config from current AxiDraw state before attempting to load
    m_plotter.penUpPos = m_axState.penUpPos;
    m_plotter.penDownPos = m_axState.penDownPos;
    if (!serialization::loadProject(m_page, m_camera, m_renderer, m_plotter, "page.json", &err))
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
        // Sync AxiDraw state from loaded plotter config
        m_axState.penUpPos = m_plotter.penUpPos;
        m_axState.penDownPos = m_plotter.penDownPos;
    }
}

void MainScreen::onResize(int width, int height)
{
    m_renderer.setSize(width, height);
    m_camera.setSize(width, height);
}

void MainScreen::onUpdate(double /*dt*/)
{
    // Handle keyboard-driven actions that should work outside of ImGui widgets
    ImGuiIO &io = ImGui::GetIO();
    if (!io.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        if (m_interaction.SelectedEntity())
        {
            int id = *m_interaction.SelectedEntity();
            auto it = m_page.entities.find(id);
            if (it != m_page.entities.end())
            {
                m_page.entities.erase(it);
                // Clear selection/hover if they referenced the deleted entity
                m_interaction.DeselectEntity();
                if (m_interaction.HoveredEntity() && *m_interaction.HoveredEntity() == id)
                {
                    m_interaction.ClearHover();
                }
                LOG(INFO) << "Deleted entity via Delete key: " << id;
            }
        }
    }
}

void MainScreen::onRender()
{
    m_renderer.render(m_camera, m_page, m_interaction.state());
}

void MainScreen::onDetach()
{
    m_renderer.shutdown();
    std::string err;
    // Persist the entire plotter configuration
    // Keep pen positions in sync with the last known AxiDraw state
    m_plotter.penUpPos = m_axState.penUpPos;
    m_plotter.penDownPos = m_axState.penDownPos;
    if (!serialization::saveProject(m_page, m_camera, m_renderer, m_plotter, "page.json", &err))
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
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS)
    {
        // Middle click always pans the page; never selects/moves entities
        m_interaction.beginPan(m_camera, m_page.mouse_page_mm);
    }
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_RELEASE)
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
