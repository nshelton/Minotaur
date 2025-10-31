#pragma once

#include "Model.h"
#include "PlotView.h"
#include "render/LineRenderer.h"
#include "render/A3PageRenderer.h"

class PlotController {
public:
    bool init();
    void update(double /*dt*/);
    void render();
    // Render with access to current page for mm->NDC mapping
    void render(const A3Page& page);
    void shutdown();
    void setTransform(Vec2 pos, float scale);

    // Bind external renderers used by the view
    void bindRenderers(LineRenderer* lines, A3PageRenderer* pageRenderer);

    // Add a PathSet entity into the model
    void addPathSet(PathSet ps);

    // Lightweight accessors for UI helpers
    const PlotModel& model() const { return m_model; }
    int activeIndex() const { return m_activeIndex; }
    void setActiveIndex(int idx) { m_activeIndex = (idx >= 0 && idx < (int)m_model.entities.size()) ? idx : -1; }

    // Input handling for hover/drag of entities
    // Returns true if the event is consumed by entity interaction
    bool onMouseButton(const A3Page& page, int button, int action, int mods, Vec2 p_px);
    bool onCursorPos(const A3Page& page, Vec2 p_px);

    // Draw overlay (bounding boxes/handles) into the line renderer
    void renderOverlays(const A3Page& page);

    PlotModel & model() { return m_model; }

private:
    PlotModel m_model{};
    PlotView m_view{};
    LineRenderer* m_lines{nullptr};
    A3PageRenderer* m_pageRenderer{nullptr};
    // Current viewport transform (same as set on LineRenderer)
    Vec2 m_viewportPos{0.0f, 0.0f};
    float m_vscale{1.0f};

    // Interaction state
    int m_hoverIndex{-1};
    int m_activeIndex{-1};
    bool m_dragging{false};
    Vec2 m_dragStart_mm{0.0f, 0.0f};
    Vec2 m_entityStartPos_mm{0.0f, 0.0f};
    float m_entityStartScale{1.0f};

    // Helpers
    int hitTestEntityAABB(const A3Page& page, Vec2 px) const;
    void computeEntityAABBmm(const PathSet& ps, Vec2& min_p, Vec2& max_p) const;
};
