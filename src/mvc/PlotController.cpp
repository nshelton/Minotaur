#include <cmath>
#include "PlotController.h"

bool PlotController::init()
{
    return m_view.init();
}

void PlotController::update(double /*dt*/)
{
    // Update model state if needed
}

void PlotController::render()
{
    if (m_lines && m_pageRenderer)
    {
        m_view.render(m_model, *m_lines, *m_pageRenderer);
    }
}

void PlotController::render(const A3Page &page)
{
    if (m_lines && m_pageRenderer)
    {
        m_view.render(m_model, page, *m_lines, *m_pageRenderer);
    }
}

void PlotController::shutdown()
{
    m_view.shutdown();
}

void PlotController::setTransform(float tx, float ty, float scale)
{
    if (m_lines)
        m_lines->setTransform(tx, ty, scale);
    m_vtx = tx;
    m_vty = ty;
    m_vscale = scale;
}

void PlotController::bindRenderers(LineRenderer *lines, A3PageRenderer *pageRenderer)
{
    m_lines = lines;
    m_pageRenderer = pageRenderer;
}

void PlotController::addPathSet(PathSet ps)
{
    m_model.entities.push_back(std::move(ps));
}

/// @brief Hit test for entity AABB under given pixel coordinates
/// @param page
/// @param x_px screen pixel x for mouse
/// @param y_px screen pixel y for mouse
/// @return
int PlotController::hitTestEntityAABB(const A3Page &page, float x_px, float y_px) const
{
    if (!m_pageRenderer)
        return -1;
    float x_ndc = 0.0f, y_ndc = 0.0f;
    m_pageRenderer->pixelToNDC(x_px, y_px, x_ndc, y_ndc);
    // Undo viewport transform to get pre-viewport NDC
    float pre_ndc_x = (x_ndc - m_vtx) / (m_vscale != 0.0f ? m_vscale : 1.0f);
    float pre_ndc_y = (y_ndc - m_vty) / (m_vscale != 0.0f ? m_vscale : 1.0f);
    float x_mm = 0.0f, y_mm = 0.0f;
    m_pageRenderer->ndcToMm(page, pre_ndc_x, pre_ndc_y, x_mm, y_mm);
    for (int i = static_cast<int>(m_model.entities.size()) - 1; i >= 0; --i)
    {
        const PathSet &ps = m_model.entities[static_cast<size_t>(i)];
        float minx, miny, maxx, maxy;
        computeEntityAABBmm(ps, minx, miny, maxx, maxy);
        if (x_mm >= minx && x_mm <= maxx && y_mm >= miny && y_mm <= maxy)
        {
            return i;
        }
    }
    return -1;
}

void PlotController::computeEntityAABBmm(const PathSet &ps, float &minx, float &miny, float &maxx, float &maxy) const
{
    const float INF = 1.0e30f;
    minx = INF;
    miny = INF;
    maxx = -INF;
    maxy = -INF;
    for (const auto &path : ps.paths)
    {
        for (const auto &p : path.points)
        {
            float ex = (p.x * ps.transform.scale) + ps.transform.tx;
            float ey = (p.y * ps.transform.scale) + ps.transform.ty;
            if (ex < minx)
                minx = ex;
            if (ey < miny)
                miny = ey;
            if (ex > maxx)
                maxx = ex;
            if (ey > maxy)
                maxy = ey;
        }
    }
    if (!(minx <= maxx && miny <= maxy))
    {
        minx = miny = maxx = maxy = 0.0f;
    }
}

bool PlotController::onMouseButton(const A3Page &page, int button, int action, int /*mods*/, double x_px, double y_px)
{
    if (!m_pageRenderer)
        return false;
    if (button != 0)
        return false; // left button only

    if (action == 1 /* GLFW_PRESS */)
    {
        int hit = hitTestEntityAABB(page, static_cast<float>(x_px), static_cast<float>(y_px));
        if (hit >= 0)
        {
            m_activeIndex = hit;
            m_hoverIndex = hit;
            m_dragging = true;
            // Record drag start in mm and entity original transform
            float x_ndc = 0.0f, y_ndc = 0.0f;
            m_pageRenderer->pixelToNDC(static_cast<float>(x_px), static_cast<float>(y_px), x_ndc, y_ndc);
            float pre_ndc_x = (x_ndc - m_vtx) / (m_vscale != 0.0f ? m_vscale : 1.0f);
            float pre_ndc_y = (y_ndc - m_vty) / (m_vscale != 0.0f ? m_vscale : 1.0f);
            m_pageRenderer->ndcToMm(page, pre_ndc_x, pre_ndc_y, m_dragStartX_mm, m_dragStartY_mm);
            const PathSet &ps = m_model.entities[static_cast<size_t>(m_activeIndex)];
            m_entityStartTx = ps.transform.tx;
            m_entityStartTy = ps.transform.ty;
            return true;
        }
    }
    else if (action == 0 /* GLFW_RELEASE */)
    {
        if (m_dragging)
        {
            m_dragging = false;
            // Clear active selection so overlays only show on hover
            m_activeIndex = -1;
            return true;
        }
    }
    return false;
}

