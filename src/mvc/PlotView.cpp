#include "PlotView.h"

void PlotView::render(const PlotModel& model, LineRenderer& lines, const A3PageRenderer& pageRenderer) {
    A3Page page = A3Page::Portrait();
    for (const auto& ps : model.entities) {
        for (const auto& path : ps.paths) {
            if (path.points.size() < 2) continue;
            float prevx_ndc = 0.0f, prevy_ndc = 0.0f;
            bool havePrev = false;
            for (const auto& p : path.points) {
                float ex = (p.x * ps.transform.scale) + ps.transform.tx;
                float ey = (p.y * ps.transform.scale) + ps.transform.ty;
                float x_ndc = 0.0f, y_ndc = 0.0f;
                pageRenderer.mmToNDC(page, ex, ey, x_ndc, y_ndc);
                if (havePrev) {
                    lines.addLine(prevx_ndc, prevy_ndc, x_ndc, y_ndc, ps.r, ps.g, ps.b, ps.a);
                } else {
                    havePrev = true;
                }
                prevx_ndc = x_ndc; prevy_ndc = y_ndc;
            }
            if (path.closed && havePrev && path.points.size() > 2) {
                float sx = (path.points.front().x * ps.transform.scale) + ps.transform.tx;
                float sy = (path.points.front().y * ps.transform.scale) + ps.transform.ty;
                float sx_ndc=0, sy_ndc=0;
                pageRenderer.mmToNDC(page, sx, sy, sx_ndc, sy_ndc);
                lines.addLine(prevx_ndc, prevy_ndc, sx_ndc, sy_ndc, ps.r, ps.g, ps.b, ps.a);
            }
        }
    }
}

void PlotView::render(const PlotModel& model, const A3Page& page, LineRenderer& lines, const A3PageRenderer& pageRenderer) {
    for (const auto& ps : model.entities) {
        for (const auto& path : ps.paths) {
            if (path.points.size() < 2) continue;
            float prevx_ndc = 0.0f, prevy_ndc = 0.0f;
            bool havePrev = false;
            for (const auto& p : path.points) {
                float ex = (p.x * ps.transform.scale) + ps.transform.tx;
                float ey = (p.y * ps.transform.scale) + ps.transform.ty;
                float x_ndc = 0.0f, y_ndc = 0.0f;
                pageRenderer.mmToNDC(page, ex, ey, x_ndc, y_ndc);
                if (havePrev) {
                    lines.addLine(prevx_ndc, prevy_ndc, x_ndc, y_ndc, ps.r, ps.g, ps.b, ps.a);
                } else {
                    havePrev = true;
                }
                prevx_ndc = x_ndc; prevy_ndc = y_ndc;
            }
            if (path.closed && havePrev && path.points.size() > 2) {
                float sx = (path.points.front().x * ps.transform.scale) + ps.transform.tx;
                float sy = (path.points.front().y * ps.transform.scale) + ps.transform.ty;
                float sx_ndc=0, sy_ndc=0;
                pageRenderer.mmToNDC(page, sx, sy, sx_ndc, sy_ndc);
                lines.addLine(prevx_ndc, prevy_ndc, sx_ndc, sy_ndc, ps.r, ps.g, ps.b, ps.a);
            }
        }
    }
}
