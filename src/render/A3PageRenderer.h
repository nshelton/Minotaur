#pragma once

#include "models/A3Page.h"
#include "render/LineRenderer.h"

class A3PageRenderer {
public:
    void setWindowSize(int width, int height) { m_winW = width; m_winH = height; }

    // Add page outline lines to the provided LineRenderer, preserving aspect
    void addOutline(const A3Page& page, LineRenderer& lines, Color col) const;

    // Add a background grid with spacing in millimeters (e.g., 10 for 1cm)
    void addGrid(const A3Page& page, LineRenderer& lines,
                 float step_mm = 10.0f,
                 Color col = Color(0.35f, 0.35f, 0.4f, 0.35f)) const;

    // Public conversion helper: page-space millimeters to NDC (letterboxed)
    void mmToNDC(const A3Page& page, Vec2 p_mm, Vec2& ndc) const {
        pageToNDC(page, p_mm, ndc);
    }

    // Convert window pixel coordinates to NDC
    void pixelToNDC(Vec2 p_px, Vec2& ndc) const {
        if (m_winW <= 0 || m_winH <= 0) { ndc = Vec2{0.0f, 0.0f}; return; }
        ndc.x = (p_px.x / static_cast<float>(m_winW)) * 2.0f - 1.0f;
        ndc.y = 1.0f - (p_px.y / static_cast<float>(m_winH)) * 2.0f;
    }

    // Inverse mapping: NDC to page-space millimeters (letterboxed)
    void ndcToMm(const A3Page& page, Vec2 ndc, Vec2& p_mm) const;

private:
    int m_winW{0};
    int m_winH{0};

    // Convert page-space (mm) to NDC, letterboxed to preserve aspect
    void pageToNDC(const A3Page& page, Vec2 p_mm, Vec2& ndc) const;
};
