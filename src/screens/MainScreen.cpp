#include "MainScreen.h"
#include "app/App.h"
#include <GLFW/glfw3.h>
#include "generators/pathset/CircleGenerator.h"
#include "generators/mesh/MeshGenerator.h"
#include "generators/pathset/SvgGenerator.h"
#include "utils/ImageLoader.h"
#include "utils/Clipboard.h"
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
    serialization::RenderSettings rs;
    if (!serialization::loadProject(m_page, m_camera, rs, m_plotter.config(), m_projectPath, &err))
    {
        if (!err.empty())
        {
            LOG(WARNING) << "Failed to load page.json: " << err;
        }
        // Fallback demo content
        m_page.addGeneratedEntity(
            std::make_unique<CircleGenerator>(),
            Vec2(297.0f / 2.0f, 420.0f / 2.0f));
    }
    else
    {
        m_renderer.setLineWidth(rs.lineWidth);
        m_renderer.setNodeDiameterPx(rs.nodeDiameter);
        // Sync AxiDraw state from loaded plotter config
        m_plotter.syncStateFromConfig();
    }
}

void MainScreen::onResize(int width, int height)
{
    m_camera.setSize(width, height);
}

void MainScreen::onUpdate(double /*dt*/)
{
    // Tick all generators so parametric entities stay up to date.
    // Two passes: first kick stale generators, then collect any async results.
    for (auto &[id, e] : m_page.entities)
        e.tickGenerator();
    for (auto &[id, e] : m_page.entities)
        e.pollAsyncGenerator();

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

    // Auto-connect plotter when not connected
    m_plotter.pollAutoConnect();

    // Turn off plot progress visualization when job completes
    m_plotter.updatePlotProgress();
}

void MainScreen::onRender()
{
    m_renderer.beginFrame(m_camera, m_page, m_interaction.state());

    // Render plot progress overlay with 3-color visualization
    if (m_plotter.showPlotProgress() && m_plotter.isRunning())
    {
        const auto &paths = m_plotter.getOrderedPaths();
        auto s = m_plotter.stats();
        const int sent = s.sentPathIndex;
        const int queued = s.queuedPathIndex;

        static const Color kDone{0.5f, 0.5f, 0.5f, 0.3f};    // gray
        static const Color kQueued{0.2f, 0.4f, 1.0f, 0.6f};   // blue
        static const Color kPending{1.0f, 0.2f, 0.8f, 0.6f};  // magenta

        for (size_t pi = 0; pi < paths.size(); ++pi)
        {
            const auto &path = paths[pi];
            if (path.points.size() < 2) continue;

            const Color &col = (static_cast<int>(pi) < sent)  ? kDone
                             : (static_cast<int>(pi) < queued) ? kQueued
                             :                                    kPending;

            for (size_t i = 1; i < path.points.size(); ++i)
            {
                m_renderer.addLine(path.points[i - 1], path.points[i], col);
            }
            if (path.closed && path.points.size() > 2)
            {
                m_renderer.addLine(path.points.back(), path.points.front(), col);
            }
        }
    }

    m_renderer.endFrame(m_camera);
}

void MainScreen::onDetach()
{
    m_renderer.shutdown();
    std::string err;
    // Keep pen positions in sync with the last known AxiDraw state
    m_plotter.syncConfigFromState();
    serialization::RenderSettings rs{m_renderer.lineWidth(), m_renderer.nodeDiameterPx()};
    if (!serialization::saveProject(m_page, m_camera, rs, m_plotter.config(), m_projectPath, &err))
    {
        LOG(ERROR) << "Failed to save page.json: " << err;
    }
}

void MainScreen::onFilesDropped(const std::vector<std::string>& paths)
{
    for (const auto &p : paths)
    {
        // Check for .obj mesh files first
        {
            size_t dot = p.rfind('.');
            if (dot != std::string::npos)
            {
                std::string ext = p.substr(dot);
                // Case-insensitive .obj check
                if (ext == ".obj" || ext == ".OBJ" || ext == ".Obj")
                {
                    Vec2 center(m_page.page_width_mm * 0.5f, m_page.page_height_mm * 0.5f);
                    auto gen = std::make_unique<MeshGenerator>(p);
                    if (gen->hasMesh())
                    {
                        m_page.addGeneratedEntity(std::move(gen), center);
                        LOG(INFO) << "Loaded OBJ mesh: " << p;
                        continue;
                    }
                    else
                    {
                        LOG(WARNING) << "Failed to load OBJ: " << p;
                        continue;
                    }
                }
                if (ext == ".svg" || ext == ".SVG" || ext == ".Svg")
                {
                    Vec2 center(m_page.page_width_mm * 0.5f, m_page.page_height_mm * 0.5f);
                    auto gen = std::make_unique<SvgGenerator>(p);
                    if (gen->hasSvg())
                    {
                        m_page.addGeneratedEntity(std::move(gen), center);
                        LOG(INFO) << "Loaded SVG: " << p;
                        continue;
                    }
                    else
                    {
                        LOG(WARNING) << "Failed to load SVG: " << p;
                        continue;
                    }
                }
            }
        }

        // Try PGM first for minimal dependency
        Bitmap bm;
        std::string err;
        // Try loading as color image first
        ColorImage ci;
        if (ImageLoader::loadColorImage(p, ci, &err, 0.5f))
        {
            m_page.addEntity(ci);
            LOG(INFO) << "Loaded image and added as color image: " << p;
        }
        else if (ImageLoader::loadPGM(p, bm, &err, 0.5f))
        {
            m_page.addEntity(bm);
            LOG(INFO) << "Loaded PGM and added as bitmap: " << p;
        }
        else if (ImageLoader::loadImage(p, bm, &err, 0.5f))
        {
            m_page.addEntity(bm);
            LOG(INFO) << "Loaded image and added as bitmap: " << p;
        }
        else
        {
            LOG(WARNING) << "Unsupported image or failed to load ('" << p << "'): " << err;
        }
    }
}

void MainScreen::onClipboardPaste()
{
    auto data = Clipboard::getImageData();
    if (data.empty())
    {
        LOG(INFO) << "Clipboard paste: no image data found";
        return;
    }

    ColorImage ci;
    std::string err;
    if (ImageLoader::loadColorImageFromMemory(data.data(), data.size(), ci, &err, 0.5f))
    {
        m_page.addEntity(ci);
        LOG(INFO) << "Pasted clipboard image as color image (" << ci.width_px << "x" << ci.height_px << ")";
    }
    else
    {
        LOG(WARNING) << "Failed to decode clipboard image: " << err;
    }
}

void MainScreen::onMouseButton(int button, int action, int /*mods*/, Vec2 px)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        m_interaction.onMouseDown(m_page, m_camera, px);
    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
    {
        m_interaction.onMouseUp();
    }
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS)
    {
        // Middle click always pans the page; never selects/moves entities
        m_interaction.beginPan(m_camera, px);
    }
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_RELEASE)
    {
        m_interaction.onMouseUp();
    }
}

void MainScreen::onCursorPos(Vec2 px)
{
    m_interaction.onCursorPos(m_page, m_camera, px);
}

void MainScreen::onScroll(double xoffset, double yoffset, Vec2 px)
{
    m_interaction.onScroll(m_page, m_camera, static_cast<float>(yoffset), px);
}