bool PlotController::onCursorPos(const A3Page &page, double x_px, double y_px)
{
    if (!m_pageRenderer)
        return false;
    if (m_dragging && m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_model.entities.size()))
    {
        float x_ndc = 0.0f, y_ndc = 0.0f;
        m_pageRenderer->pixelToNDC(static_cast<float>(x_px), static_cast<float>(y_px), x_ndc, y_ndc);
        float pre_ndc_x = (x_ndc - m_vtx) / (m_vscale != 0.0f ? m_vscale : 1.0f);
        float pre_ndc_y = (y_ndc - m_vty) / (m_vscale != 0.0f ? m_vscale : 1.0f);
        float x_mm = 0.0f, y_mm = 0.0f;
        m_pageRenderer->ndcToMm(page, pre_ndc_x, pre_ndc_y, x_mm, y_mm);
        float dx = x_mm - m_dragStartX_mm;
        float dy = y_mm - m_dragStartY_mm;
        PathSet &ps = m_model.entities[static_cast<size_t>(m_activeIndex)];
        ps.transform.tx = m_entityStartTx + dx;
        ps.transform.ty = m_entityStartTy + dy;
        return true;
    }
    int hit = hitTestEntityAABB(page, static_cast<float>(x_px), static_cast<float>(y_px));
    m_hoverIndex = hit;
    return (hit >= 0);
}

void PlotController::renderOverlays(const A3Page &page)
{
    if (!m_lines || !m_pageRenderer)
        return;
    auto drawAABB = [&](const PathSet &ps, float r, float g, float b, float a)
    {
        float minx, miny, maxx, maxy;
        computeEntityAABBmm(ps, minx, miny, maxx, maxy);
        float ax0, ay0, ax1, ay1;
        m_pageRenderer->mmToNDC(page, minx, miny, ax0, ay0);
        m_pageRenderer->mmToNDC(page, maxx, miny, ax1, ay1);
        m_lines->addLine(ax0, ay0, ax1, ay1, r, g, b, a);
        float bx0, by0, bx1, by1;
        m_pageRenderer->mmToNDC(page, minx, maxy, bx0, by0);
        m_pageRenderer->mmToNDC(page, maxx, maxy, bx1, by1);
        m_lines->addLine(bx0, by0, bx1, by1, r, g, b, a);
        m_lines->addLine(ax0, ay0, bx0, by0, r, g, b, a);
        m_lines->addLine(ax1, ay1, bx1, by1, r, g, b, a);

        // Corner handles as small squares in mm
        const float hs = 5.0f;
        auto drawHandle = [&](float cx_mm, float cy_mm)
        {
            float h0x, h0y, h1x, h1y, h2x, h2y, h3x, h3y;
            m_pageRenderer->mmToNDC(page, cx_mm - hs * 0.5f, cy_mm - hs * 0.5f, h0x, h0y);
            m_pageRenderer->mmToNDC(page, cx_mm + hs * 0.5f, cy_mm - hs * 0.5f, h1x, h1y);
            m_pageRenderer->mmToNDC(page, cx_mm + hs * 0.5f, cy_mm + hs * 0.5f, h2x, h2y);
            m_pageRenderer->mmToNDC(page, cx_mm - hs * 0.5f, cy_mm + hs * 0.5f, h3x, h3y);
            m_lines->addLine(h0x, h0y, h1x, h1y, r, g, b, a);
            m_lines->addLine(h1x, h1y, h2x, h2y, r, g, b, a);
            m_lines->addLine(h2x, h2y, h3x, h3y, r, g, b, a);
            m_lines->addLine(h3x, h3y, h0x, h0y, r, g, b, a);
        };
        drawHandle(minx, miny);
        drawHandle(maxx, miny);
        drawHandle(maxx, maxy);
        drawHandle(minx, maxy);
    };

    if (m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_model.entities.size()))
    {
        drawAABB(m_model.entities[static_cast<size_t>(m_activeIndex)], 0.95f, 0.95f, 0.15f, 1.0f);
    }
    else if (m_hoverIndex >= 0 && m_hoverIndex < static_cast<int>(m_model.entities.size()))
    {
        drawAABB(m_model.entities[static_cast<size_t>(m_hoverIndex)], 0.2f, 0.7f, 0.9f, 1.0f);
    }
}

// removed shape builders from controller (moved to PathSetGenerator)
