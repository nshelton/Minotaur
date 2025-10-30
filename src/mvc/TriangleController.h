#pragma once

#include "Model.h"
#include "TriangleView.h"

class TriangleController {
public:
    bool init();
    void update(double /*dt*/);
    void render();
    void shutdown();
    void setTransform(float tx, float ty, float scale);

private:
    TriangleModel m_model{};
    TriangleView m_view{};
};
