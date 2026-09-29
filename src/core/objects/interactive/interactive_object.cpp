/**
 * =========================================================================================
 * @file core/objects/interactive/interactive_object.cpp
 * @brief Implementation of InteractiveObject Layer 3 Host Bridge
 * =========================================================================================
 */

#include "core/objects/interactive/interactive_object.hpp"
#include "core/engine/canvas_transform.hpp"
#include <cmath>
#include <algorithm>

namespace Folio {

// =============================================================================
// CONSTRUCTORS & DESTRUCTOR
// =============================================================================

InteractiveObject::InteractiveObject() {
    type = ObjectType::Video;
    worldX = 0.0;
    worldY = 0.0;
    worldWidth = 160.0;  // Default 160mm x 90mm (standard 16:9 card)
    worldHeight = 90.0;
    UpdateBounds();
}

InteractiveObject::InteractiveObject(double x, double y, double width, double height,
                                     std::unique_ptr<IInteractiveOverlay> overlay)
    : overlayInstance(std::move(overlay))
{
    type = ObjectType::Video;
    worldX = x;
    worldY = y;
    worldWidth = (width > 0.0) ? width : 160.0;
    worldHeight = (height > 0.0) ? height : 90.0;
    UpdateBounds();

    if (overlayInstance) {
        (void)overlayInstance->Initialize();
    }
}

InteractiveObject::~InteractiveObject() {
    if (overlayInstance) {
        overlayInstance->Shutdown();
        overlayInstance.reset();
    }
}

InteractiveObject::InteractiveObject(const InteractiveObject& other)
    : CanvasObject(other),
      isInteracting(false)
{
    // Clone spatial properties and type
    type = ObjectType::Video;
    worldX = other.worldX;
    worldY = other.worldY;
    worldWidth = other.worldWidth;
    worldHeight = other.worldHeight;
    transform = other.transform;
    bounds = other.bounds;
    zOrder = other.zOrder;
    opacity = other.opacity;
    isVisible = other.isVisible;
    isLocked = other.isLocked;
    isSelectable = other.isSelectable;
    isSelected = false; // Do not copy selection state
    isTemporary = other.isTemporary;

    // Duplicate overlay if copy support is provided by the bridge implementation
    if (other.overlayInstance) {
        overlayInstance = other.overlayInstance->CloneOverlay();
        if (overlayInstance) {
            (void)overlayInstance->Initialize();
        }
    }
}

InteractiveObject& InteractiveObject::operator=(const InteractiveObject& other) {
    if (this == &other) return *this;

    if (overlayInstance) {
        overlayInstance->Shutdown();
        overlayInstance.reset();
    }

    CanvasObject::operator=(other);
    isInteracting = false;
    type = ObjectType::Video;
    worldX = other.worldX;
    worldY = other.worldY;
    worldWidth = other.worldWidth;
    worldHeight = other.worldHeight;
    transform = other.transform;
    bounds = other.bounds;
    zOrder = other.zOrder;
    opacity = other.opacity;
    isVisible = other.isVisible;
    isLocked = other.isLocked;
    isSelectable = other.isSelectable;
    isSelected = false;
    isTemporary = other.isTemporary;

    if (other.overlayInstance) {
        overlayInstance = other.overlayInstance->CloneOverlay();
        if (overlayInstance) {
            (void)overlayInstance->Initialize();
        }
    }

    return *this;
}

// =============================================================================
// OVERLAYS & INTERACTION STATE
// =============================================================================

void InteractiveObject::SetOverlay(std::unique_ptr<IInteractiveOverlay> overlay) {
    if (overlayInstance) {
        overlayInstance->Shutdown();
    }
    overlayInstance = std::move(overlay);
    if (overlayInstance) {
        (void)overlayInstance->Initialize();
    }
}

void InteractiveObject::SetInteracting(bool interacting) noexcept {
    if (isInteracting == interacting) return;

    isInteracting = interacting;
    if (overlayInstance) {
        if (isInteracting) {
            overlayInstance->OnFocusGained();
        } else {
            overlayInstance->OnFocusLost();
        }
    }
}

// =============================================================================
// COORDINATE PROJECTION
// =============================================================================

OverlayRect InteractiveObject::ComputeScreenRect(const Viewport& vp) const {
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
     *   screenX      = round((worldX - cameraOriginX) * scale)
     *   screenY      = round((worldY - cameraOriginY) * scale)
     *   screenWidth  = round(worldWidth * scale)
     *   screenHeight = round(worldHeight * scale)
     */
    const double scale = (vp.pixelsPerMm > 0.0) ? (vp.pixelsPerMm * vp.zoom) : vp.zoom;
    const double cameraOriginX = vp.bounds.minX;
    const double cameraOriginY = vp.bounds.minY;

    const double screenX = (worldX - cameraOriginX) * scale;
    const double screenY = (worldY - cameraOriginY) * scale;
    const double screenW = worldWidth * scale;
    const double screenH = worldHeight * scale;

    return OverlayRect{
        static_cast<int>(std::round(screenX)),
        static_cast<int>(std::round(screenY)),
        static_cast<int>(std::round(std::max(0.0, screenW))),
        static_cast<int>(std::round(std::max(0.0, screenH)))
    };
}

// =============================================================================
// CANVASOBJECT OVERRIDES
// =============================================================================

void InteractiveObject::Render(BLContext& ctx, const Viewport& viewport) const {
    if (!isVisible) return;

    ctx.save();
    ctx.apply_transform(transform);

    if (opacity < 0.999f) {
        ctx.set_global_alpha(static_cast<double>(opacity));
    }

    /**
     * Layer 1 Static Pass:
     * Draws an underlying dark slate card placeholder with subtle rounded border.
     * This provides a solid backbuffer base on the static layer so that if the
     * Layer 3 overlay is hidden, loading, or moving across frames, the canvas
     * never presents an empty hole.
     */
    constexpr double cornerRadius = 6.0;

    // Dark slate card background (fill)
    ctx.set_fill_style(BLRgba32(0x18, 0x1C, 0x24, static_cast<uint8_t>(opacity * 240)));
    ctx.fill_round_rect(BLRoundRect(worldX, worldY, worldWidth, worldHeight, cornerRadius, cornerRadius));

    // Subtle slate border outline (stroke)
    ctx.set_stroke_style(BLRgba32(0x33, 0x3D, 0x4D, static_cast<uint8_t>(opacity * 255)));
    const double strokeScale = (viewport.zoom > 0.001) ? (1.0 / viewport.zoom) : 1.0;
    ctx.set_stroke_width(strokeScale);
    ctx.stroke_round_rect(BLRoundRect(worldX, worldY, worldWidth, worldHeight, cornerRadius, cornerRadius));

    ctx.restore();
}

std::unique_ptr<CanvasObject> InteractiveObject::Clone() const {
    return std::make_unique<InteractiveObject>(*this);
}

} // namespace Folio
