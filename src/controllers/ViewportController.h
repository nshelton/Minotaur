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

    // Dragging
    void beginDrag(double x, double y) {
        m_dragging = true;
        m_lastX = x; m_lastY = y;
    }
    void endDrag() { m_dragging = false; }
    void onCursor(double x, double y) {
        if (!m_dragging || m_width == 0 || m_height == 0) return;
        double dx = x - m_lastX;
        double dy = y - m_lastY;
        m_lastX = x; m_lastY = y;
        float ndcDx = static_cast<float>(2.0 * dx / m_width);
        float ndcDy = static_cast<float>(-2.0 * dy / m_height);
        // Adjust by inverse zoom so drag feels consistent at all scales
        m_offsetX += ndcDx / m_zoom;
        m_offsetY += ndcDy / m_zoom;
    }

    // Mouse wheel zoom at cursor position
    void onScroll(double xoffset, double yoffset, double x, double y) {
        (void)xoffset; // not used for now
        if (m_width == 0 || m_height == 0) return;

        float oldZoom = m_zoom;
        float factor = std::pow(1.1f, static_cast<float>(yoffset));
        m_zoom = clamp(oldZoom * factor, m_minZoom, m_maxZoom);
        if (m_zoom == oldZoom) return;

        // Keep the point under the cursor fixed in NDC
        float cx = static_cast<float>(2.0 * x / m_width - 1.0);
        float cy = static_cast<float>(1.0 - 2.0 * y / m_height);
        m_offsetX = m_offsetX + cx * (1.0f / m_zoom - 1.0f / oldZoom);
        m_offsetY = m_offsetY + cy * (1.0f / m_zoom - 1.0f / oldZoom);
    }

    // Current transform in NDC space: (p + offset) * zoom
    float translateX() const { return m_offsetX; }
    float translateY() const { return m_offsetY; }
    float scale() const { return m_zoom; }

    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    static float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    int m_width{0};
    int m_height{0};

    bool m_dragging{false};
    double m_lastX{0.0}, m_lastY{0.0};

    float m_offsetX{0.0f};
    float m_offsetY{0.0f};
    float m_zoom{1.0f};
    float m_minZoom{0.1f};
    float m_maxZoom{10.0f};
};

