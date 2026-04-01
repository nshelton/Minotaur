#pragma once

#include <optional>
#include "core/Core.h"
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

    // Mouse state (transient, not serialized)
    Vec2 mouse_pixel;
    Vec2 mouse_page_mm;

    // Rendering options
    bool showPathNodes{true};
};

class InteractionController
{
public:
    void onMouseDown(PageModel &scene, Camera &camera, const Vec2 &screenPx);
    // Begin a camera pan irrespective of what's under the cursor
    void beginPan(Camera &camera, const Vec2 &screenPx);
    void onMouseUp();
    void onCursorPos(PageModel &scene, Camera &camera, const Vec2 &screenPx);
    void onScroll(PageModel &scene, Camera &camera, float yoffset, const Vec2 &screenPx);

    const InteractionState &state() const { return m_state; }

    std::optional<int> HoveredEntity() const { return m_state.hoveredId; }
    std::optional<int> SelectedEntity() const { return m_state.activeId; }

    void SelectEntity(int id) { m_state.activeId = id; }
    void ClearHover() { m_state.hoveredId.reset(); }
    void DeselectEntity() { m_state.activeId.reset(); }

    bool ShowPathNodes() const { return m_state.showPathNodes; }
    void SetShowPathNodes(bool v) { m_state.showPathNodes = v; }

private:
    void updateMouseState(const Camera &camera, const Vec2 &screenPx)
    {
        m_state.mouse_pixel = screenPx;
        m_state.mouse_page_mm = camera.screenToWorld(screenPx);
    }

    void updateHover(const PageModel &scene, const Camera &camera, const Vec2 &mouseWorld);
    std::optional<int> pick(const PageModel &scene, const Vec2 &world);
    ResizeHandle pickHandle(const Entity &entity, const Vec2 &world, float radiusMm) const;
    void computeHandlePointsLocal(const Entity &entity, Vec2 (&out)[8]) const;
    void moveEntity(PageModel &scene, int id, const Vec2 &delta);
    void resizeEntity(PageModel &scene, int id, const Vec2 &world);

    InteractionState m_state;
    Mat3 m_cameraStart;
    Vec2 m_cameraStartCenterMm;
};