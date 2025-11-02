#include "Camera.h"
#include <glog/logging.h>

Camera::Camera()
{
    m_zoom = 200.0f; // initial zoom
    m_aspect = 4.0f / 3.0f; // default aspect
    reset();
}

void Camera::setSize(int width, int height) 
{
    m_width = width;
    m_height = height;
    m_aspect = static_cast<float>(width) / static_cast<float>(height);

    Vec2 center = Vec2(m_left + (m_right - m_left) * 0.5f, (m_bottom + m_top) * 0.5f);

    m_left = center.x - m_aspect * m_zoom;
    m_right = center.x + m_aspect * m_zoom;
    m_bottom = center.y - m_zoom;
    m_top = center.y + m_zoom;

    m_viewTransform.setOrtho(m_left, m_right, m_bottom, m_top);
    LOG(INFO) << "Camera setSize: " << width << "x" << height << ", aspect: " << m_aspect;
}


void Camera::move(Vec2 delta) {
    m_left -= delta.x;
    m_right -= delta.x;
    m_bottom -= delta.y;
    m_top -= delta.y;
    m_viewTransform.setOrtho(m_left, m_right, m_bottom, m_top);
}

void Camera::reset() {

    // initial 500mm half-width view
    Vec2 center = Vec2(297.0f / 2.0f, 420.0f / 2.0f);
    m_left = -m_aspect * m_zoom + center.x;
    m_right = m_aspect * m_zoom + center.x;
    m_bottom = -m_zoom + center.y;
    m_top = m_zoom + center.y;

    m_viewTransform = Mat3();
    m_dragging = false;

    m_viewTransform.setOrtho(m_left, m_right, m_bottom, m_top);
    m_dragging = false;
}

void Camera::zoomAtPixel(const Vec2 &px, float wheelSteps)
{
    // convert pixel to NDC
    Vec2 screen = getSize();
    Vec2 ndc((px.x / screen.x) * 2.0f - 1.0f,
             1.0f - (px.y / screen.y) * 2.0f);

    // world point under cursor before zoom
    Vec2 anchor = screenToWorld(px);

    // zoom factor (>1 zooms in)
    float factor = std::pow(1.1f, wheelSteps);

    // update zoom (vertical half-size in mm)
    m_zoom = m_zoom / factor; // clamp here if you add sensible mm bounds

    // choose new center so anchor stays fixed under cursor
    Vec2 newCenter;
    newCenter.x = anchor.x - ndc.x * m_aspect * m_zoom;
    newCenter.y = anchor.y - ndc.y * m_zoom;

    m_left   = newCenter.x - m_aspect * m_zoom;
    m_right  = newCenter.x + m_aspect * m_zoom;
    m_bottom = newCenter.y - m_zoom;
    m_top    = newCenter.y + m_zoom;

    m_viewTransform.setOrtho(m_left, m_right, m_bottom, m_top);
}