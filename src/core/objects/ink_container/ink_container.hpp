#pragma once
/**
 * =========================================================================================
 * @file core/objects/ink_container/ink_container.hpp
 * @brief High-Performance Baked Vector Ink Container and Stroke Geometry Model
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN:
 * ---------------------
 * `InkContainer` inherits from `CanvasObject` and acts as the persistent entity for freehand
 * vector inking on the infinite canvas.
 *
 * It provides:
 *  1. Dual-Representation Vector Model:
 *     - Closed 2D polygon ribbon outline (BLPath) for zero-seam, hardware-accelerated
 *       SIMD non-zero fill rendering via Blend2D.
 *     - Discrete spatial centerline points (StrokePoint) and segments (Segment1D) for
 *       precision distance-based hit testing, proximity selection, and point eraser slicing.
 *  2. Rich Pen Semantics:
 *     - Per-stroke BlendMode (Normal/SrcOver, Multiply for Highlighters, Additive for Glow/Neon).
 *     - Configurable CapType (Round, Flat, Chisel, Square) and StrokePattern (Solid, Dashed, Dotted, DashDot).
 *  3. Affine Geometry Transformations:
 *     - Real-time interactive transformation via 2D affine matrix (BLMatrix2D).
 *     - Geometric transform baking committing scaling, translation, and rotation directly into
 *       centerline points, segments, and polygon outline hulls.
 *  4. High-Performance Two-Tier Hit Testing & Eraser Slicing:
 *     - Broadphase AABB query rejection.
 *     - Narrowphase inverted-transform spatial evaluation (exact segment Euclidean distance,
 *       continuous swept-capsule testing, and quadratic circle-segment eraser splitting).
 */

#include <vector>
#include <memory>
#include <cstdint>
#include <blend2d/blend2d.h>

#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"
#include "core/engine/stroke_smoother.hpp"
#include "input/pen_palette.hpp"

/**
 * @struct StrokePoint
 * @brief Discrete centerline vertex holding spatial coordinates and pre-resolved stroke radius.
 *
 * Coordinates are in physical world millimeters (mm).
 */
struct StrokePoint {
    double x = 0.0;     ///< Centerline X position in world millimeters (mm)
    double y = 0.0;     ///< Centerline Y position in world millimeters (mm)
    float  r = 0.25f;   ///< Pre-calculated stroke radius (half-thickness) in world millimeters (mm)
};

/**
 * @struct Stroke
 * @brief Vector stroke entity encapsulating canonical centerline geometry and ephemeral render caches.
 *
 * DATA STORAGE CONTRACT:
 * - Database & Serialization: ONLY the Canonical Centerline Vector Model and style attributes
 *   are serialized to disk or SQLite. Polygons and segment trees are NEVER stored permanently.
 * - Runtime Acceleration: `segments`, `outlinePath`, and `bounds` are transient caches generated
 *   in memory via StrokeOutlineBuilder::GenerateRibbon() and StrokeCollision::BuildSegments()
 *   upon document loading or when points are modified.
 */
struct Stroke {
    // =========================================================================
    // 1. CANONICAL CENTERLINE VECTOR MODEL (Serialized / Saved to Database)
    // =========================================================================
    std::vector<StrokePoint> points;       ///< Smoothed centerline vertices with precomputed radius (world mm)

    PenType       penType       = PenType::Pen;           ///< Pen preset category (Ballpoint, Fountain, Calligraphy, Highlighter)
    CapType       capType       = CapType::Round;         ///< Stroke endcap style (Round, Flat)
    StrokePattern strokePattern = StrokePattern::Solid;   ///< Line pattern (Solid, Dashed, Dotted, DashDot)
    BlendMode     blendMode     = BlendMode::Normal;      ///< Layer blend mode (Normal, Multiply, Additive)

    BLRgba32      color{0x18, 0x1A, 0x20, 0xFF};          ///< 32-bit RGBA stroke color
    union {
        float     baseWidthMm = 0.5f;                     ///< Baseline nominal stroke thickness in millimeters (mm)
        float     baseWidth;                              ///< Serialization and export compatibility alias
    };
    float         opacity       = 1.0f;                   ///< Stroke opacity multiplier [0.0, 1.0]

    // =========================================================================
    // 2. TRANSIENT RUNTIME ACCELERATION CACHES (Ephemeral in RAM — Never Saved)
    // =========================================================================
    std::vector<Segment1D>   segments;     ///< Swept segment tree for distance hit-testing & eraser slicing
    BLPath                   outlinePath;  ///< Closed 2D polygon contour ribbon for hardware-accelerated Blend2D rasterization
    AABB                     bounds;       ///< Axis-aligned bounding box in millimeters (mm) for spatial R-Tree indexing

    /**
     * @brief Maps internal BlendMode to Blend2D composite operator.
     * @return BLCompOp enumeration value.
     */
    [[nodiscard]] BLCompOp GetBlend2DCompOp() const noexcept {
        switch (blendMode) {
            case BlendMode::Multiply: return BL_COMP_OP_MULTIPLY;
            case BlendMode::Additive: return BL_COMP_OP_PLUS;
            case BlendMode::Normal:
            default:                  return BL_COMP_OP_SRC_OVER;
        }
    }
};

