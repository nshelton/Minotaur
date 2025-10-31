#include "A3PageRenderer.h"

#include <algorithm>

void A3PageRenderer::pageToNDC(const A3Page& page, Vec2 mm, Vec2& ndc) const {
    if (m_winW <= 0 || m_winH <= 0) { ndc = Vec2(0.0f, 0.0f); return; }

    const float pageW = page.width_mm;
    const float pageH = page.height_mm;
    const float aw = static_cast<float>(m_winW) / static_cast<float>(m_winH);
    const float ap = pageW / pageH;

    float drawW_px = 0.0f, drawH_px = 0.0f;
    if (aw >= ap) {
        // Window is wider: fit by height
        drawH_px = static_cast<float>(m_winH);
        drawW_px = drawH_px * ap;
    } else {
        // Window is taller: fit by width
        drawW_px = static_cast<float>(m_winW);
        drawH_px = drawW_px / ap;
    }

    const float offX = (static_cast<float>(m_winW) - drawW_px) * 0.5f;
    const float offY = (static_cast<float>(m_winH) - drawH_px) * 0.5f;

    // Map page coords to pixel coords inside the letterboxed rect
    const float px = offX + (mm.x / pageW) * drawW_px;
    const float py = offY + (1.0f - (mm.y / pageH)) * drawH_px; // flip Y so mm up maps to NDC up

    // Convert pixel -> NDC
    ndc.x = (px / static_cast<float>(m_winW)) * 2.0f - 1.0f;
    ndc.y = 1.0f - (py / static_cast<float>(m_winH)) * 2.0f;
}

void A3PageRenderer::addOutline(const A3Page& page, LineRenderer& lines, Color col) const {
    Vec2 p_min = Vec2(0,0);
    Vec2 p_max = Vec2(page.width_mm, page.height_mm);

    Vec2 a0, a1, b0, b1;

    pageToNDC(page, p_min, a0);
    pageToNDC(page, Vec2(p_max.x, p_min.y), a1);
    pageToNDC(page, p_max, b0);
    pageToNDC(page, Vec2(p_min.x, p_max.y), b1);

    lines.addLine(a0, a1, col);
    lines.addLine(a1, b0, col);
    lines.addLine(b0, b1, col);
    lines.addLine(b1, a0, col);
}

void A3PageRenderer::addGrid(const A3Page& page, LineRenderer& lines, float step_mm, Color col) const {
    if (step_mm <= 0.0f) return;

    const float pageW = page.width_mm;
    const float pageH = page.height_mm;

    // Vertical lines at x = k * step
    for (float x = 0.0f; x <= pageW + 0.001f; x += step_mm) {
        Vec2 p0, p1;
        pageToNDC(page, Vec2(x, 0.0f), p0);
        pageToNDC(page, Vec2(x, pageH), p1);
        lines.addLine(p0, p1, col);
    }

    // Horizontal lines at y = k * step
    for (float y = 0.0f; y <= pageH + 0.001f; y += step_mm) {
        Vec2 p0, p1;
        pageToNDC(page, Vec2(0.0f, y), p0);
        pageToNDC(page, Vec2(pageW, y), p1);
        lines.addLine(p0, p1, col);
    }
}

/// @brief  Convert NDC to page-space millimeters (inverse of pageToNDC)
/// @param page 
/// @param ndc 
/// @param mm 
void A3PageRenderer::ndcToMm(const A3Page& page, Vec2 ndc, Vec2& mm) const {
    if (m_winW <= 0 || m_winH <= 0) {mm = Vec2(0,0); return; }

    const float pageW = page.width_mm;
    const float pageH = page.height_mm;
    const float aw = static_cast<float>(m_winW) / static_cast<float>(m_winH);
    const float ap = pageW / pageH;

    float drawW_px = 0.0f, drawH_px = 0.0f;
    if (aw >= ap) {
        drawH_px = static_cast<float>(m_winH);
        drawW_px = drawH_px * ap;
    } else {
        drawW_px = static_cast<float>(m_winW);
        drawH_px = drawW_px / ap;
    }

    const float offX = (static_cast<float>(m_winW) - drawW_px) * 0.5f;
    const float offY = (static_cast<float>(m_winH) - drawH_px) * 0.5f;

    // NDC -> pixel
    const float px = (ndc.x + 1.0f) * 0.5f * static_cast<float>(m_winW);
    const float py = (1.0f - ndc.y) * 0.5f * static_cast<float>(m_winH);

    // Pixel inside letterboxed rect -> normalized page coords
    const float u = (px - offX) / drawW_px;   // 0..1 across page width
    const float v = (py - offY) / drawH_px;   // 0..1 from top to bottom

    mm.x = u * pageW;
    mm.y = (1.0f - v) * pageH; // flip back to page up direction
}
