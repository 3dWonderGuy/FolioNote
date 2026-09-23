#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <blend2d/blend2d.h>
#include "core/spatial/aabb.hpp"
#include "core/engine/gizmo_types.hpp"
#include "core/engine/stroke_smoother.hpp"

class CanvasTransform;


/**
 * @brief Enumeration of all possible object types in the canvas.
 *
 * Each entry maps 1:1 to a concrete class in core/objects/:
 *   InkContainer   → ink_container.hpp         (pen/stylus/mouse strokes)
 *   Text           → text/text_box.hpp          (rich text box)
 *   Image          → image_container.hpp        (raster image)
 *   Video          → media/video_container.hpp  (video file or YouTube embed)
 *   PDF            → pdf_container.hpp          (PDF page view)
 *   Table          → table_container.hpp        (row/column table — Phase 2)
 *   AttachmentFile → attachment_container.hpp   (linked external file chip)
 *   Shape          → primitives/shape_container.hpp (closed 2D vector shape)
 *   Connector      → connectors/smart_arrow_container.hpp (line/arrow with endpoint handles)
 *   Audio          → media/audio_container.hpp  (audio file link chip)
 *   Link           → links/link_object.hpp      (URL or cross-note anchor chip)
 *   MathLaTeX      → (future: LaTeX equation renderer)
 *   Frame          → (future: grouped frame container)
 *
 * Scalability: add new types here and in binary_serializer.hpp dispatch.
 * No changes to the engine dispatch loop are needed — it is polymorphic.
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
    Connector,     ///< SmartArrowObject — 2-point line/arrow connector
    Audio,
    Link,          ///< LinkObject — URL or folio:// cross-note anchor chip
    MathLaTeX,
    Frame,
};


/**
 * @brief Base class for all drawable objects on the canvas. This class provides a common interface for different types of objects,
 * such as ink strokes, text boxes, and images.
 */
class CanvasObject {
public:
    std::string guuid = "";                              // to have searchable text and prevent user to user collisions and for persistent storage id (ink is not tracked)
    std::string groupId = "";                            // UUID of parent logical group (empty if ungrouped)
    uint32_t uid = 0;                                   // Matches RTree UID index (deleted upon closing notebook)
    ObjectType type = ObjectType::InkContainer;         // Fast type discriminator
    AABB bounds;                                        // Cached World-space AABB
    BLMatrix2D transform = BLMatrix2D::make_identity();  // Local-to-world affine transform
    int32_t zOrder = 1;                                 // Draw order (higher = front, 0 = background)
    float opacity = 1.0f;                               // Alpha scalar (0.0 to 1.0)

    // Packed state bitfields (1 byte total)
    uint8_t isVisible    : 1 = 1;  // Soft-visibility flag (skips rendering without spatial eviction)
    uint8_t isLocked     : 1 = 0;  // Modification guard against transforms/deletion
    uint8_t isSelectable : 1 = 1;  // Interactive hit-test filter
    uint8_t isSelected   : 1 = 0;  // Selection highlight & bounding box grip trigger
    uint8_t isTemporary  : 1 = 0;  // Transient guide / preview stroke
    uint8_t reserved     : 3 = 0;  // Reserved for future use

    // forward decleration

    class Serializer;
    class Deserializer;
    
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
     * @brief Computes and updates the object's axis-aligned bounding box (bounds) in world coordinates.
     */
    virtual void UpdateBounds() = 0;

    /**
     * @brief Tests if a single 2D world-space point intersects the object.
     * @param worldX World X coordinate in millimeters.
     * @param worldY World Y coordinate in millimeters.
     * @return True if the point lies inside or on the object's active boundary.
     */
    virtual bool HitTest(double worldX, double worldY) const = 0;

    /**
     * @brief Tests if a world-space circle (stylus tip, finger touch, or eraser point)
     * intersects or contains this object.
     *
     * Mathematical Process:
     *   1. Broad phase: Query AABB expanded by radiusMm against bounds.
     *   2. Narrow phase: Check center point via HitTest(worldX, worldY).
     *   3. Clamped distance check: Find closest point Q on object's AABB to center C:
     *        Q_x = clamp(worldX, bounds.minX, bounds.maxX)
     *        Q_y = clamp(worldY, bounds.minY, bounds.maxY)
     *        distSq = (worldX - Q_x)^2 + (worldY - Q_y)^2
     *      Returns true if distSq <= radiusMm^2.
     *
     * @param worldX Circle center X coordinate in millimeters.
     * @param worldY Circle center Y coordinate in millimeters.
     * @param radiusMm Detection radius in millimeters.
     * @return True if the circle intersects the object.
     */
    virtual bool HitTestCircle(double worldX, double worldY, double radiusMm) const {
        AABB queryBox(worldX - radiusMm, worldY - radiusMm, worldX + radiusMm, worldY + radiusMm);
        if (!bounds.Intersects(queryBox)) {
            return false;
        }

        // Direct interior hit
        if (HitTest(worldX, worldY)) {
            return true;
        }

        // Clamped Euclidean distance from (worldX, worldY) to object bounds
        double clampedX = std::max(bounds.minX, std::min(worldX, bounds.maxX));
        double clampedY = std::max(bounds.minY, std::min(worldY, bounds.maxY));
        double dx = worldX - clampedX;
        double dy = worldY - clampedY;
        return (dx * dx + dy * dy) <= (radiusMm * radiusMm);
    }

