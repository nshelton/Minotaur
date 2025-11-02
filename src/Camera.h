#pragma once

#include <glad/glad.h>
#include <core/core.h>
#include <cmath>

class Camera {
public:
    void setSize(int width, int height);
    void reset();

    // Dragging
    void beginDrag(Vec2 pos);
    void endDrag();
    void onCursor(Vec2 pos);

    // Mouse wheel zoom at cursor position
    void onScroll(double xoffset, double yoffset, Vec2 pos);

    // Current transform from mm to NDC
    Mat3 Transform() const { return m_viewTransform; }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    static float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    int m_width{0};
    int m_height{0};

    bool m_dragging{false};
    Vec2 m_last;

    // page to ndc transform
    Mat3 m_viewTransform;

    float m_zoom = 500;;

    float m_left;
    float m_right;
    float m_top;
    float m_bottom;
    float m_aspect;

    float m_minZoom{0.1f};
    float m_maxZoom{10.0f};
};
