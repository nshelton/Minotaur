#include "Interaction.h"

#include "Page.h"
#include "Camera.h"
#include "core/core.h"
#include <glog/logging.h>
#include <cmath>

void InteractionController::updateHover(const PageModel &scene, const Camera &camera, const Vec2 &mouseWorld)
{
    m_state.hoveredId = pick(scene, mouseWorld);
    m_state.hoveredHandle = ResizeHandle::None;
    if (m_state.hoveredId)
    {
        const Entity &e = scene.entities.at(*m_state.hoveredId);
        m_state.hoveredHandle = pickHandle(e, mouseWorld, HANDLE_HITBOX_RADIUS);
    }
}

void InteractionController::onMouseDown(PageModel &scene, Camera &camera, const Vec2 &mouseWorld)
{
    m_state.mouseDownWorld = mouseWorld;

    auto hit = pick(scene, mouseWorld);
    LOG(INFO) << "CursorPos at (" << mouseWorld.x << ", " << mouseWorld.y << "), hit entity " << (hit ? std::to_string(*hit) : "none");

    if (hit)
    {
        m_state.activeId = hit;
        const Entity &e = scene.entities.at(*hit);
        // Prefer resizing if mouse is near a handle; otherwise drag
        ResizeHandle handle = pickHandle(e, mouseWorld, HANDLE_HITBOX_RADIUS);
        if (handle != ResizeHandle::None)
        {
            m_state.mode = InteractionMode::ResizingEntity;
            m_state.activeHandle = handle;
            m_state.dragEntityStartTransform = e.localToPage;

            // Cache local-space aabb corners and determine anchor/handle local points
            Vec2 ptsLocal[8];
            computeHandlePointsLocal(e, ptsLocal);
            // Order: N,S,E,W,NE,NW,SE,SW (match enum order after None)
            int idx = 0;
            switch (handle)
            {
            case ResizeHandle::N: idx = 0; break;
            case ResizeHandle::S: idx = 1; break;
            case ResizeHandle::E: idx = 2; break;
            case ResizeHandle::W: idx = 3; break;
            case ResizeHandle::NE: idx = 4; break;
            case ResizeHandle::NW: idx = 5; break;
            case ResizeHandle::SE: idx = 6; break;
            case ResizeHandle::SW: idx = 7; break;
            default: idx = 0; break;
            }

            // Anchor is opposite corner for corner handles; for edge handles, opposite edge midpoint
            Vec2 anchorLocal;
            switch (handle)
            {
            case ResizeHandle::NE: anchorLocal = ptsLocal[7]; break; // SW
            case ResizeHandle::NW: anchorLocal = ptsLocal[6]; break; // SE
            case ResizeHandle::SE: anchorLocal = ptsLocal[5]; break; // NW
            case ResizeHandle::SW: anchorLocal = ptsLocal[4]; break; // NE
            case ResizeHandle::N:  anchorLocal = ptsLocal[1]; break; // S
            case ResizeHandle::S:  anchorLocal = ptsLocal[0]; break; // N
            case ResizeHandle::E:  anchorLocal = ptsLocal[3]; break; // W
            case ResizeHandle::W:  anchorLocal = ptsLocal[2]; break; // E
            default: anchorLocal = ptsLocal[7]; break;
            }

            m_state.resizeHandleLocal = ptsLocal[idx];
            m_state.resizeAnchorLocal = anchorLocal;
            m_state.resizeAnchorPage = m_state.dragEntityStartTransform.apply(anchorLocal);
        }
        else
        {
            m_state.mode = InteractionMode::DraggingEntity;
            m_state.dragEntityStartTransform = e.localToPage;
        }
    }
    else
    {
        // background → pan
        // m_state.activeId.reset();
        m_state.mode = InteractionMode::PanningCamera;
        m_cameraStart = camera.Transform();
        m_cameraStartCenterMm = camera.center();
    }
}

void InteractionController::beginPan(Camera &camera, const Vec2 &mouseWorld)
{
    m_state.mouseDownWorld = mouseWorld;
    m_state.mode = InteractionMode::PanningCamera;
    m_cameraStart = camera.Transform();
    m_cameraStartCenterMm = camera.center();
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
    m_state.activeHandle = ResizeHandle::None;
}

std::optional<int> InteractionController::pick(const PageModel &scene, const Vec2 &world)
{
    for (const auto &[id, entity] : scene.entities)
    {
        if (!entity.visible)
            continue;
        if (entity.contains(world, 10.0f)) // 10mm tolerance
            return id;
    }
    return std::nullopt;
}

