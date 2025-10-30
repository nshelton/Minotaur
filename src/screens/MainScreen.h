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

private:
    App* m_app{nullptr};
    ViewportController m_viewport{};
    TriangleController m_triangle{};
};

