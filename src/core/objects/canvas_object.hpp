#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <blend2d/blend2d.h>
#include "core/spatial/aabb.hpp"
#include "core/spatial/aabb_utils.hpp"
#include "core/engine/gizmo_types.hpp"
#include "core/objects/canvas_context.hpp"

class CanvasTransform;
struct Viewport;

namespace Folio {
struct ContextMenuItem;
class IInteractiveOverlay;
}

/**
 * @brief Discriminated type tag for all canvas entities.
 */
enum class ObjectType : uint8_t {
    InkContainer,   
    Text,           
    Image,
    Video,
    PDF,
    Table,
    AttachmentFile,
    Shape,
    Connector,   
    Audio,
    Link,        
    MathLaTeX,     
    Frame,          // Grouped frame container
    Other           // Custom extension / third-party plugin objects
};

/**
 * @brief Abstract base class for all drawable canvas objects.
 * Operates strictly in physical world millimeters with no filesystem coupling.
 */
class CanvasObject {
public:
    std::string guuid = "";                             // Persistent UUID for database serialization
    std::string groupId = "";                           // UUID of parent logical group (empty if ungrouped)
    uint32_t uid = 0;                                   // Transient runtime ID matching spatial R-Tree

    ObjectType type = ObjectType::InkContainer;         // Object type discriminator
    
    // World-space coordinates and dimensions in physical millimeters
    double worldX      = 0.0;
    double worldY      = 0.0;
    double worldWidth  = 0.0;
    double worldHeight = 0.0;

    BLMatrix2D transform = BLMatrix2D::make_identity(); // Active affine manipulation matrix
    AABB bounds;                                        // Cached world-space bounding box for R-Tree queries

    int32_t zOrder = 1;                                 // Stacking order (higher = foreground)
    float opacity = 1.0f;                               // Global alpha multiplier [0.0, 1.0]

    // Packed state bitfields (1 byte total)
    uint8_t isVisible    : 1 = 1;  // False skips rendering passes
    uint8_t isLocked     : 1 = 0;  // True blocks selection and transforms
    uint8_t isSelectable : 1 = 1;  // Hit-test filter for interactive selection
    uint8_t isSelected   : 1 = 0;  // True displays gizmo handles and highlights
    uint8_t isTemporary  : 1 = 0;  // True skips document persistence (previews/guides)
    uint8_t reserved     : 3 = 0;  // Reserved alignment bits

    // Lifecycle & Rule of 5: ensure derived objects are copy-constructible for Clone()
    CanvasObject() = default;
    virtual ~CanvasObject() = default;
    CanvasObject(const CanvasObject&) = default;
    CanvasObject& operator=(const CanvasObject&) = default;

    // Checks if this object is bound to a parent logical group.
    [[nodiscard]] bool IsGrouped() const noexcept {
        return !groupId.empty();
    }

    // =========================================================================
    // LAYER 3 INTERACTIVE OVERLAYS
    // =========================================================================

    // Queries if this object hosts an interactive overlay widget.
    [[nodiscard]] virtual bool HasLiveOverlay() const noexcept { return false; }

    // Returns mutable pointer to the hosted Layer 3 overlay interface.
    [[nodiscard]] virtual Folio::IInteractiveOverlay* GetOverlay() noexcept { return nullptr; }

    // Returns const pointer to the hosted Layer 3 overlay interface.
    [[nodiscard]] virtual const Folio::IInteractiveOverlay* GetOverlay() const noexcept { return nullptr; }

    // =========================================================================
    // BOUNDS & SPATIAL
    // =========================================================================

    // Recalculates the world-space AABB incorporating the active transform.
    virtual void UpdateBounds() {
        bounds = Folio::AABBUtils::ComputeTransformedBounds(worldX, worldY, worldWidth, worldHeight, transform);
    }

    // Tests if a world-space point intersects the object's boundary.
    [[nodiscard]] virtual bool HitTest(double x, double y) const {
        return bounds.Contains(x, y);
    }

    // Tests if a world-space circle (stylus tip, touch, or eraser) intersects the object.
    [[nodiscard]] virtual bool HitTestCircle(double cx, double cy, double radiusMm) const {
        return Folio::AABBUtils::IntersectsCircle(bounds, cx, cy, radiusMm);
    }

    // Retrieves the cached world-space axis-aligned bounding box.
    [[nodiscard]] const AABB& GetAABB() const noexcept { return bounds; }

    // =========================================================================
    // GEOMETRY & TRANSFORMS
    // =========================================================================

    // Post-multiplies a transformation matrix and refreshes bounds.
    virtual void ApplyTransform(const BLMatrix2D& matrix) {
        transform.post_transform(matrix);
        UpdateBounds();
    }

    // Commits accumulated matrix into intrinsic dimensions and resets transform to identity.
    virtual void BakeTransform() {
        if (Folio::AABBUtils::BakeTransformedRect(worldX, worldY, worldWidth, worldHeight, transform)) {
            UpdateBounds();
        }
    }

    // =========================================================================
    // RENDERING & LIFECYCLE
    // =========================================================================

    // Draws the object to the target Blend2D context relative to viewport settings.
    virtual void Render(BLContext& ctx, const Viewport& viewport) const = 0;

    // Produces a deep polymorphic duplicate of this object.
    [[nodiscard]] virtual std::unique_ptr<CanvasObject> Clone() const = 0;

    // Returns the manipulation gizmo configuration (e.g. 8-point box, two-point).
    [[nodiscard]] virtual GizmoStyle GetGizmoStyle() const noexcept {
        return GizmoStyle::BoundingBox;
    }

    // Appends domain-specific actions into the right-click context menu.
    virtual void CustomizeActions(std::vector<Folio::ContextMenuItem>& actions) {}

    // =========================================================================
    // POLYMORPHIC INTERACTION HOOKS
    // =========================================================================

    /**
     * @brief Polymorphic pointer click interaction hook.
     *
     * GENERAL WORKING PROCESS:
     * Dispatched by input arbiters (such as InputStateMachine) when a pointer click
     * or double-click lands within this object's hit boundary. Derived objects override
     * this method to execute actions (toggling media playback, launching external attachments,
     * activating text editing) using the injected subsystems rather than requiring downward casts.
     *
     * @param ctx Unified dependency injection context containing injected managers and click telemetry.
     * @return true if the event was consumed and handled; false to fall through to default canvas selection.
     */
    virtual bool OnPointerClick(const Folio::CanvasContext& ctx) {
        return false;
    }
};

namespace Folio {
using ::CanvasObject;
using ::ObjectType;
}