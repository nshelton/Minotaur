#pragma once

#include <optional>
#include "core/core.h"
#include "Camera.h"
#include "Page.h"

enum class InteractionMode
{
    None,
    PanningCamera,
    DraggingEntity,
    ResizingEntity
};

struct InteractionState
{
    std::optional<int> hoveredId;
    std::optional<int> activeId; // the entity we grabbed
    InteractionMode mode = InteractionMode::None;

    // cached for drags
    Vec2 mouseDownWorld;
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

    private:
    std::optional<int> pick(const PageModel &scene, const Vec2 &world);
    void moveEntity(PageModel &scene, int id, const Vec2 &delta);
    void resizeEntity(PageModel &scene, int id, const Vec2 &world);

    InteractionState m_state;
    Mat3 m_cameraStart;
    Vec2 m_cameraStartCenterMm;
};