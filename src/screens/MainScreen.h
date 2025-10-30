#pragma once

#include "app/Screen.h"
#include "controllers/ViewportController.h"
#include "mvc/TriangleController.h"

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

private:
    App* m_app{nullptr};
    ViewportController m_viewport{};
    TriangleController m_triangle{};
};
