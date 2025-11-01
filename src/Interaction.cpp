#include "Interaction.h"

#include "Page.h"
#include "Camera.h"
#include "core/core.h"

void InteractionController::updateHover(const PageModel &scene, const Camera &camera, const Vec2 &mousePx)
{
    // Vec2 world = camera.screenToWorld(mousePx);
    // m_state.hoveredId = pick(scene, world);

}

void InteractionController::onMouseDown(PageModel &scene, Camera &camera, const Vec2 &mousePx)
{
    // m_state.mouseDownScreen = mousePx;
    // m_state.mouseDownWorld = camera.screenToWorld(mousePx);

    // // did we hit an entity?
    // Vec2 world = m_state.mouseDownWorld;
    // auto hit = pick(scene, world);

    // if (hit)
    // {
    //     m_state.activeId = hit;
    //     // for now just dragging; later you can detect "near resize handle"
    //     m_state.mode = InteractionMode::DraggingEntity;
    // }
    // else
    // {
    //     // background → pan
    //     m_state.activeId.reset();
    //     m_state.mode = InteractionMode::PanningCamera;
    //     m_cameraStart = camera.getTransform(); // whatever you use
    // }
}

void InteractionController::onCursorPos(PageModel &scene, Camera &camera, const Vec2 &mousePx)
{
    // switch (m_state.mode)
    // {
    // case InteractionMode::PanningCamera:
    // {
    //     // pan in screen or world space
    //     Vec2 curWorld = camera.screenToWorld(mousePx);
    //     Vec2 delta = curWorld - m_state.mouseDownWorld;
    //     camera.setCenter(m_cameraStart.center - delta); // invert if needed
    //     break;
    // }
    // case InteractionMode::DraggingEntity:
    // {
    //     if (!m_state.activeId)
    //         break;
    //     Vec2 curWorld = camera.screenToWorld(mousePx);
    //     Vec2 delta = curWorld - m_state.mouseDownWorld;
    //     moveEntity(scene, *m_state.activeId, delta);
    //     break;
    // }
    // case InteractionMode::ResizingEntity:
    // {
    //     if (!m_state.activeId)
    //         break;
    //     Vec2 curWorld = camera.screenToWorld(mousePx);
    //     resizeEntity(scene, *m_state.activeId, curWorld);
    //     break;
    // }
    // case InteractionMode::None:
    // default:
    //     // when idle, keep hover updated
    //     updateHover(scene, camera, mousePx);
    //     break;
    // }
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