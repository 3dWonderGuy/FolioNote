/**
 * =========================================================================================
 * @file core/overlay/interactive_overlay_host.cpp
 * @brief Implementation of InteractiveOverlayHost Controller
 * =========================================================================================
 */

#include "core/overlay/interactive_overlay_host.hpp"
#include "core/objects/canvas_object.hpp"

namespace Folio {

// =============================================================================
// COORDINATE PROJECTION
// =============================================================================

/**
 * Mathematical Projection from World Millimeters to Screen Pixels:
 *
 * In FolioNote, the camera viewport defined by vp.bounds stores the visible
 * window's top-left coordinates in world space (minX, minY):
 *   cameraOriginX = vp.bounds.minX
 *   cameraOriginY = vp.bounds.minY
 *
 * Screen resolution density:
 *   scale = (vp.pixelsPerMm > 0.0) ? (vp.pixelsPerMm * vp.zoom) : vp.zoom;
 *
 * Screen projection formulas:
 *   screenX      = round((obj.worldX - cameraOriginX) * scale)
 *   screenY      = round((obj.worldY - cameraOriginY) * scale)
 *   screenWidth  = round(obj.worldWidth * scale)
 *   screenHeight = round(obj.worldHeight * scale)
 *
 * @param obj CanvasObject providing world physical position and dimensions.
 * @param vp Viewport providing camera bounds and zoom factor.
 * @return Screen-space integer pixel rectangle for overlay alignment.
 */
OverlayRect InteractiveOverlayHost::ComputeScreenRect(const CanvasObject& obj, const Viewport& vp) noexcept {
    const double scale = (vp.pixelsPerMm > 0.0) ? (vp.pixelsPerMm * vp.zoom) : vp.zoom;
    const double cameraOriginX = vp.bounds.minX;
    const double cameraOriginY = vp.bounds.minY;

    const double screenX = (obj.worldX - cameraOriginX) * scale;
    const double screenY = (obj.worldY - cameraOriginY) * scale;
    const double screenW = obj.worldWidth * scale;
    const double screenH = obj.worldHeight * scale;

    return OverlayRect{
        static_cast<int>(std::round(screenX)),
        static_cast<int>(std::round(screenY)),
        static_cast<int>(std::round(std::max(0.0, screenW))),
        static_cast<int>(std::round(std::max(0.0, screenH)))
    };
}

// =============================================================================
// ENGINE TICK (UPDATE)
// =============================================================================

void InteractiveOverlayHost::UpdateActiveOverlays(uint64_t nowMs, double deltaSec,
                                                 const std::vector<CanvasObject*>& visibleObjects)
{
    /**
     * Working Process:
     *   Frustum-culled visibility check: only objects within the current camera frustum
     *   that own an active Layer 3 overlay subsystem receive frame tick updates.
     */
    for (CanvasObject* obj : visibleObjects) {
        if (!obj || !obj->isVisible || !obj->HasLiveOverlay()) {
            continue;
        }

        IInteractiveOverlay* overlay = obj->GetOverlay();
        if (overlay) {
            overlay->OnUpdate(nowMs, deltaSec);
        }
    }
}

void InteractiveOverlayHost::UpdateActiveOverlays(uint64_t nowMs, double deltaSec,
                                                 const std::vector<std::shared_ptr<CanvasObject>>& visibleObjects)
{
    for (const auto& obj : visibleObjects) {
        if (!obj || !obj->isVisible || !obj->HasLiveOverlay()) {
            continue;
        }

        IInteractiveOverlay* overlay = obj->GetOverlay();
        if (overlay) {
            overlay->OnUpdate(nowMs, deltaSec);
        }
    }
}

// =============================================================================
// RENDERING
// =============================================================================

void InteractiveOverlayHost::RenderOverlays(BLContext& screenCtx, const Viewport& vp,
                                            const std::vector<CanvasObject*>& visibleObjects)
{
    /**
     * Layer 3 Live Presentation Pass:
     *   Renders active overlays onto the screen composite context.
     *   Screen coordinates are projected using ComputeScreenRect(vp), ensuring
     *   pixel-perfect registration over the Layer 1 static placeholder card.
     */
    for (CanvasObject* obj : visibleObjects) {
        if (!obj || !obj->isVisible || !obj->HasLiveOverlay()) {
            continue;
        }

        const OverlayRect screenRect = ComputeScreenRect(*obj, vp);
        if (screenRect.IsEmpty()) {
            continue;
        }

        IInteractiveOverlay* overlay = obj->GetOverlay();
        if (overlay) {
            overlay->OnRenderOverlay(screenCtx, screenRect);
        }
    }
}

void InteractiveOverlayHost::RenderOverlays(BLContext& screenCtx, const Viewport& vp,
                                            const std::vector<std::shared_ptr<CanvasObject>>& visibleObjects)
{
    for (const auto& obj : visibleObjects) {
        if (!obj || !obj->isVisible || !obj->HasLiveOverlay()) {
            continue;
        }

        const OverlayRect screenRect = ComputeScreenRect(*obj, vp);
        if (screenRect.IsEmpty()) {
            continue;
        }

        IInteractiveOverlay* overlay = obj->GetOverlay();
        if (overlay) {
            overlay->OnRenderOverlay(screenCtx, screenRect);
        }
    }
}

// =============================================================================
// INPUT DISPATCH
// =============================================================================

bool InteractiveOverlayHost::HandleInput(const SDL_Event& event, const Viewport& vp,
                                         CanvasObject* activeObject)
{
    if (!activeObject || !activeObject->HasLiveOverlay()) {
        return false;
    }

    if (!activeObject->IsInteracting()) {
        return false;
    }

    IInteractiveOverlay* overlay = activeObject->GetOverlay();
    if (!overlay) {
        return false;
    }

    const OverlayRect screenRect = ComputeScreenRect(*activeObject, vp);
    return overlay->OnInputEvent(event, screenRect);
}

// =============================================================================
// FOCUS MANAGEMENT
// =============================================================================

void InteractiveOverlayHost::SetFocusedObject(CanvasObject* obj) {
    if (focusedObject == obj) return;

    ClearFocusedObject();

    if (obj && obj->HasLiveOverlay()) {
        focusedObject = obj;
        focusedObject->SetInteracting(true);
    }
}

void InteractiveOverlayHost::ClearFocusedObject() {
    if (!focusedObject) return;

    if (focusedObject->HasLiveOverlay()) {
        focusedObject->SetInteracting(false);
    }

    focusedObject = nullptr;
}

} // namespace Folio
