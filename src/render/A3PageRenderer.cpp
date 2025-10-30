#include "A3PageRenderer.h"

#include <algorithm>

void A3PageRenderer::pageToNDC(const A3Page& page, float x_mm, float y_mm, float& x_ndc, float& y_ndc) const {
    if (m_winW <= 0 || m_winH <= 0) { x_ndc = 0.0f; y_ndc = 0.0f; return; }

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
    const float px = offX + (x_mm / pageW) * drawW_px;
    const float py = offY + (1.0f - (y_mm / pageH)) * drawH_px; // flip Y so mm up maps to NDC up

    // Convert pixel -> NDC
    x_ndc = (px / static_cast<float>(m_winW)) * 2.0f - 1.0f;
    y_ndc = 1.0f - (py / static_cast<float>(m_winH)) * 2.0f;
}

void A3PageRenderer::addOutline(const A3Page& page, LineRenderer& lines,
                                float r, float g, float b, float a) const {
    float x0 = 0.0f, y0 = 0.0f;
    float x1 = page.width_mm, y1 = page.height_mm;

    float ax0, ay0, ax1, ay1, bx0, by0, bx1, by1;
    // Bottom edge
    pageToNDC(page, x0, y0, ax0, ay0);
    pageToNDC(page, x1, y0, ax1, ay1);
    lines.addLine(ax0, ay0, ax1, ay1, r, g, b, a);
    // Top edge
    pageToNDC(page, x0, y1, bx0, by0);
    pageToNDC(page, x1, y1, bx1, by1);
    lines.addLine(bx0, by0, bx1, by1, r, g, b, a);
    // Left edge
    lines.addLine(ax0, ay0, bx0, by0, r, g, b, a);
    // Right edge
    lines.addLine(ax1, ay1, bx1, by1, r, g, b, a);
}

void A3PageRenderer::addGrid(const A3Page& page, LineRenderer& lines,
                             float step_mm,
                             float r, float g, float b, float a) const {
    if (step_mm <= 0.0f) return;

    const float pageW = page.width_mm;
    const float pageH = page.height_mm;

    // Vertical lines at x = k * step
    for (float x = 0.0f; x <= pageW + 0.001f; x += step_mm) {
        float x0, y0, x1, y1;
        pageToNDC(page, x, 0.0f, x0, y0);
        pageToNDC(page, x, pageH, x1, y1);
        lines.addLine(x0, y0, x1, y1, r, g, b, a);
    }

    // Horizontal lines at y = k * step
    for (float y = 0.0f; y <= pageH + 0.001f; y += step_mm) {
        float x0, y0, x1, y1;
        pageToNDC(page, 0.0f, y, x0, y0);
        pageToNDC(page, pageW, y, x1, y1);
        lines.addLine(x0, y0, x1, y1, r, g, b, a);
    }
}
