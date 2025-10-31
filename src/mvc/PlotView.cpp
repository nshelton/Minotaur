#include "PlotView.h"

void PlotView::render(const PlotModel& model, LineRenderer& lines, const A3PageRenderer& pageRenderer) {
    A3Page page = A3Page::Portrait();
    for (const auto& ps : model.entities) {
        for (const auto& path : ps.paths) {
            if (path.points.size() < 2) continue;
            Vec2 prev_ndc;
            bool havePrev = false;
            for (const auto& p : path.points) {

                Vec2 e = p * ps.transform.scale + ps.transform.pos;
                Vec2 ndc;
                pageRenderer.mmToNDC(page, e, ndc);
                if (havePrev) {
                    lines.addLine(prev_ndc, ndc, ps.color);
                } else {
                    havePrev = true;
                }
                prev_ndc = ndc;
            }
            if (path.closed && havePrev && path.points.size() > 2) {
                Vec2 s_mm = path.points.front() * ps.transform.scale + ps.transform.pos;
                Vec2 s_ndc;
                pageRenderer.mmToNDC(page, s_mm, s_ndc);
                lines.addLine(prev_ndc, s_ndc, ps.color);
            }
        }
    }
}

void PlotView::render(const PlotModel& model, const A3Page& page, LineRenderer& lines, const A3PageRenderer& pageRenderer) {
    for (const auto& ps : model.entities) {
        for (const auto& path : ps.paths) {
            if (path.points.size() < 2) continue;
            Vec2 prev_ndc{0.0f, 0.0f};
            bool havePrev = false;
            for (const auto& p : path.points) {
                Vec2 e = p * ps.transform.scale + ps.transform.pos;
                Vec2 ndc{0.0f, 0.0f};
                pageRenderer.mmToNDC(page, e, ndc);
                if (havePrev) {
                    lines.addLine(prev_ndc, ndc, ps.color);
                } else {
                    havePrev = true;
                }
                prev_ndc = ndc;
            }
            if (path.closed && havePrev && path.points.size() > 2) {
                Vec2 start = path.points.front() * ps.transform.scale + ps.transform.pos;
                Vec2 start_ndc{0.0f, 0.0f};
                pageRenderer.mmToNDC(page, start, start_ndc);
                if (havePrev) {
                    lines.addLine(prev_ndc, start_ndc, ps.color);
                }
            }
        }
    }
}