/**
 * @class InkContainer
 * @brief Persistent canvas entity holding one or more finished vector ink strokes.
 */
class InkContainer final : public CanvasObject {
public:
    std::vector<Stroke> strokes;       ///< Collection of baked vector strokes belonging to this container
    bool isHighlighter = false;        ///< Legacy container-level highlighter flag (synced with BlendMode::Multiply)
    mutable bool renderDirty = true;   ///< Dirty flag indicating background composite cache requires updating

    /**
     * @brief Constructs an empty InkContainer with ObjectType::InkContainer.
     */
    InkContainer();

    /**
     * @brief Flags this container as requiring re-rasterization on the next render pass.
     */
    void InvalidateCache() noexcept;

    /**
     * @brief Appends a finished stroke to the container and updates the world-space bounding box.
     * @param stroke Vector stroke to append.
     */
    void AddStroke(const Stroke& stroke);

    // =========================================================================
    // BOUNDS & SPATIAL QUERIES
    // =========================================================================

    /**
     * @brief Recomputes the cached world-space AABB incorporating all strokes and the active transform matrix.
     */
    void UpdateBounds() override;

    /**
     * @brief Tests if a 2D world-space coordinate intersects any stroke in this container.
     * @param x World X coordinate in millimeters (mm).
     * @param y World Y coordinate in millimeters (mm).
     * @return true if coordinate intersects stroke polygon or lies within tolerance distance.
     */
    [[nodiscard]] bool HitTest(double x, double y) const noexcept override;

    /**
     * @brief Tests if a circular query kernel (stylus tip, touch point, or eraser) intersects any stroke.
     * @param cx Circle center X in world millimeters (mm).
     * @param cy Circle center Y in world millimeters (mm).
     * @param radiusMm Circle radius in world millimeters (mm).
     * @return true if circle kernel overlaps any stroke.
     */
    [[nodiscard]] bool HitTestCircle(double cx, double cy, double radiusMm) const noexcept override;

    /**
     * @brief Continuous swept-capsule collision test against all strokes between coordinates w0 and w1.
     * Prevents fast-moving eraser tunneling and trajectory skipping.
     * @param w0 Capsule start coordinate in world millimeters (mm).
     * @param w1 Capsule end coordinate in world millimeters (mm).
     * @param radiusMm Capsule radius in world millimeters (mm).
     * @return true if swept capsule intersects any stroke.
     */
    [[nodiscard]] bool HitTestSwept(const Point2D& w0, const Point2D& w1, double radiusMm) const;

    // =========================================================================
    // GEOMETRY & TRANSFORMS
    // =========================================================================

    /**
     * @brief Applies a post-multiplication 2D affine transformation matrix to this container.
     * @param matrix 2D affine matrix to apply.
     */
    void ApplyTransform(const BLMatrix2D& matrix) override;

    /**
     * @brief Bakes the accumulated affine transform matrix into intrinsic stroke vertices,
     * recomputes 2D closed polygon contours, and resets transform matrix to identity.
     */
    void BakeTransform() override;

    /**
     * @brief Specifies standard 8-point bounding box gizmo interaction for vector ink containers.
     * @return GizmoStyle::BoundingBox.
     */
    [[nodiscard]] GizmoStyle GetGizmoStyle() const noexcept override {
        return GizmoStyle::BoundingBox;
    }

    // =========================================================================
    // RENDERING & LIFECYCLE
    // =========================================================================

    /**
     * @brief Draws vector strokes into the target Blend2D context using non-zero winding rules.
     * @param ctx Blend2D rendering context.
     * @param viewport Active canvas viewport for frustum culling.
     */
    void Render(BLContext& ctx, const Viewport& viewport) const override;

    /**
     * @brief Produces a deep clone of this InkContainer and all contained strokes.
     * @return Polymorphic unique_ptr to cloned CanvasObject.
     */
    [[nodiscard]] std::unique_ptr<CanvasObject> Clone() const override;

    // =========================================================================
    // ERASER SLICING
    // =========================================================================

    /**
     * @brief Slices strokes overlapping the circular eraser kernel into surviving sub-stroke fragments.
     * @param worldX Eraser center X in world millimeters (mm).
     * @param worldY Eraser center Y in world millimeters (mm).
     * @param radius Eraser circle radius in world millimeters (mm).
     * @param[out] outNewFragments Destination vector populated with newly spawned split containers.
     * @return true if any stroke was sliced or modified.
     */
    bool SliceStrokeAt(double worldX, double worldY, double radius,
                       std::vector<std::shared_ptr<InkContainer>>& outNewFragments);

    /**
     * @brief Overload without output fragment list (discards split sub-fragments).
     * @param worldX Eraser center X in world millimeters (mm).
     * @param worldY Eraser center Y in world millimeters (mm).
     * @param radius Eraser circle radius in world millimeters (mm).
     * @return true if any stroke was sliced or modified.
     */
    bool SliceStrokeAt(double worldX, double worldY, double radius);
};