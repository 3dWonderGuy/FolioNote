/**
 * =========================================================================================
 * @file core/objects/interactive/interactive_object.hpp
 * @brief CanvasObject Wrapper Hosting Layer 3 Interactive Overlays
 *
 * Architecture Context:
 *   InteractiveObject bridges the static 2D canvas spatial system (R-tree spatial indexing,
 *   world-space coordinate storage in millimeters, undo/redo, gizmo bounding boxes) with the
 *   dynamic Layer 3 interactive overlay subsystem (video decoders, emulators, web views).
 *
 *   - Layer 1 (Static Pass): Renders an underlying dark slate card placeholder to guarantee
 *     the canvas has visual feedback when the overlay is loading or dormant.
 *   - Layer 2 (Transient Pass): Standard canvas selection gizmos appear around the object when
 *     isInteracting == false, permitting drag/resize/rotation without affecting overlay internals.
 *   - Layer 3 (Live Overlay): Host renders the live overlay directly at projected screen pixels
 *     and forwards input events when isInteracting == true.
 * =========================================================================================
 */

#pragma once

#include "core/objects/canvas_object.hpp"
#include "core/overlay/interactive_overlay.hpp"

namespace Folio {

class InteractiveObject : public CanvasObject {
public:
    /// Flag controlling whether mouse/touch/keyboard events route to the overlay (true)
    /// or to the standard canvas manipulation gizmos and selection system (false).
    bool isInteracting = false;

    /// Owned concrete overlay subsystem instance (bridge pattern).
    std::unique_ptr<IInteractiveOverlay> overlayInstance = nullptr;

    // =========================================================================
    // CONSTRUCTORS & DESTRUCTOR
    // =========================================================================

    /**
     * @brief Constructs an InteractiveObject at origin with default 160mm x 90mm (16:9) size.
     */
    InteractiveObject();

    /**
     * @brief Constructs an InteractiveObject with explicit world bounds and an optional overlay.
     * @param x Top-left X in world millimeters.
     * @param y Top-left Y in world millimeters.
     * @param width Extent width in world millimeters.
     * @param height Extent height in world millimeters.
     * @param overlay Optional unique_ptr to an IInteractiveOverlay implementation.
     */
    InteractiveObject(double x, double y, double width, double height,
                      std::unique_ptr<IInteractiveOverlay> overlay = nullptr);

    ~InteractiveObject() override;

    // Copying transfers spatial metadata; overlay duplication delegates to CloneOverlay()
    InteractiveObject(const InteractiveObject& other);
    InteractiveObject& operator=(const InteractiveObject& other);

    // Standard moves
    InteractiveObject(InteractiveObject&&) noexcept = default;
    InteractiveObject& operator=(InteractiveObject&&) noexcept = default;

    // =========================================================================
    // OVERLAYS & INTERACTION STATE
    // =========================================================================

    /**
     * @brief Overrides CanvasObject::HasLiveOverlay to signal presence of Layer 3 subsystem.
     */
    [[nodiscard]] bool HasLiveOverlay() const noexcept override {
        return overlayInstance != nullptr;
    }

    /**
     * @brief Returns pointer to the owned overlay instance (IInteractiveOverlay bridge).
     */
    [[nodiscard]] IInteractiveOverlay* GetOverlay() noexcept override {
        return overlayInstance.get();
    }

    [[nodiscard]] const IInteractiveOverlay* GetOverlay() const noexcept override {
        return overlayInstance.get();
    }

    /**
     * @brief Replaces or sets the active overlay subsystem, safely shutting down any previous one.
     * @param overlay New overlay subsystem instance.
     */
    void SetOverlay(std::unique_ptr<IInteractiveOverlay> overlay);

    /**
     * @brief Sets whether user input is currently captured by the overlay.
     * @param interacting True to engage overlay input capture; false for canvas gizmo manipulation.
     */
    void SetInteracting(bool interacting) noexcept;

    /**
     * @brief Checks if user input is currently directed to the overlay.
     */
    [[nodiscard]] bool IsInteracting() const noexcept {
        return isInteracting;
    }

    // =========================================================================
    // COORDINATE PROJECTION HELPERS
    // =========================================================================

    /**
     * @brief Projects the object's world-space bounds (in mm) into integer screen pixel coordinates.
     *
     * Mathematical Derivation:
     *   World coordinates: (worldX, worldY) top-left in mm, (worldWidth, worldHeight) extents.
     *   Camera offset:     cameraX = vp.bounds.minX, cameraY = vp.bounds.minY.
     *   Screen density:    scale = (vp.pixelsPerMm > 0) ? (vp.pixelsPerMm * vp.zoom) : vp.zoom.
     *
     *   Equations:
     *     screenX      = round((worldX - cameraX) * scale)
     *     screenY      = round((worldY - cameraY) * scale)
     *     screenWidth  = round(worldWidth * scale)
     *     screenHeight = round(worldHeight * scale)
     *
     * @param vp The active visible camera viewport containing camera bounds and zoom factor.
     * @return OverlayRect containing screen pixel integer coordinates [x, y, width, height].
     */
    [[nodiscard]] OverlayRect ComputeScreenRect(const Viewport& vp) const;

    // =========================================================================
    // CANVASOBJECT OVERRIDES
    // =========================================================================

    /**
     * @brief Layer 1 Static Pass: Renders an underlying dark slate card placeholder with a subtle
     * rounded border so the canvas is never blank if the overlay is hidden, unloaded, or loading.
     *
     * @param ctx Blend2D rendering context target (static baked layer).
     * @param viewport Active camera viewport.
     */
    void Render(BLContext& ctx, const Viewport& viewport) const override;

    /**
     * @brief Creates a deep copy of this interactive object.
     */
    [[nodiscard]] std::unique_ptr<CanvasObject> Clone() const override;

    /**
     * @brief Specifies bounding box gizmo for spatial manipulation when isInteracting == false.
     */
    [[nodiscard]] GizmoStyle GetGizmoStyle() const noexcept override {
        return GizmoStyle::BoundingBox;
    }
};

} // namespace Folio
