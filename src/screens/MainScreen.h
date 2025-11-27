#pragma once

#include "app/Screen.h"
#include <imgui.h>
#include "Camera.h"
#include "Renderer.h"
#include <memory>
#include "serial/SerialController.h"
#include "plotters/AxidrawController.h"
#include "plotters/PlotSpooler.h"
#include "plotters/PlotterConfig.h"

class MainScreen : public IScreen
{
public:
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

private:
    App *m_app{nullptr};
    Camera m_camera{};
    Renderer m_renderer{};
    InteractionController m_interaction{};
    PageModel m_page{};

    // Plotter/Serial
    SerialController m_serial{};
    AxiDrawState m_axState{};
    std::unique_ptr<AxiDrawController> m_ax{};
    std::unique_ptr<PlotSpooler> m_spooler{};
    PlotterConfig m_plotter{};
    char m_portBuf[64] = "";

    // Plot progress visualization
    bool m_showPlotProgress{false};
};
