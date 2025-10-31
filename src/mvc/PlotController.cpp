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

void PlotController::setTransform(const Transform2D &t)
{
    if (m_lines)
        m_lines->setTransform(t);
    m_transform = t;
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

    Vec2 ndc(0.0f, 0.0f);
    m_pageRenderer->pixelToNDC(pixel, ndc);
    // Undo viewport transform to get pre-viewport NDC
    Vec2 pre_ndc = m_transform * ndc;
    Vec2 mm;

    m_pageRenderer->ndcToMm(page, pre_ndc, mm);

    for (int i = static_cast<int>(m_model.entities.size()) - 1; i >= 0; --i)
    {
        const PathSet &cps = m_model.entities[static_cast<size_t>(i)];
        PathSet &ps = const_cast<PathSet&>(cps);

        ps.computeAABB();
        if (ps.aabb.contains(mm))
        {
            return i;
        }
    }
    return -1;
}

// compute bounding box of pathset
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

             Vec2 pre_ndc = m_transform / ndc;
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
    m_model.mousePos = px;
    Vec2 ndc;
    m_pageRenderer->pixelToNDC(px, ndc);
    Vec2 mm;
    m_pageRenderer->ndcToMm(page, ndc, m_model.mouseMm);

    if (!m_pageRenderer)
        return false;

    if (m_dragging && m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_model.entities.size()))
    {
        Vec2 ndc;
        m_pageRenderer->pixelToNDC(px, ndc);
        Vec2 pre_ndc = m_transform / ndc;
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
        Vec2 a0, a1, b0, b1;
        m_pageRenderer->mmToNDC(page, ps.aabb.min, a0);
        m_pageRenderer->mmToNDC(page, Vec2{ps.aabb.max.x, ps.aabb.min.y}, a1);
        m_pageRenderer->mmToNDC(page, Vec2{ps.aabb.min.x, ps.aabb.max.y}, b0);
        m_pageRenderer->mmToNDC(page, ps.aabb.max, b1);

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
        drawHandle(Vec2{ps.aabb.min.x, ps.aabb.min.y});
        drawHandle(Vec2{ps.aabb.max.x, ps.aabb.min.y});
        drawHandle(Vec2{ps.aabb.max.x, ps.aabb.max.y});
        drawHandle(Vec2{ps.aabb.min.x, ps.aabb.max.y});
    };

    if (m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_model.entities.size()))
    {
        m_model.entities[static_cast<size_t>(m_activeIndex)].computeAABB();
        drawAABB(m_model.entities[static_cast<size_t>(m_activeIndex)], Color(0.95f, 0.95f, 0.15f, 1.0f));
    }
    else if (m_hoverIndex >= 0 && m_hoverIndex < static_cast<int>(m_model.entities.size()))
    {
        m_model.entities[static_cast<size_t>(m_hoverIndex)].computeAABB();
        drawAABB(m_model.entities[static_cast<size_t>(m_hoverIndex)], Color(0.2f, 0.7f, 0.9f, 1.0f));
    }
}

// removed shape builders from controller (moved to PathSetGenerator)
