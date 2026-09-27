/**
 * =========================================================================================
 * @file core/overlay/interactive_overlay_host.cpp
 * @brief Implementation of InteractiveOverlayHost Controller
 * =========================================================================================
 */

#include "core/overlay/interactive_overlay_host.hpp"
#include "core/objects/interactive/interactive_object.hpp"

namespace Folio {

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

        // Bridge to InteractiveObject for coordinate projection
        InteractiveObject* io = nullptr;
        if (obj->type == ObjectType::Interactive) {
            io = static_cast<InteractiveObject*>(obj);
        } else {
            io = dynamic_cast<InteractiveObject*>(obj);
        }

        if (!io) continue;

        const OverlayRect screenRect = io->ComputeScreenRect(vp);
        if (screenRect.IsEmpty()) {
            continue;
        }

        IInteractiveOverlay* overlay = io->GetOverlay();
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

        InteractiveObject* io = nullptr;
        if (obj->type == ObjectType::Interactive) {
            io = static_cast<InteractiveObject*>(obj.get());
        } else {
            io = dynamic_cast<InteractiveObject*>(obj.get());
        }

        if (!io) continue;

        const OverlayRect screenRect = io->ComputeScreenRect(vp);
        if (screenRect.IsEmpty()) {
            continue;
        }

        IInteractiveOverlay* overlay = io->GetOverlay();
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

    InteractiveObject* io = nullptr;
    if (activeObject->type == ObjectType::Interactive) {
        io = static_cast<InteractiveObject*>(activeObject);
    } else {
        io = dynamic_cast<InteractiveObject*>(activeObject);
    }

    if (!io || !io->isInteracting) {
        return false;
    }

    IInteractiveOverlay* overlay = io->GetOverlay();
    if (!overlay) {
        return false;
    }

    const OverlayRect screenRect = io->ComputeScreenRect(vp);
    return overlay->OnInputEvent(event, screenRect);
}

// =============================================================================
// FOCUS MANAGEMENT
// =============================================================================

void InteractiveOverlayHost::SetFocusedObject(CanvasObject* obj) {
    if (focusedObject == obj) return;

    ClearFocusedObject();

    if (obj && obj->HasLiveOverlay()) {
        InteractiveObject* io = (obj->type == ObjectType::Interactive)
            ? static_cast<InteractiveObject*>(obj)
            : dynamic_cast<InteractiveObject*>(obj);

        if (io) {
            focusedObject = obj;
            io->SetInteracting(true);
        }
    }
}

void InteractiveOverlayHost::ClearFocusedObject() {
    if (!focusedObject) return;

    if (focusedObject->HasLiveOverlay()) {
        InteractiveObject* io = (focusedObject->type == ObjectType::Interactive)
            ? static_cast<InteractiveObject*>(focusedObject)
            : dynamic_cast<InteractiveObject*>(focusedObject);

        if (io) {
            io->SetInteracting(false);
        }
    }

    focusedObject = nullptr;
}

} // namespace Folio
