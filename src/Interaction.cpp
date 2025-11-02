#include "Interaction.h"

#include "Page.h"
#include "Camera.h"
#include "core/core.h"

void InteractionController::updateHover(const PageModel &scene, const Camera &camera, const Vec2 &mousePx)
{
    // Vec2 world = camera.screenToWorld(mousePx);
    // m_state.hoveredId = pick(scene, world);
}

void InteractionController::onMouseDown(PageModel &scene, Camera &camera, const Vec2 &mouseWorld)
{
    m_state.mouseDownWorld = mouseWorld;

    // did we hit an entity?
    // auto hit = pick(scene, mouseWorld);

    // if (hit)
    // {
    //     m_state.activeId = hit;
    //     // for now just dragging; later you can detect "near resize handle"
    //     m_state.mode = InteractionMode::DraggingEntity;
    // }
    // else
    // {
    // background → pan
    m_state.activeId.reset();
    m_state.mode = InteractionMode::PanningCamera;
    m_cameraStart = camera.Transform();
    m_cameraStartCenterMm = camera.center();
    // }
}

void InteractionController::onCursorPos(PageModel &scene, Camera &camera, const Vec2 &mouseWorld)
{
    switch (m_state.mode)
    {
    case InteractionMode::PanningCamera:
    {
        // Pan in mm space using cached start center
        Vec2 delta = (mouseWorld - m_state.mouseDownWorld);
        camera.move(delta);
        break;
    }
    case InteractionMode::DraggingEntity:
    {
        if (!m_state.activeId)
            break;
        Vec2 delta = mouseWorld - m_state.mouseDownWorld;
        moveEntity(scene, *m_state.activeId, delta);
        break;
    }
    case InteractionMode::ResizingEntity:
    {
        if (!m_state.activeId)
            break;
        resizeEntity(scene, *m_state.activeId, mouseWorld);
        break;
    }
    case InteractionMode::None:
    default:
        // when idle, keep hover updated
        updateHover(scene, camera, mouseWorld);
        break;
    }
}

void InteractionController::onScroll(PageModel &scene, Camera &camera, float yoffset, const Vec2 &px)
{
    camera.zoomAtPixel(px, static_cast<float>(yoffset));
}

void InteractionController::onMouseUp()
{
    m_state.mode = InteractionMode::None;
    m_state.activeId.reset();
}

std::optional<int> InteractionController::pick(const PageModel &scene, const Vec2 &world)
{
    // iterate scene items, find closest, check radius, etc.
    return std::nullopt;
}

void InteractionController::moveEntity(PageModel &scene, int id, const Vec2 &delta)
{
    // scene.get(id).position = original + delta;
    // you can store original pos at mousedown if you want exact delta
}

void InteractionController::resizeEntity(PageModel &scene, int id, const Vec2 &world)
{
}