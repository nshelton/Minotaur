#pragma once

#include <glad/glad.h>
#include <core/core.h>
#include <cmath>

class Camera
{
public:

    Camera();

    void setSize(int width, int height);
    void reset();
    void move(Vec2 delta);
    Vec2 getSize() const
    {
        return Vec2(m_width, m_height);
    }

    Vec2 screenToWorld(Vec2 screen_px) const
    {
        Vec2 ndc = Vec2(
            (screen_px.x / static_cast<float>(m_width)) * 2.0f - 1.0f,
            1.0f - (screen_px.y / static_cast<float>(m_height)) * 2.0f);
        return m_viewTransform.applyInverse(ndc);
    }

    void setCenter(Vec2 center)
    {
        m_left = center.x - m_aspect * m_zoom;
        m_right = center.x + m_aspect * m_zoom;
        m_bottom = center.y - m_zoom;
        m_top = center.y + m_zoom;
        m_viewTransform.setOrtho(m_left, m_right, m_bottom, m_top);
    }

    Vec2 center() const
    {
        return Vec2(
            (m_left + m_right) * 0.5f,
            (m_bottom + m_top) * 0.5f);
    }

    // Current transform from mm to NDC
    Mat3 Transform() const { return m_viewTransform; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    void zoomAtPixel(const Vec2 &px, float wheelSteps);

private:
    static float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    int m_width{0};
    int m_height{0};

    bool m_dragging{false};
    Vec2 m_last;

    // page (mm) to ndc transform
    Mat3 m_viewTransform;

    // zoom is the vertical half-size in mm
    float m_zoom;

    float m_left;
    float m_right;
    float m_top;
    float m_bottom;
    float m_aspect;

    float m_minZoom{0.1f};
    float m_maxZoom{10.0f};
};
