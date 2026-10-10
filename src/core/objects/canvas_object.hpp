#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <blend2d/blend2d.h>
#include "core/spatial/aabb.hpp"
#include "core/spatial/aabb_utils.hpp"
#include "core/canvas_engine/gizmo/gizmo_types.hpp"
#include "core/canvas_engine/transform/canvas_transform.hpp"
#include "core/objects/object_config.hpp"

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
    uint32_t pageIndex = 0;                             // Document sequence index for stable painter's algorithm tie-breaking
    float opacity = 1.0f;                               // Global alpha multiplier [0.0, 1.0]
    GizmoStyle gizmoStyle = GizmoStyle::BoundingBox;    // Manipulation handle configuration (BoundingBox, TwoPoint, MoveOnly, None)

    // Packed state bitfields (1 byte total)
    uint8_t isVisible    : 1 = 1;  // False skips rendering passes
    uint8_t isLocked     : 1 = 0;  // True blocks selection and transforms
    uint8_t isSelectable : 1 = 1;  // Hit-test filter for interactive selection
    uint8_t isSelected   : 1 = 0;  // True displays gizmo handles and highlights
    uint8_t isTemporary  : 1 = 0;  // True skips document persistence (previews/guides)
    uint8_t reserved     : 3 = 0;  // Reserved alignment bits

    // Lifecycle & Rule of 5: ensure derived objects are copy-constructible for Clone()
    CanvasObject() = default;                            // 1. Default constructor
    virtual ~CanvasObject() = default;                   // 2. Virtual destructor
    CanvasObject(const CanvasObject&) = default;         // 3. Copy constructor
    CanvasObject& operator=(const CanvasObject&) = default; // 4. Copy assignment operator

    // Checks if this object is bound to a parent logical group.
    [[nodiscard]] bool IsGrouped() const noexcept {
        return !groupId.empty();
    }

    // =========================================================================
    // LAYER 3 INTERACTIVE OVERLAYS
    // =========================================================================

    // Queries if this object hosts an interactive overlay widget.
    [[nodiscard]] virtual bool HasLiveOverlay() const noexcept { return false; }
    [[nodiscard]] virtual bool SupportsOverlay() const noexcept { return HasLiveOverlay(); }

    // Returns mutable pointer to the hosted Layer 3 overlay interface.
    [[nodiscard]] virtual Folio::IInteractiveOverlay* GetOverlay() noexcept { return nullptr; }

    // Returns const pointer to the hosted Layer 3 overlay interface.
    [[nodiscard]] virtual const Folio::IInteractiveOverlay* GetOverlay() const noexcept { return nullptr; }

    /**
     * @brief Queries whether this canvas object is actively receiving interactive input.
     * @return true if currently focused for interactive input; false otherwise.
     */
    [[nodiscard]] virtual bool IsInteracting() const noexcept { return false; }

    /**
     * @brief Sets whether this canvas object is actively receiving interactive input.
     * @param interacting true to activate interactive input routing; false to deactivate.
     */
    virtual void SetInteracting(bool interacting) noexcept {}

    // =========================================================================
    // BOUNDS & SPATIAL QUERIES
    // =========================================================================

    /**
     * @brief Recalculates the world-space AABB incorporating the active affine transform.
     *
     * MATHEMATICAL PROCESS:
     * Transforms the four untransformed rectangle corners [ (worldX, worldY),
     * (worldX + worldWidth, worldY), (worldX + worldWidth, worldY + worldHeight),
     * (worldX, worldY + worldHeight) ] by `transform` (2x3 affine matrix), then computes
     * the axis-aligned envelope [minX, minY, maxX, maxY].
     */
    virtual void UpdateBounds() {
        bounds = Folio::AABBUtils::ComputeTransformedBounds(worldX, worldY, worldWidth, worldHeight, transform);
    }

    /**
     * @brief Tests if a world-space point (x, y) intersects the object's boundary.
     *
     * Default implementation performs an AABB bounding check. Derived classes with
     * non-rectangular geometry (e.g. SmartArrow, Shape, Ink) override this for tight geometry testing.
     *
     * @param x World X coordinate in millimeters.
     * @param y World Y coordinate in millimeters.
     * @return true if point is inside object boundary; false otherwise.
     */
    [[nodiscard]] virtual bool HitTest(double x, double y) const {
        return bounds.Contains(x, y);
    }

    /**
     * @brief Tests if a world-space circle (stylus tip, touch contact, or eraser reticle) intersects this object.
     *
     * MATHEMATICAL PROCESS:
     * Clamps circle center (cx, cy) to closest point on bounds [minX..maxX, minY..maxY],
     * then verifies if squared Euclidean distance <= radiusMm * radiusMm.
     *
     * @param cx Circle center X in millimeters.
     * @param cy Circle center Y in millimeters.
     * @param radiusMm Radius of intersection circle in millimeters.
     * @return true if circle overlaps or encloses any part of this object.
     */
    [[nodiscard]] virtual bool HitTestCircle(double cx, double cy, double radiusMm) const {
        return Folio::AABBUtils::IntersectsCircle(bounds, cx, cy, radiusMm);
    }

    /// @brief Retrieves the cached world-space axis-aligned bounding box.
    [[nodiscard]] const AABB& GetAABB() const noexcept { return bounds; }

    // =========================================================================
    // GEOMETRY & TRANSFORMS
    // =========================================================================

    /**
     * @brief Post-multiplies a transformation matrix and refreshes cached spatial bounds.
     *
     * MATHEMATICAL PROCESS:
     * Performs affine matrix multiplication: `transform = transform * matrix`.
     * Used during interactive gizmo manipulation (translation, scaling, rotation).
     *
     * @param matrix 2D affine transformation matrix to apply.
     */
    virtual void ApplyTransform(const BLMatrix2D& matrix) {
        transform.post_transform(matrix);
        UpdateBounds();
    }

    /**
     * @brief Commits accumulated affine transform into intrinsic dimensions and resets transform to identity.
     *
     * MATHEMATICAL PROCESS:
     * For pure translations and non-rotated scalings, updates (worldX, worldY, worldWidth, worldHeight)
     * directly and resets `transform` to BLMatrix2D::make_identity(). For rotated objects,
     * retains rotation in matrix to prevent skew distortion.
     */
    virtual void BakeTransform() {
        if (Folio::AABBUtils::BakeTransformedRect(worldX, worldY, worldWidth, worldHeight, transform)) {
            UpdateBounds();
        }
    }

    // =========================================================================
    // RENDERING & LIFECYCLE
    // =========================================================================

    /**
     * @brief Draws the object into the target Blend2D context relative to viewport settings.
     *
     * @param ctx Target Blend2D drawing context (e.g. Layer 1 baked layer buffer).
     * @param viewport Active canvas viewport supplying camera world-to-screen transforms and scale factors.
     */
    virtual void Render(BLContext& ctx, const Viewport& viewport) const = 0;

    /// @brief Produces a deep polymorphic duplicate of this object.
    [[nodiscard]] virtual std::unique_ptr<CanvasObject> Clone() const = 0;

    /// @brief Returns the manipulation gizmo configuration (e.g. 8-point box, two-point, move-only, none).
    [[nodiscard]] GizmoStyle GetGizmoStyle() const noexcept {
        return gizmoStyle;
    }

    /**
     * @brief Appends domain-specific action items into the right-click context menu.
     *
     * Derived objects override this to inject contextual operations (e.g., Relink File,
     * Open With Default App, Toggle Playback, Change Shape Type).
     *
     * @param[in,out] actions Menu action vector to append context items to.
     */
    virtual void CustomizeActions(std::vector<Folio::ContextMenuItem>& actions) {}

    // =========================================================================
    // CONFIGURATION & GLOBAL SETTINGS ACCESS
    // =========================================================================

    /**
     * @brief Direct zero-overhead access to centralized global settings, theme, and geometry constraints.
     * Synchronized bidirectionally with SettingsManager.
     * @return Const reference to active ObjectConfig singleton.
     */
    [[nodiscard]] static const Folio::ObjectConfig& GetConfig() noexcept {
        return Folio::ObjectConfig::Get();
    }

    /**
     * @brief Mutable accessor for configuration synchronization.
     * @return Mutable reference to active ObjectConfig singleton.
     */
    [[nodiscard]] static Folio::ObjectConfig& GetMutableConfig() noexcept {
        return Folio::ObjectConfig::Get();
    }
};

namespace Folio {
using ::CanvasObject;
using ::ObjectType;
}