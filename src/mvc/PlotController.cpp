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

void PlotController::setTransform(Vec2 pos, float scale)
{
    if (m_lines)
        m_lines->setTransform(pos, scale);
    m_viewportPos = pos;
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
int PlotController::hitTestEntityAABB(const A3Page &page, Vec2 pixel) const
{
    if (!m_pageRenderer)
        return -1;
    Vec2 ndc{0.0f, 0.0f};
    m_pageRenderer->pixelToNDC(pixel, ndc);
    // Undo viewport transform to get pre-viewport NDC
    Vec2 pre_ndc = ndc - m_viewportPos / (m_vscale != 0.0f ? m_vscale : 1.0f);
    Vec2 mm;
    m_pageRenderer->ndcToMm(page, pre_ndc, mm);
    for (int i = static_cast<int>(m_model.entities.size()) - 1; i >= 0; --i)
    {
        const PathSet &ps = m_model.entities[static_cast<size_t>(i)];
        Vec2 pathMin, mathMax;
        computeEntityAABBmm(ps, pathMin, mathMax);
        if (mm.x >= pathMin.x && mm.x <= mathMax.x && mm.y >= pathMin.y && mm.y <= mathMax.y)
        {
            return i;
        }
    }
    return -1;
}

void PlotController::computeEntityAABBmm(const PathSet &ps, Vec2 &min_p, Vec2 &max_p) const
{
    const float INF = 1.0e30f;
    min_p = Vec2{INF, INF};
    max_p = Vec2{-INF, -INF};
    for (const auto &path : ps.paths)
    {
        for (const auto &p : path.points)
        {
            Vec2 e = p * ps.transform.scale + ps.transform.pos;
            if (e.x < min_p.x)
                min_p.x = e.x;

            if (e.y < min_p.y)
                min_p.y = e.y;

            if (e.x > max_p.x)
                max_p.x = e.x;
    
            if (e.y > max_p.y)
                max_p.y = e.y;
        }
    }
    if (!(min_p.x <= max_p.x && min_p.y <= max_p.y))
    {
        min_p = max_p = Vec2{0.0f, 0.0f};
    }
}

bool PlotController::onMouseButton(const A3Page &page, int button, int action, int /*mods*/, Vec2 px)
{
    if (!m_pageRenderer)
        return false;
    if (button != 0)
        return false; // left button only

    if (action == 1 /* GLFW_PRESS */)
    {
        int hit = hitTestEntityAABB(page, px);
        if (hit >= 0)
        {
            m_activeIndex = hit;
            m_hoverIndex = hit;
            m_dragging = true;
            // Record drag start in mm and entity original transform
            Vec2 ndc{0, 0};
            m_pageRenderer->pixelToNDC(px, ndc);

            Vec2 pre_ndc = (ndc - m_viewportPos) / (m_vscale != 0.0f ? m_vscale : 1.0f);
            m_pageRenderer->ndcToMm(page, pre_ndc, m_dragStart_mm);

            const PathSet &ps = m_model.entities[static_cast<size_t>(m_activeIndex)];
            m_entityStartPos_mm = ps.transform.pos;
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

bool PlotController::onCursorPos(const A3Page &page, Vec2 px)
{
    if (!m_pageRenderer)
        return false;
    if (m_dragging && m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_model.entities.size()))
    {
        Vec2 ndc;
        m_pageRenderer->pixelToNDC(px, ndc);
        Vec2 pre_ndc = (ndc - m_viewportPos) / (m_vscale != 0.0f ? m_vscale : 1.0f);
        Vec2 mm;
        m_pageRenderer->ndcToMm(page, pre_ndc, mm);
        Vec2 delta = mm - m_dragStart_mm;
        PathSet &ps = m_model.entities[static_cast<size_t>(m_activeIndex)];
        ps.transform.pos = m_entityStartPos_mm + delta;
        return true;
    }
    int hit = hitTestEntityAABB(page, px);
    m_hoverIndex = hit;
    return (hit >= 0);
}

void PlotController::renderOverlays(const A3Page &page)
{
    if (!m_lines || !m_pageRenderer)
        return;
    auto drawAABB = [&](const PathSet &ps, Color col)
    {
        Vec2 path_min{0,0};
        Vec2 path_max{0,0};
        computeEntityAABBmm(ps, path_min, path_max);
        Vec2 a0, a1, b0, b1;
        m_pageRenderer->mmToNDC(page, path_min, a0);
        m_pageRenderer->mmToNDC(page, Vec2{path_max.x, path_min.y}, a1);
        m_pageRenderer->mmToNDC(page, Vec2{path_min.x, path_max.y}, b0);
        m_pageRenderer->mmToNDC(page, path_max, b1);

        m_lines->addLine(a0, a1, col);
        m_lines->addLine(b0, b1, col);
        m_lines->addLine(a0, b0, col);
        m_lines->addLine(a1, b1, col);

        // Corner handles as small squares in mm
        const float hs = 5.0f;
        auto drawHandle = [&](Vec2 corner_mm)
        {
            Vec2 h0 = corner_mm + Vec2{-hs * 0.5f, -hs * 0.5f};
            Vec2 h1 = corner_mm + Vec2{ hs * 0.5f, -hs * 0.5f};
            Vec2 h2 = corner_mm + Vec2{ hs * 0.5f,  hs * 0.5f};
            Vec2 h3 = corner_mm + Vec2{-hs * 0.5f,  hs * 0.5f};

            Vec2 h0_mm;
            Vec2 h1_mm;
            Vec2 h2_mm;
            Vec2 h3_mm;

            m_pageRenderer->mmToNDC(page, h0, h0_mm);
            m_pageRenderer->mmToNDC(page, h1, h1_mm);
            m_pageRenderer->mmToNDC(page, h2, h2_mm);
            m_pageRenderer->mmToNDC(page, h3, h3_mm);
            m_lines->addLine(h0_mm, h1_mm, col);
            m_lines->addLine(h1_mm, h2_mm, col);
            m_lines->addLine(h2_mm, h3_mm, col);
            m_lines->addLine(h3_mm, h0_mm, col);
        };
        drawHandle(Vec2{path_min.x, path_min.y});
        drawHandle(Vec2{path_max.x, path_min.y});
        drawHandle(Vec2{path_max.x, path_max.y});
        drawHandle(Vec2{path_min.x, path_max.y});
    };

    if (m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_model.entities.size()))
    {
        drawAABB(m_model.entities[static_cast<size_t>(m_activeIndex)], Color(0.95f, 0.95f, 0.15f, 1.0f));
    }
    else if (m_hoverIndex >= 0 && m_hoverIndex < static_cast<int>(m_model.entities.size()))
    {
        drawAABB(m_model.entities[static_cast<size_t>(m_hoverIndex)], Color(0.2f, 0.7f, 0.9f, 1.0f));
    }
}

// removed shape builders from controller (moved to PathSetGenerator)
