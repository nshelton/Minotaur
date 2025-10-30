#pragma once

#include "models/A3Page.h"
#include "render/LineRenderer.h"

class A3PageRenderer {
public:
    void setWindowSize(int width, int height) { m_winW = width; m_winH = height; }

    // Add page outline lines to the provided LineRenderer, preserving aspect
    void addOutline(const A3Page& page, LineRenderer& lines,
                    float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) const;

    // Add a background grid with spacing in millimeters (e.g., 10 for 1cm)
    void addGrid(const A3Page& page, LineRenderer& lines,
                 float step_mm = 10.0f,
                 float r = 0.35f, float g = 0.35f, float b = 0.4f, float a = 0.35f) const;

    // Public conversion helper: page-space millimeters to NDC (letterboxed)
    void mmToNDC(const A3Page& page, float x_mm, float y_mm, float& x_ndc, float& y_ndc) const {
        pageToNDC(page, x_mm, y_mm, x_ndc, y_ndc);
    }

    // Convert window pixel coordinates to NDC
    void pixelToNDC(float x_px, float y_px, float& x_ndc, float& y_ndc) const {
        if (m_winW <= 0 || m_winH <= 0) { x_ndc = 0.0f; y_ndc = 0.0f; return; }
        x_ndc = (x_px / static_cast<float>(m_winW)) * 2.0f - 1.0f;
        y_ndc = 1.0f - (y_px / static_cast<float>(m_winH)) * 2.0f;
    }

    // Inverse mapping: NDC to page-space millimeters (letterboxed)
    void ndcToMm(const A3Page& page, float x_ndc, float y_ndc, float& x_mm, float& y_mm) const;

private:
    int m_winW{0};
    int m_winH{0};

    // Convert page-space (mm) to NDC, letterboxed to preserve aspect
    void pageToNDC(const A3Page& page, float x_mm, float y_mm, float& x_ndc, float& y_ndc) const;
};
