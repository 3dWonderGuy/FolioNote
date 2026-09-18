#pragma once
/**
 * @file ink_container.hpp
 * @brief Persistent canvas entity holding baked vector ink strokes.
 *
 * InkContainer inherits from CanvasObject and manages one or more finished vector strokes
 * grouped together. It provides:
 *  - Accurate world-space AABB bounding box calculation (taking stroke width & transforms into account)
 *  - Point-to-segment distance hit-testing (for selection and eraser tools)
 *  - Fast batch-rendered vector drawing via Blend2D with non-zero winding rules
 *  - Incremental dirty-flag tracking to avoid redundant rasterization
 *  - Eraser slicing / segment division into child fragment containers
 */

#include <vector>
#include <memory>
#include <blend2d/blend2d.h>

#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"
#include "core/engine/stroke_smoother.hpp"
#include "input/pen_palette.hpp"

/**
 * @brief Baked vector stroke data representing a single stroke (pen down -> pen up).
 * 
 * Each stroke contains a 2D closed polygon outline (BLPath) for instant non-zero fill
 * rasterization, alongside centerline points and segments for hit-testing and geometric editing.
 */
struct Stroke {
    std::vector<Segment1D> segments;               ///< Ordered series of line segments (for hit-testing and geometric slicing)
    std::vector<Point2D>   centerline;             ///< Original smoothed centerline points with pressure & time telemetry
    BLPath                 outlinePath;            ///< Closed 2D vector polygon contour for rasterization
    BLRgba32               color{0xFFFFFFFF};      ///< 32-bit RGBA color
    double                 baseWidth = 3.0;        ///< Nominal baseline width in world millimeters (mm)
    StrokePattern          pattern = StrokePattern::Solid; ///< Line pattern (Solid, Dashed, Dotted)
};

/**
 * @brief Persistent canvas entity holding baked vector ink strokes.
 */
class InkContainer final : public CanvasObject {
public:
    std::vector<Stroke> strokes;       ///< List of strokes contained within this container
    bool isHighlighter = false;        ///< If true, drawn with semi-transparent highlighter blending (MULTIPLY op)

    /**
     * @brief Incremental rendering dirty flag.
     * When true, new strokes have been added or existing strokes modified since the last Render() call.
     * The canvas engine uses this to re-stroke only dirty containers onto the static background layer.
     */
    mutable bool renderDirty = true;

    /**
     * @brief Marks this container as needing a full rasterization pass on the next frame.
     */
    void InvalidateCache();

    /**
     * @brief Constructs an empty ink container with ObjectType::InkContainer.
     */
    InkContainer();

    /**
     * @brief Appends a finished vector stroke to this container and updates bounding box.
     * @param stroke Finished stroke to add
     */
    void AddStroke(const Stroke& stroke);

    // =========================================================================
    // 1. BOUNDS & SPATIAL QUERIES
    // =========================================================================

    /**
     * @brief Computes the Axis-Aligned Bounding Box (AABB) in world coordinates.
     * 
     * Algorithm:
     * 1. Iterates through every segment or pre-baked 2D outline path in local coordinates.
     * 2. Accounts for the visual thickness of each segment by adding/subtracting half-width (radius).
     * 3. Transforms the 4 corners of the local bounding box using the container's affine matrix:
     *      p' = [m00*x + m10*y + m20, m01*x + m11*y + m21]^T
     * 4. Enclosing AABB derived from min/max extrema with a 2.0 mm antialiasing safety margin.
     */
    void UpdateBounds() override;

    /**
     * @brief Performs precise geometric hit testing for a world-space point (e.g., stylus or eraser).
     *
     * 2-Tier Culling Strategy:
     * - Tier 1: AABB broadphase bounding box test in world coordinates.
     * - Tier 2: Inverted affine mapping into local space followed by:
     *     a. BLPath::hit_test with BL_FILL_RULE_NON_ZERO.
     *     b. StrokeCollisionEngine segment-to-point distance check (tolerance 1.5 mm).
     *
     * @param worldX Query X in world millimeters
     * @param worldY Query Y in world millimeters
     * @return true if point hits any stroke
     */
    bool HitTest(double worldX, double worldY) const override;

