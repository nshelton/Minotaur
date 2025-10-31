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
        m_offset = Vec2(0.0f, 0.0f);
        m_zoom = 1.0f;
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

        float ndcDx = static_cast<float>(2.0 * d.x / m_width);
        float ndcDy = static_cast<float>(-2.0 * d.y / m_height);
        // Adjust by inverse zoom so drag feels consistent at all scales
        m_offset = m_offset + Vec2(ndcDx / m_zoom, ndcDy / m_zoom);
    }

    // Mouse wheel zoom at cursor position
    void onScroll(double xoffset, double yoffset, Vec2 pos) {
        (void)xoffset; // not used for now
        if (m_width == 0 || m_height == 0) return;

        float oldZoom = m_zoom;
        float factor = std::pow(1.1f, static_cast<float>(yoffset));
        m_zoom = clamp(oldZoom * factor, m_minZoom, m_maxZoom);
        if (m_zoom == oldZoom) return;

        // Keep the point under the cursor fixed in NDC
        Vec2 c(static_cast<float>(2.0 * pos.x / m_width - 1.0),
               static_cast<float>(1.0 - 2.0 * pos.y / m_height));

        m_offset = m_offset + c * (1.0f / m_zoom - 1.0f / oldZoom);
    }

    // Current transform in NDC space: (p + offset) * zoom
    float translateX() const { return m_offset.x; }
    float translateY() const { return m_offset.y; }
    float scale() const { return m_zoom; }

    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    static float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    int m_width{0};
    int m_height{0};

    bool m_dragging{false};
    Vec2 m_last;
    Vec2 m_offset;
    float m_zoom{1.0f};
    float m_minZoom{0.1f};
    float m_maxZoom{10.0f};
};
