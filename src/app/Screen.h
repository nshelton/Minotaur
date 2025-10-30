#pragma once

class App;

class IScreen {
public:
    virtual ~IScreen() = default;
    virtual void onAttach(App& app) = 0;
    virtual void onResize(int width, int height) = 0;
    virtual void onUpdate(double dt) = 0;
    virtual void onRender() = 0;
    virtual void onDetach() {}

    // Input (optional overrides)
    virtual void onMouseButton(int /*button*/, int /*action*/, int /*mods*/, double /*x*/, double /*y*/) {}
    virtual void onCursorPos(double /*x*/, double /*y*/) {}
    virtual void onScroll(double /*xoffset*/, double /*yoffset*/, double /*x*/, double /*y*/) {}

    // UI (ImGui) hook
    virtual void onGui() {}
};