    /**
     * @brief Evaluates whether any stroke segment intersects an eraser circle of radiusMm.
     *
     * @param worldX Eraser circle center X in world millimeters
     * @param worldY Eraser circle center Y in world millimeters
     * @param radiusMm Eraser circle radius in world millimeters
     * @return true if circle overlaps any stroke
     */
    bool HitTestCircle(double worldX, double worldY, double radiusMm) const override;

    /**
     * @brief Evaluates whether any stroke segment intersects a continuous swept capsule from w0 to w1.
     * Prevents fast-moving eraser skips / tunneling with continuous swept-line collision.
     *
     * @param w0 Segment start in world coordinates
     * @param w1 Segment end in world coordinates
     * @param radiusMm Capsule radius in world millimeters
     * @return true if swept capsule intersects any stroke
     */
    bool HitTestSwept(const Point2D& w0, const Point2D& w1, double radiusMm) const override;

    /**
     * @brief Checks if this container intersects a selection bounding box (e.g. lasso or marquee selection).
     * @param selectionBounds Selection rectangle in world millimeters
     */
    bool Intersects(const AABB& selectionBounds) const override;

    // =========================================================================
    // 2. GEOMETRY & TRANSFORMS
    // =========================================================================

    /**
     * @brief Applies a post-multiplication affine transform matrix (translate, scale, rotate).
     * @param matrix 2D affine transformation matrix
     */
    void ApplyTransform(const BLMatrix2D& matrix) override;

    // =========================================================================
    // 3. VECTOR RENDERING PASS
    // =========================================================================

    /**
     * @brief Renders the vector strokes into the given Blend2D context using 2D closed polygon outlines.
     * 
     * Pipeline:
     * 1. Viewport frustum culling: rejects completely off-screen containers.
     * 2. Graphics context state isolation: save() / apply_transform() / restore().
     * 3. Winding Rule: BL_FILL_RULE_NON_ZERO ensures self-overlapping loops fuse into a solid silhouette.
     * 4. Composition Mode: BL_COMP_OP_MULTIPLY for highlighters, BL_COMP_OP_SRC_OVER for opaque pens.
     * 5. Fast-Path: Renders cached 2D closed polygon outline (BLPath) with zero seams.
     *
     * @param ctx Blend2D raster context
     * @param viewport Active canvas viewport for frustum culling
     */
    void Render(BLContext& ctx, const Viewport& viewport) const override;

    // =========================================================================
    // 4. DUPLICATION & PERSISTENCE
    // =========================================================================

    /**
     * @brief Deep-copies this InkContainer entity.
     */
    std::unique_ptr<CanvasObject> Clone() const override;

    void Serialize(Serializer& writer) const override;
    void Deserialize(Deserializer& reader) override;

    // =========================================================================
    // 5. ERASER SLICING
    // =========================================================================

    /**
     * @brief Erases segments within an eraser radius and slices affected strokes into surviving sub-strokes.
     *
     * Math:
     * - Maps eraser circle to local coordinates using inverted affine matrix.
     * - Radius scaled by 1 / hypot(m00, m01).
     * - Splits segments into sub-chains and rebuilds clean 2D closed polygon contours.
     *
     * @param worldX Eraser center X in world mm
     * @param worldY Eraser center Y in world mm
     * @param radius Eraser circle radius in world mm
     * @param[out] outNewFragments Receives newly spawned split fragment containers
     * @return true if any stroke was modified or sliced
     */
    bool SliceStrokeAt(double worldX, double worldY, double radius,
                       std::vector<std::shared_ptr<InkContainer>>& outNewFragments);

    /**
     * @brief Overload without fragment output list (discards split sub-fragments).
     */
    bool SliceStrokeAt(double worldX, double worldY, double radius);
};