void InteractionController::computeHandlePointsLocal(const Entity &entity, Vec2 (&out)[8]) const
{
    BoundingBox bb = entity.boundsLocal();
    const Vec2 minL = bb.min;
    const Vec2 maxL = bb.max;
    const Vec2 mid = Vec2((minL.x + maxL.x) * 0.5f, (minL.y + maxL.y) * 0.5f);

    // N, S, E, W
    out[0] = Vec2(mid.x, maxL.y);
    out[1] = Vec2(mid.x, minL.y);
    out[2] = Vec2(maxL.x, mid.y);
    out[3] = Vec2(minL.x, mid.y);

    // NE, NW, SE, SW
    out[4] = Vec2(maxL.x, maxL.y);
    out[5] = Vec2(minL.x, maxL.y);
    out[6] = Vec2(maxL.x, minL.y);
    out[7] = Vec2(minL.x, minL.y);
}

ResizeHandle InteractionController::pickHandle(const Entity &entity, const Vec2 &world, float radiusMm) const
{
    Vec2 handlesLocal[8];
    computeHandlePointsLocal(entity, handlesLocal);
    const Mat3 &M = entity.localToPage;
    const float r2 = radiusMm * radiusMm;

    struct Pair { ResizeHandle h; int idx; } table[8] = {
        {ResizeHandle::N, 0}, {ResizeHandle::S, 1}, {ResizeHandle::E, 2}, {ResizeHandle::W, 3},
        {ResizeHandle::NE, 4}, {ResizeHandle::NW, 5}, {ResizeHandle::SE, 6}, {ResizeHandle::SW, 7}
    };

    for (const auto &p : table)
    {
        Vec2 hp = M.apply(handlesLocal[p.idx]);
        Vec2 d = world - hp;
        float d2 = d.x * d.x + d.y * d.y;
        if (d2 <= r2)
            return p.h;
    }
    return ResizeHandle::None;
}

void InteractionController::moveEntity(PageModel &scene, int id, const Vec2 &delta)
{
    scene.entities[id].localToPage.setTranslation(m_state.dragEntityStartTransform.translation() + delta);
}

void InteractionController::resizeEntity(PageModel &scene, int id, const Vec2 &world)
{
    Entity &e = scene.entities[id];
    const Mat3 &M0 = m_state.dragEntityStartTransform;

    // local deltas from anchor to handle
    Vec2 dh = Vec2(
        m_state.resizeHandleLocal.x - m_state.resizeAnchorLocal.x,
        m_state.resizeHandleLocal.y - m_state.resizeAnchorLocal.y);

    // avoid division by zero for edge handles
    const float eps = 1e-4f;
    bool affectX = std::abs(dh.x) > eps;
    bool affectY = std::abs(dh.y) > eps;

    // current target vector (page) from anchor to mouse
    Vec2 target = Vec2(world.x - m_state.resizeAnchorPage.x,
                       world.y - m_state.resizeAnchorPage.y);

    // initial scales
    Vec2 s0 = M0.scale();

    // compute candidate uniform scale factors along axes
    float kx = 1.0f;
    float ky = 1.0f;
    if (affectX)
        kx = (target.x / dh.x) / (s0.x == 0.0f ? 1.0f : s0.x);
    if (affectY)
        ky = (target.y / dh.y) / (s0.y == 0.0f ? 1.0f : s0.y);

    float k = 1.0f;
    if (affectX && affectY)
    {
        // choose axis with larger relative change to feel natural
        k = (std::abs(kx) > std::abs(ky)) ? kx : ky;
    }
    else if (affectX)
    {
        k = kx;
    }
    else if (affectY)
    {
        k = ky;
    }

    // clamp to avoid degeneracy
    const float minScale = 1e-3f;
    Vec2 scale = s0 * k;
    if (std::abs(scale.x) < minScale) scale.x = (scale.x < 0 ? -minScale : minScale);
    if (std::abs(scale.y) < minScale) scale.y = (scale.y < 0 ? -minScale : minScale);

    // new translation so anchor stays fixed
    Vec2 tPrime = Vec2(
        m_state.resizeAnchorPage.x - scale.x * m_state.resizeAnchorLocal.x,
        m_state.resizeAnchorPage.y - scale.y * m_state.resizeAnchorLocal.y);

    e.localToPage.setScale(scale);
    e.localToPage.setTranslation(tPrime);
}