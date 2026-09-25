#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <blend2d/blend2d.h>
#include "core/spatial/aabb.hpp"
#include "core/spatial/aabb_utils.hpp"
#include "core/engine/gizmo_types.hpp"
#include "core/engine/stroke_smoother.hpp"

class CanvasTransform;


/**
 * @brief Enumeration of all possible object types in the canvas.
 */
enum class ObjectType {
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
    Frame,          // grouped frame container
    Other,          // this is the mark for the custom objects made by third party plugins
};


/**
 * @brief Base class for all drawable objects on the canvas. This class provides a common interface for different types of objects,
 * such as ink strokes, text boxes, and images.
 */
class CanvasObject {
public:
    std::string guuid = "";                             // to have searchable text and prevent user to user collisions and for persistent storage id (ink is not tracked)
    std::string groupId = "";                           // UUID of parent logical group (empty if ungrouped)
    uint32_t uid = 0;                                   // Matches RTree UID index (deleted upon closing notebook)

    ObjectType type = ObjectType::InkContainer;         // object type
    
    // Object position and size (disk saved)
    double worldX      = 0.0;                           // Top-left X coordinate in world millimeters
    double worldY      = 0.0;                           // Top-left Y coordinate in world millimeters
    double worldWidth  = 0.0;                           // Extent width in world millimeters
    double worldHeight = 0.0;                           // Extent height in world millimeters
    // used for the interactive manipulation in the screen
    BLMatrix2D transform = BLMatrix2D::make_identity(); // Used for transformation

    AABB bounds;                                        // Cached World-space AABB

    int32_t zOrder = 1;                                 // Draw order (higher = front, 0 = background)
    float opacity = 1.0f;                               // Alpha scalar

    // Packed state bitfields (1 byte total)
    uint8_t isVisible    : 1 = 1;  // Soft-visibility flag (skips rendering without spatial eviction)
    uint8_t isLocked     : 1 = 0;  // Modification guard against transforms/deletion
    uint8_t isSelectable : 1 = 1;  // Interactive hit-test filter
    uint8_t isSelected   : 1 = 0;  // Selection highlight & bounding box grip trigger
    uint8_t isTemporary  : 1 = 0;  // Transient guide / preview stroke
    uint8_t reserved     : 3 = 0;  // Reserved for future use

    virtual ~CanvasObject() = default;                  // Virtual destructor for proper cleanup of derived classes

    /**
     * @brief Returns true if this object belongs to a logical group.
     */
    [[nodiscard]] bool IsGrouped() const noexcept {
        return !groupId.empty();
    }

    /********************************************* */
    // Bounds & Spatial
    /********************************************* */

    /**
     * @brief Takes what ever the size of current object is and saves it as axis-aligned aabb bounds
     */
    virtual void UpdateBounds() {
        bounds = Folio::AABBUtils::ComputeTransformedBounds(worldX, worldY, worldWidth, worldHeight, transform);
    }

    /**
     * @brief Tests if a single 2D world-space point intersects the object.
     * @param worldX World X coordinate in millimeters.
     * @param worldY World Y coordinate in millimeters.
     * @return True if the point lies inside or on the object's active boundary.
     */
    virtual bool HitTest(double worldX, double worldY) const {
        return bounds.Contains(worldX, worldY); // check if the point is inside the bounds of the object
    }

    /**
    * @brief Tests if a world-space circle (stylus tip, touch point, or eraser)
    * intersects the object's boundary.
    *
    * @param worldX Circle center X in millimeters.
    * @param worldY Circle center Y in millimeters.
    * @param radiusMm Circle radius in millimeters.
    */
    virtual bool HitTestCircle(double worldX, double worldY, double radiusMm) const {
        return Folio::AABBUtils::IntersectsCircle(bounds, worldX, worldY, radiusMm);
    }

    /**
     * @brief Gets the cached world-space axis-aligned bounding box.
     */
    [[nodiscard]] const AABB& GetAABB() const noexcept { return bounds; }

    /********************************************* */
    // Geometry & Transforms
    /********************************************* */

    /**
     * @brief Applies a 2D affine transformation matrix to this object.
     *
     * @param matrix 2D affine transformation matrix.
     */
    virtual void ApplyTransform(const BLMatrix2D& matrix) {
        transform.post_transform(matrix);
        UpdateBounds();
    }

    /**
     * @brief Bakes the accumulated affine transform matrix into the object's intrinsic geometry.
     *
     * General Process:
     *   Called upon completion of an interactive manipulation (e.g. mouse release after gizmo drag/rotation).
     *   For standard rectangular objects, delegates to Folio::AABBUtils::BakeTransformedRect to commit
     *   scale and translation into (worldX, worldY, worldWidth, worldHeight) and reset transform to identity.
     *   Objects with custom geometry (InkContainer, SmartArrowObject) override this.
     */
    virtual void BakeTransform() {
        if (Folio::AABBUtils::BakeTransformedRect(worldX, worldY, worldWidth, worldHeight, transform)) {
            UpdateBounds();
        }
    }

    /********************************************* */
    // Rendering
    /********************************************* */

    /**
     * @brief Different object require different type of rendering
     *
     * General Process:
     *   1. Cull test: verifies that object `bounds` intersects `viewport.bounds`.
     *   2. Applies object opacity and layer blending if applicable.
     *   3. Renders vector paths, text layout, or image bitmaps into the target context.
     *
     * @param ctx Blend2D rendering context target.
     * @param viewport Current visible camera viewport and zoom scale.
     */
    virtual void Render(BLContext& ctx, const Viewport& viewport) const = 0;

    /********************************************* */
    // Duplication & Persistence
    /********************************************* */

    virtual std::unique_ptr<CanvasObject> Clone() const = 0;

    /********************************************* */
    // Selection & Gizmo Interaction
    /********************************************* */

    /**
     * @brief Returns the locked interaction gizmo style for this object.
     * Default is GizmoStyle::BoundingBox (8 resize grips + rotation knob).
     * Derived objects select their locked gizmo style (e.g. TwoPoint, MoveOnly, None).
     *
     * @return GizmoStyle interaction mode enum
     */
    virtual GizmoStyle GetGizmoStyle() const noexcept {
        return GizmoStyle::BoundingBox;
    }
};