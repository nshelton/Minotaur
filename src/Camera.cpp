

#include "Camera.h"

void Camera::setSize(int width, int height) 
{
    m_width = width;
    m_height = height;
    m_aspect = static_cast<float>(width) / static_cast<float>(height);

    // initial 500mm half-width view
    m_zoom = 500;
    m_left = -m_aspect * m_zoom;
    m_right = m_aspect * m_zoom;
    m_bottom = -m_zoom;
    m_top = m_zoom;

    m_viewTransform = Mat3();
    m_dragging = false;

    m_viewTransform.setOrtho(m_left, m_right, m_bottom, m_top);

}

void Camera::reset() {
    m_viewTransform = Mat3();
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

    m_viewTransform.SetTranslation(m_viewTransform.translation() + ndcd / m_viewTransform.scale().x);
}

void Camera::onScroll(double xoffset, double yoffset, Vec2 pos) {
    (void)xoffset; // not used for now
    if (m_width == 0 || m_height == 0) return;

    Vec2 screenSize(static_cast<float>(m_width), static_cast<float>(m_height));
    Vec2 normalized = pos / screenSize;
    Vec2 ndcPos(normalized.x * 2.0f - 1.0f, 1.0f - normalized.y * 2.0f);
    Vec2 worldPoint = m_viewTransform.applyInverse(ndcPos);

    Vec2 oldZoom = m_viewTransform.scale();
    float factor = std::pow(1.1f, static_cast<float>(yoffset));
    Vec2 newZoom = oldZoom * factor;
    m_viewTransform.SetScale(newZoom);
    Vec2 newTranslation = ndcPos - (worldPoint * newZoom);
    m_viewTransform.SetTranslation(newTranslation);
}
