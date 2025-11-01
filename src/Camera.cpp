

#include "Camera.h"

void Camera::setSize(int width, int height) 
{
    m_width = width;
    m_height = height;


    m_viewTransform = Transform2D();
    m_dragging = false;
    m_viewTransform.pos = Vec2{-1.0f, 1.0f};
    m_viewTransform.scale = 2.0f / std::max(width, height);
}

void Camera::reset() {
    m_viewTransform = Transform2D();
    m_dragging = false;
}

void Camera::beginDrag(Vec2 pos) {
    m_dragging = true;
    m_last = pos;
}

void Camera::endDrag() { m_dragging = false; }

void Camera::onCursor(Vec2 pos) {
    if (!m_dragging || m_width == 0 || m_height == 0) return;
    
    Vec2 d = pos - m_last;
    m_last = pos;

    Vec2 ndcd = Vec2(2, -2) * d / Vec2(m_width, m_height);

    m_viewTransform.pos += ndcd / m_viewTransform.scale;
}

void Camera::onScroll(double xoffset, double yoffset, Vec2 pos) {
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