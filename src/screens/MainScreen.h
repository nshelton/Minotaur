#pragma once

#include "app/Screen.h"
#include <imgui.h>
#include "controllers/ViewportController.h"
#include "render/LineRenderer.h"
#include "render/A3PageRenderer.h"
#include "models/A3Page.h"
#include "mvc/PlotController.h"

class MainScreen : public IScreen {
public:
    void onAttach(App& app) override;
    void onResize(int width, int height) override;
    void onUpdate(double dt) override;
    void onRender() override;
    void onDetach() override;
    void onMouseButton(int button, int action, int mods, double x, double y) override;
    void onCursorPos(double x, double y) override;
    void onScroll(double xoffset, double yoffset, double x, double y) override;
    void onGui() override;

private:
    App* m_app{nullptr};
    ViewportController m_viewport{};
    LineRenderer m_lines{};
    A3PageRenderer m_pageRenderer{};
    A3Page m_page{A3Page::Portrait()};
    PlotController m_plot{};
    bool m_viewDragging{false};
};