    /**
     * @brief Continuous swept-volume hit test between two world-space points w0 and w1.
     * Prevents high-speed eraser strokes from 'tunneling' or skipping through objects.
     *
     * Mathematical Process:
     *   Given segment S(t) = w0 + t * (w1 - w0), for t in [0, 1]:
     *   1. Broad phase: Construct swept bounding box expanded by radiusMm.
     *   2. Check endpoints w0 and w1 with HitTestCircle.
     *   3. Project object's bounding center C onto the segment S(t):
     *        V = w1 - w0
     *        lenSq = |V|^2
     *        t = clamp(((C - w0) . V) / lenSq, 0.0, 1.0)
     *        closestPoint = w0 + t * V
     *   4. Evaluate HitTestCircle at the closest point along the swept path.
     *
     * @param w0 Starting point of the swept motion segment.
     * @param w1 Ending point of the swept motion segment.
     * @param radiusMm Radius of the swept sphere / capsule in millimeters.
     * @return True if the swept volume intersects this object.
     */
    virtual bool HitTestSwept(const Point2D& w0, const Point2D& w1, double radiusMm) const {
        AABB sweptBox(
            std::min(w0.x, w1.x) - radiusMm,
            std::min(w0.y, w1.y) - radiusMm,
            std::max(w0.x, w1.x) + radiusMm,
            std::max(w0.y, w1.y) + radiusMm
        );
        if (!bounds.Intersects(sweptBox)) {
            return false;
        }

        // Check segment endpoints
        if (HitTestCircle(w0.x, w0.y, radiusMm) || HitTestCircle(w1.x, w1.y, radiusMm)) {
            return true;
        }

        // Compute object center C
        Point2D center{ (bounds.minX + bounds.maxX) * 0.5, (bounds.minY + bounds.maxY) * 0.5 };

        // Vector V = w1 - w0
        double vx = w1.x - w0.x;
        double vy = w1.y - w0.y;
        double lenSq = vx * vx + vy * vy;

        if (lenSq > 1e-6) {
            // Projection factor t = ((C - w0) . V) / lenSq
            double t = ((center.x - w0.x) * vx + (center.y - w0.y) * vy) / lenSq;
            t = std::max(0.0, std::min(1.0, t));

            Point2D proj{ w0.x + t * vx, w0.y + t * vy };
            return HitTestCircle(proj.x, proj.y, radiusMm);
        }

        return false;
    }

    /**
     * @brief Tests if the object's geometry intersects a selection bounding box.
     * @param selectionBounds Marquee / selection area AABB in world space.
     * @return True if any part of the object intersects the selection bounds.
     */
    virtual bool Intersects(const AABB& selectionBounds) const = 0;

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
     * Mathematical Model:
     *   Maps coordinates from source space to transformed space using a 2x3 affine matrix:
     *     [ x' ]   [ m00  m01  m02 ] [ x ]   [ m00*x + m01*y + m02 ]
     *     [ y' ] = [ m10  m11  m12 ] [ y ] = [ m10*x + m11*y + m12 ]
     *     [ 1  ]   [  0    0    1  ] [ 1 ]   [          1          ]
     *   Where:
     *     - m00, m11 represent non-uniform scaling / cosine rotation factors
     *     - m01, m10 represent shear / sine rotation factors
     *     - m02, m12 represent translation (dx, dy) in millimeters
     *
     * @param matrix 2D affine transformation matrix.
     */
    virtual void ApplyTransform(const BLMatrix2D& matrix) = 0;

    /**
     * @brief Bakes the accumulated affine transform matrix into the object's intrinsic geometry.
     *
     * General Process:
     *   Called upon completion of an interactive manipulation (e.g. mouse release after gizmo drag/rotation).
     *   Multiplies every intrinsic vertex/point by the current `transform` matrix and resets `transform`
     *   to identity (BLMatrix2D::make_identity()). This eliminates numerical drift from compounding matrices
     *   and ensures subsequent bounds updates are exact.
     */
    virtual void BakeTransform() {}

    /********************************************* */
    // Rendering
    /********************************************* */

    /**
     * @brief Renders the object using the provided Blend2D graphics context.
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
    virtual void Serialize(Serializer& writer) const = 0;
    virtual void Deserialize(Deserializer& reader) = 0;

    /********************************************* */
    // Selection & Gizmo Interaction
    /********************************************* */

    /**
     * @brief Queries custom handles for this object. If returns false, the engine
     * automatically generates the standard 8-point bounding box resize grips + 1 rotation pin.
     * Custom objects (e.g. SmartArrow, Connectors, custom shapes) can override this to
     * provide custom endpoint or vertex handles.
     */
    virtual bool GetCustomGizmoHandles(std::vector<GizmoHandle>& outHandles, const CanvasTransform& transform) const {
        (void)outHandles;
        (void)transform;
        return false;
    }

    /**
     * @brief Called when the user drags a custom handle returned by GetCustomGizmoHandles.
     */
    virtual bool OnGizmoHandleDrag(int customId, const Point2D& worldPos, const Point2D& worldDelta) {
        (void)customId;
        (void)worldPos;
        (void)worldDelta;
        return false;
    }

    /**
     * @brief Optional custom selection overlay drawing (e.g., connector anchors, curve tangents).
     */
    virtual void RenderCustomSelection(BLContext& ctx, const CanvasTransform& transform) const {
        (void)ctx;
        (void)transform;
    }
};