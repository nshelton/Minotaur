#pragma once

#include <glad/glad.h>
#include <cmath>

class ViewportController {
public:
    void setSize(int width, int height) {
        m_width = width;
        m_height = height;
        glViewport(0, 0, width, height);
    }

    void reset() {
        m_viewTransform = Transform2D();
        m_dragging = false;
    }

    // Dragging
    void beginDrag(Vec2 pos) {
        m_dragging = true;
        m_last = pos;
    }
    void endDrag() { m_dragging = false; }
    void onCursor(Vec2 pos) {
        if (!m_dragging || m_width == 0 || m_height == 0) return;
        
        Vec2 d = pos - m_last;
        m_last = pos;

        Vec2 ndcd = Vec2(2, -2) * d / Vec2(m_width, m_height);

        m_viewTransform.pos += ndcd / m_viewTransform.scale;
    }

    // Mouse wheel zoom at cursor position
    void onScroll(double xoffset, double yoffset, Vec2 pos) {
        (void)xoffset; // not used for now
        if (m_width == 0 || m_height == 0) return;

        float oldZoom = m_viewTransform.scale;
        float factor = std::pow(1.1f, static_cast<float>(yoffset));
        m_viewTransform.scale = clamp(oldZoom * factor, m_minZoom, m_maxZoom);
        if (m_viewTransform.scale == oldZoom) return;

        // Keep the point under the cursor fixed in NDC
        Vec2 c(static_cast<float>(2.0 * pos.x / m_width - 1.0),
               static_cast<float>(1.0 - 2.0 * pos.y / m_height));

        m_viewTransform.pos +=  c * (1.0f / m_viewTransform.scale - 1.0f / oldZoom);
    }

    // Current transform from mm to NDC
    Transform2D Transform() const { return m_viewTransform; }

    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    static float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    int m_width{0};
    int m_height{0};

    bool m_dragging{false};
    Vec2 m_last;

    // page to ndc transform
    Transform2D m_viewTransform;
 
    float m_minZoom{0.1f};
    float m_maxZoom{10.0f};
};
