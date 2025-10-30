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
};

