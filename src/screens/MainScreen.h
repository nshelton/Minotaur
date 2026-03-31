#pragma once

#include "app/Screen.h"
#include <imgui.h>
#include "Camera.h"
#include "Renderer.h"
#include <map>
#include <memory>
#include <string>
#include "plotters/PlotterManager.h"

class MainScreen : public IScreen
{
public:
    explicit MainScreen(const std::string &projectPath = "page.json")
        : m_projectPath(projectPath) {}

    void onAttach(App &app) override;
    void onResize(int width, int height) override;
    void onUpdate(double dt) override;
    void onRender() override;
    void onDetach() override;
    void onMouseButton(int button, int action, int mods, Vec2 px) override;
    void onCursorPos(Vec2 px) override;
    void onScroll(double xoffset, double yoffset, Vec2 px) override;
    void onGui() override;
    void onFilesDropped(const std::vector<std::string>& paths) override;
    void onClipboardPaste() override;

private:
    App *m_app{nullptr};
    Camera m_camera{};
    Renderer m_renderer{};
    InteractionController m_interaction{};
    PageModel m_page{};

    // Plotter
    PlotterManager m_plotter{};

    // Project file path
    std::string m_projectPath;

    // Per-entity string parameter input buffers for the generator UI panel.
    // Keyed by "<entityId>:<paramKey>".
    std::map<std::string, std::string> m_genStringBufs;
};
