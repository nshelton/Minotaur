#pragma once

#include <optional>
#include "core/core.h"
#include "Camera.h"
#include "Page.h"

#define HANDLE_HITBOX_RADIUS 10.0f

enum class InteractionMode
{
    None,
    PanningCamera,
    DraggingEntity,
    ResizingEntity
};

enum class ResizeHandle
{
    None,
    N,
    S,
    E,
    W,
    NE,
    NW,
    SE,
    SW
};

struct InteractionState
{
    std::optional<int> hoveredId;
    std::optional<int> activeId; // the entity we grabbed
    InteractionMode mode = InteractionMode::None;

    Mat3 dragEntityStartTransform; // cached for drags
    Vec2 mouseDownWorld; // cached for drags

    // Resize state
    ResizeHandle hoveredHandle{ResizeHandle::None};
    ResizeHandle activeHandle{ResizeHandle::None};
    Vec2 resizeAnchorLocal;   // local-space anchor corner point
    Vec2 resizeHandleLocal;   // local-space handle point
    Vec2 resizeAnchorPage;    // page-space anchor position at drag start

    // Rendering options
    bool showPathNodes{true};
};

class InteractionController
{
public:
    void updateHover(const PageModel &scene, const Camera &camera, const Vec2 &mousePx);
    void onMouseDown(PageModel &scene, Camera &camera, const Vec2 &px);
    void onMouseUp();
    void onCursorPos(PageModel &scene, Camera &camera, const Vec2 &px);
    void onScroll(PageModel &scene, Camera &camera, float yoffset, const Vec2 &px);

    const InteractionState &state() const { return m_state; }

    std::optional<int> HoveredEntity() const { return m_state.hoveredId; }
    std::optional<int> SelectedEntity() const { return m_state.activeId; }

    void SelectEntity(int id) { m_state.activeId = id; }
    void ClearHover() { m_state.hoveredId.reset(); }
    void DeselectEntity() { m_state.activeId.reset(); }

    bool ShowPathNodes() const { return m_state.showPathNodes; }
    void SetShowPathNodes(bool v) { m_state.showPathNodes = v; }

private:
    std::optional<int> pick(const PageModel &scene, const Vec2 &world);
    ResizeHandle pickHandle(const Entity &entity, const Vec2 &world, float radiusMm) const;
    void computeHandlePointsLocal(const Entity &entity, Vec2 (&out)[8]) const;
    void moveEntity(PageModel &scene, int id, const Vec2 &delta);
    void resizeEntity(PageModel &scene, int id, const Vec2 &world);

    InteractionState m_state;
    Mat3 m_cameraStart;
    Vec2 m_cameraStartCenterMm;
};