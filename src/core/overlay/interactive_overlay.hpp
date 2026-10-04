/**
 * =========================================================================================
 * @file core/overlay/interactive_overlay.hpp
 * @brief Core Interface and Geometry Types for FolioNote Layer 3 Interactive Overlays
 *
 * Architecture Context:
 *   FolioNote utilizes a decoupled 3-layer rendering engine:
 *   +-----------------------------------------------------------------------------------+
 *   | Layer 3: Live Interactive Overlays (UI widgets, video players, web views, 3D)     |
 *   |          Self-contained, thread-isolated subsystems anchored to canvas world mm.  |
 *   +-----------------------------------------------------------------------------------+
 *   | Layer 2: In-Flight Transient Layer (live stylus inking strokes, transform gizmos) |
 *   |          Dynamic real-time raster overlays updated during active user gestures.   |
 *   +-----------------------------------------------------------------------------------+
 *   | Layer 1: Static Baked Backbuffer (strokes, text, shapes, static image assets)     |
 *   |          Offscreen cached bitmap rasterized only when content geometry changes.   |
 *   +-----------------------------------------------------------------------------------+
 *
 * Separation of Concerns:
 *   CanvasObject instances retain spatial geometry and lightweight metadata in world mm.
 *   Heavy runtime state, hardware decoding handles, input event consumption, and sub-frame
 *   tick updates belong exclusively to IInteractiveOverlay implementations. Layer 3 updates
 *   run each display tick without invalidating or forcing Layer 1 to rebake.
 * =========================================================================================
 */

#pragma once

#include <cstdint>
#include <memory>
#include <blend2d/blend2d.h>
#include <SDL3/SDL.h>

namespace Folio {

/**
 * @brief Screen-space axis-aligned pixel bounding rectangle for overlay presentation.
 *
 * Mathematical Projection:
 *   Given an object with world coordinates (worldX, worldY, worldWidth, worldHeight) in mm,
 *   and a camera Viewport with camera offset (cameraX = minX, cameraY = minY) and zoom,
 *   the projected screen pixel coordinates are computed as:
 *
 *     scale       = pixelsPerMm * zoom
 *     screenX     = round((worldX - cameraX) * scale)
 *     screenY     = round((worldY - cameraY) * scale)
 *     screenWidth = round(worldWidth * scale)
 *     screenHeight= round(worldHeight * scale)
 */
struct OverlayRect {
    int x = 0;       ///< Screen pixel X coordinate of top-left corner
    int y = 0;       ///< Screen pixel Y coordinate of top-left corner
    int width = 0;   ///< Extent width in screen pixels
    int height = 0;  ///< Extent height in screen pixels

    /**
     * @brief Tests if screen pixel coordinates lie within this rectangle.
     * @param px Screen X coordinate in integer pixels.
     * @param py Screen Y coordinate in integer pixels.
     * @return True if (px, py) is contained within [x, x + width) and [y, y + height).
     */
    [[nodiscard]] constexpr bool Contains(int px, int py) const noexcept {
        return px >= x && px < (x + width) &&
               py >= y && py < (y + height);
    }

    /**
     * @brief Returns true if the rectangle has zero or negative area.
     */
    [[nodiscard]] constexpr bool IsEmpty() const noexcept {
        return width <= 0 || height <= 0;
    }
};

/**
 * @brief Abstract bridge interface for interactive Layer 3 overlay subsystems.
 *
 * Concrete implementations (video players, animated GIFs, emulators, web views, 3D viewports)
 * implement this contract to receive tick updates, render directly onto the screen composite
 * context using projected screen coordinates, and intercept input events when interacting.
 */
class IInteractiveOverlay {
public:
    virtual ~IInteractiveOverlay() = default;

    // =========================================================================
    // LIFECYCLE
    // =========================================================================

    /**
     * @brief Initializes subsystem resources, GPU textures, hardware contexts, or worker threads.
     * @return True if initialization succeeded; false on failure.
     */
    [[nodiscard]] virtual bool Initialize() = 0;

    /**
     * @brief Releases all allocated subsystem resources, decoding pipelines, and threads.
     */
    virtual void Shutdown() = 0;

    // =========================================================================
    // ENGINE TICK & RENDERING
    // =========================================================================

    /**
     * @brief Advances overlay state, physics, media playback, or animation frames.
     *
     * Performance Constraint:
     *   MUST NOT perform heap allocations during OnUpdate(). Pre-allocate all buffers.
     *
     * @param nowMs Current monotonically increasing engine timestamp in milliseconds.
     * @param deltaTime Elapsed duration since last frame tick in fractional seconds.
     */
    virtual void OnUpdate(uint64_t nowMs, double deltaTime) = 0;

    /**
     * @brief Renders the overlay onto the screen-space composite context.
     *
     * General Working Process:
     *   1. The host projects the parent CanvasObject's world coordinates into screenRect.
     *   2. Screen-space clipping or rounded rectangles are applied if necessary.
     *   3. The overlay renders its visual surface (hardware image blit, vector drawing, UI).
     *
     * Performance Constraint:
     *   MUST NOT perform dynamic memory allocations during OnRenderOverlay().
     *
     * @param ctx Blend2D rendering context targeting the final composite screen surface.
     * @param screenRect Projected screen pixel bounds where the overlay should appear.
     */
    virtual void OnRenderOverlay(BLContext& ctx, const OverlayRect& screenRect) = 0;

    // =========================================================================
    // INPUT ROUTING
    // =========================================================================

    /**
     * @brief Routes raw SDL3 input events to the overlay when active user interaction is engaged.
     *
     * Input Isolation:
     *   When a CanvasObject is in `isInteracting == true` mode, incoming mouse, touch,
     *   pen, and keyboard events are directed here first. If this method returns true,
     *   the event is marked as consumed and will NOT trigger canvas panning, zooming,
     *   selection changes, or gizmo manipulation.
     *
     * @param event The SDL_Event received from the operating system event queue.
     * @param screenRect Current screen pixel bounds of the parent object.
     * @return True if the overlay consumed the event; false to bubble to default canvas handling.
     */
    [[nodiscard]] virtual bool OnInputEvent(const SDL_Event& event, const OverlayRect& screenRect) = 0;

    // =========================================================================
    // OPTIONAL SUBSYSTEM HOOKS
    // =========================================================================

    /**
     * @brief Called when the user focuses or begins direct interaction with this overlay.
     */
    virtual void OnFocusGained() {}

    /**
     * @brief Called when interaction ends or user selects another object or canvas tool.
     */
    virtual void OnFocusLost() {}

    /**
     * @brief Called when the parent object enters or exits the camera viewport frustum.
     * @param isVisible True if currently intersecting the camera frustum; false if culled.
     */
    virtual void OnVisibilityChanged(bool isVisible) {
        (void)isVisible;
    }

    /**
     * @brief Optional clone hook for duplicating overlay state when copying canvas objects.
     * @return A newly allocated clone of the overlay, or nullptr if duplication is unsupported.
     */
    [[nodiscard]] virtual std::unique_ptr<IInteractiveOverlay> CloneOverlay() const {
        return nullptr;
    }
};

} // namespace Folio
