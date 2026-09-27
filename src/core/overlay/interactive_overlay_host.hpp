/**
 * =========================================================================================
 * @file core/overlay/interactive_overlay_host.hpp
 * @brief Host Controller Coordinating Layer 3 Interactive Overlays in Render and Event Loops
 *
 * Architecture Context:
 *   InteractiveOverlayHost sits between the FolioNote main engine loop, the active camera
 *   frustum, and the canvas object collection.
 *
 *   Rendering Pipeline Integration:
 *     1. Static Baked Pass (Layer 1): CanvasEngine rebakes staticCanvasLayer if dirty.
 *     2. Transient Pass (Layer 2): Inking and gizmos composited onto compositeSurface.
 *     3. Layer 3 Overlays Pass: InteractiveOverlayHost::RenderOverlays renders live overlays
 *        directly onto the composite surface using projected screen-space coordinates.
 *
 *   Event Pipeline Integration:
 *     InteractiveOverlayHost::HandleInput intercepts SDL3 events before canvas panning,
 *     zooming, or gizmo manipulations occur if an interactive object has active focus.
 * =========================================================================================
 */

#pragma once

#include <vector>
#include <cstdint>
#include <blend2d/blend2d.h>
#include <SDL3/SDL.h>

#include "core/spatial/aabb.hpp"
#include "core/overlay/interactive_overlay.hpp"

class CanvasObject;

namespace Folio {

class InteractiveObject;

class InteractiveOverlayHost {
public:
    InteractiveOverlayHost() = default;
    ~InteractiveOverlayHost() = default;

    // Non-copyable host controller
    InteractiveOverlayHost(const InteractiveOverlayHost&) = delete;
    InteractiveOverlayHost& operator=(const InteractiveOverlayHost&) = delete;

    InteractiveOverlayHost(InteractiveOverlayHost&&) noexcept = default;
    InteractiveOverlayHost& operator=(InteractiveOverlayHost&&) noexcept = default;

    // =========================================================================
    // ENGINE TICK (UPDATE)
    // =========================================================================

    /**
     * @brief Advances overlay state and animations for all visible interactive objects in frustum.
     *
     * Working Process:
     *   1. Iterates only visible objects satisfying obj->HasLiveOverlay() == true.
     *   2. Dispatches OnUpdate(nowMs, deltaSec) to each object's overlayInstance.
     *   3. Bypasses objects outside the camera frustum to conserve CPU/GPU cycles.
     *
     * @param nowMs Monotonically increasing engine time in milliseconds.
     * @param deltaSec Elapsed time since previous tick in seconds.
     * @param visibleObjects Frustum-culled list of canvas objects visible in current viewport.
     */
    void UpdateActiveOverlays(uint64_t nowMs, double deltaSec,
                             const std::vector<CanvasObject*>& visibleObjects);
    void UpdateActiveOverlays(uint64_t nowMs, double deltaSec,
                             const std::vector<std::shared_ptr<CanvasObject>>& visibleObjects);

    // =========================================================================
    // RENDERING
    // =========================================================================

    /**
     * @brief Renders Layer 3 interactive overlays onto the final screen composite surface.
     *
     * Working Process:
     *   1. Iterates visible objects satisfying obj->HasLiveOverlay() == true.
     *   2. Computes each object's integer pixel screenRect using ComputeScreenRect(vp).
     *   3. Rejects empty or completely offscreen rectangles.
     *   4. Invokes overlayInstance->OnRenderOverlay(screenCtx, screenRect).
     *
     * @param screenCtx Blend2D rendering context targeting the final composite screen surface.
     * @param vp The active visible camera viewport.
     * @param visibleObjects Frustum-culled list of canvas objects visible in current viewport.
     */
    void RenderOverlays(BLContext& screenCtx, const Viewport& vp,
                        const std::vector<CanvasObject*>& visibleObjects);
    void RenderOverlays(BLContext& screenCtx, const Viewport& vp,
                        const std::vector<std::shared_ptr<CanvasObject>>& visibleObjects);

    // =========================================================================
    // INPUT DISPATCH
    // =========================================================================

    /**
     * @brief Routes SDL3 input events to the currently active interactive overlay.
     *
     * Input Isolation Contract:
     *   If activeObject is non-null, is an InteractiveObject, and has isInteracting == true:
     *   - Projects its world geometry to current screen pixel bounds.
     *   - Forwards the SDL_Event to overlayInstance->OnInputEvent().
     *   - Returns true if the event was consumed, preventing canvas gizmos, panning,
     *     or lasso selection from triggering.
     *
     * @param event The incoming SDL_Event from application event loop.
     * @param vp The active visible camera viewport.
     * @param activeObject The currently focused or candidate canvas object.
     * @return True if input was consumed by the interactive overlay; false otherwise.
     */
    [[nodiscard]] bool HandleInput(const SDL_Event& event, const Viewport& vp,
                                   CanvasObject* activeObject);

    // =========================================================================
    // FOCUS MANAGEMENT
    // =========================================================================

    /**
     * @brief Explicitly engages interactive mode on an InteractiveObject.
     * @param obj The interactive object to focus.
     */
    void SetFocusedObject(CanvasObject* obj);

    /**
     * @brief Clears active overlay interaction focus, returning input to canvas tools.
     */
    void ClearFocusedObject();

    /**
     * @brief Gets currently focused interactive object, or nullptr if none.
     */
    [[nodiscard]] CanvasObject* GetFocusedObject() const noexcept {
        return focusedObject;
    }

private:
    CanvasObject* focusedObject = nullptr;
};

} // namespace Folio
