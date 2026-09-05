#pragma once
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <blend2d/blend2d.h>
#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"
#include "core/engine/stroke_smoother.hpp"
#include "core/engine/stroke_outline_builder.hpp"

/**
 * @brief Baked vector stroke data representing a single stroke (pen down -> pen up).
 * 
 * Each stroke contains a 2D closed polygon outline (BLPath) for instant non-zero fill
 * rasterization, alongside centerline points and segments for hit-testing and geometric editing.
 */
struct Stroke {
    std::vector<Segment1D> segments;  ///< Ordered series of line segments (for backward-compat & hit-testing)
    std::vector<Point2D> centerline;  ///< Original smoothed centerline points with pressure & time
    BLPath outlinePath;               ///< Closed 2D vector polygon contour for rasterization
    BLRgba32 color{0xFFFFFFFF};       ///< 32-bit RGBA color
    double baseWidth = 3.0;           ///< Nominal baseline width in world millimeters (mm)
};

/**
 * @brief Persistent canvas entity holding baked vector ink strokes.
 * 
 * InkContainer inherits from CanvasObject and represents one or more finished strokes
 * grouped together. It handles:
 *  - Accurate world-space AABB bounding box calculation (taking stroke width & transforms into account)
 *  - Point-to-segment distance hit-testing (for selection and eraser tools)
 *  - Fast batch-rendered vector drawing via Blend2D
 *  - Incremental dirty-flag tracking to avoid redundant rasterization
 */
class InkContainer final : public CanvasObject {
public:
    std::vector<Stroke> strokes;       ///< List of strokes contained within this container
    bool isHighlighter = false;        ///< If true, drawn with semi-transparent highlighter blending

    /**
     * @brief Incremental rendering dirty flag.
     * When true, new strokes have been added or existing strokes modified since the last Render() call.
     * The canvas engine uses this to re-stroke only dirty containers onto the static background layer.
     */
    mutable bool renderDirty = true;

    /**
     * @brief Marks this container as needing a full rasterization pass on the next frame.
     */
    void InvalidateCache() { renderDirty = true; }

    InkContainer() {
        type = ObjectType::InkContainer;
    }

    /**
     * @brief Appends a finished vector stroke to this container and updates bounding box.
     */
    void AddStroke(const Stroke& stroke) {
        strokes.push_back(stroke);
        renderDirty = true;  // Mark dirty so the static cache composites this stroke
        UpdateBounds();
    }

    // =========================================================================
    // 1. BOUNDS & SPATIAL QUERIES
    // =========================================================================

    /**
     * @brief Computes the Axis-Aligned Bounding Box (AABB) in world coordinates.
     * 
     * Algorithm:
     * 1. Iterates through every segment of every stroke in local coordinates.
     * 2. Accounts for the visual thickness of each segment by adding/subtracting half-width (radius).
     * 3. Transforms the 4 corners of the local bounding box using the container's affine matrix.
     * 4. Encapsulates transformed corners into a world-space AABB with padding.
     */
    void UpdateBounds() override {
        if (strokes.empty()) {
            bounds = AABB{};
            return;
        }

        double minX = 1e20, minY = 1e20, maxX = -1e20, maxY = -1e20;

        // Step 1: Find local min/max extents from 2D outline paths or segments
        for (const auto& stroke : strokes) {
            if (!stroke.outlinePath.is_empty()) {
                BLBox box;
                if (stroke.outlinePath.get_bounding_box(&box) == BL_SUCCESS) {
                    minX = std::min(minX, box.x0);
                    minY = std::min(minY, box.y0);
                    maxX = std::max(maxX, box.x1);
                    maxY = std::max(maxY, box.y1);
                }
            } else {
                for (const auto& seg : stroke.segments) {
                    double hw = seg.width * 0.5; // Stroke radius around the centerline
                    minX = std::min({minX, seg.p0.x - hw, seg.p1.x - hw});
                    minY = std::min({minY, seg.p0.y - hw, seg.p1.y - hw});
                    maxX = std::max({maxX, seg.p0.x + hw, seg.p1.x + hw});
                    maxY = std::max({maxY, seg.p0.y + hw, seg.p1.y + hw});
                }
            }
        }

        if (minX > maxX || minY > maxY) {
            bounds = AABB{};
            return;
        }

        // Step 2: Map the 4 local bounding corners through the object's affine transform
        double pad = 2.0; // 2mm safety margin to ensure antialiasing fringes are never clipped
        BLPoint corners[4] = {
            transform.map_point(minX - pad, minY - pad),
            transform.map_point(maxX + pad, minY - pad),
            transform.map_point(minX - pad, maxY + pad),
            transform.map_point(maxX + pad, maxY + pad)
        };

        // Step 3: Compute the enclosing world-space AABB from the transformed corners
        bounds = AABB{
            std::min({corners[0].x, corners[1].x, corners[2].x, corners[3].x}),
            std::min({corners[0].y, corners[1].y, corners[2].y, corners[3].y}),
            std::max({corners[0].x, corners[1].x, corners[2].x, corners[3].x}),
            std::max({corners[0].y, corners[1].y, corners[2].y, corners[3].y})
        };
    }

    /**
     * @brief Performs precise geometric hit testing for a world-space point (e.g., stylus or eraser).
     */
    bool HitTest(double worldX, double worldY) const override {
        // Broadphase: fast bounding box check
        if (!bounds.Contains(worldX, worldY)) return false;

        // Map query point from world space into object local space
        BLMatrix2D invTransform;
        BLMatrix2D::invert(invTransform, transform);
        BLPoint localPt = invTransform.map_point(worldX, worldY);

        // Narrowphase: check inside filled polygon outline, or distance to segment centerlines
        for (const auto& stroke : strokes) {
            if (!stroke.outlinePath.is_empty()) {
                BLHitTest hit = stroke.outlinePath.hit_test(BLPoint{localPt.x, localPt.y}, BL_FILL_RULE_NON_ZERO);
                if (hit == BL_HIT_TEST_IN) return true;
            }

            for (const auto& seg : stroke.segments) {
                double dx = seg.p1.x - seg.p0.x;
                double dy = seg.p1.y - seg.p0.y;
                double magSq = dx * dx + dy * dy;

                double u = (magSq < 0.0001) 
                    ? 0.0 
                    : std::clamp(((localPt.x - seg.p0.x) * dx + (localPt.y - seg.p0.y) * dy) / magSq, 0.0, 1.0);

                double px = seg.p0.x + u * dx;
                double py = seg.p0.y + u * dy;

                if (std::hypot(localPt.x - px, localPt.y - py) <= (seg.width * 0.5 + 2.0)) {
                    return true;
                }
            }
        }
        return false;
    }

    /**
     * @brief Checks if this container intersects a selection bounding box (e.g. lasso or marquee selection).
     */
    bool Intersects(const AABB& selectionBounds) const override {
        return bounds.Intersects(selectionBounds);
    }

    // =========================================================================
    // 2. GEOMETRY & TRANSFORMS
    // =========================================================================

    /**
     * @brief Applies a post-multiplication affine transform matrix (translate, scale, rotate).
     */
    void ApplyTransform(const BLMatrix2D& matrix) override {
        transform.post_transform(matrix);
        UpdateBounds();
    }

    // =========================================================================
    // 3. VECTOR RENDERING PASS
    // =========================================================================

    /**
     * @brief Renders the vector strokes into the given Blend2D context using 2D closed polygon outlines.
     * 
     * Renders using BL_FILL_RULE_NON_ZERO. This guarantees that self-overlapping loops
     * (e.g. cursive writing, signatures) fill cleanly as a single unified solid silhouette
     * without dark overlapping seams or hollow cutouts.
     */
    void Render(BLContext& ctx, const Viewport& viewport) const override {
        // 1. VISIBILITY & CULLING
        // Check if the container is meant to be visible. If it's fully transparent or hidden,
        // we skip drawing to save CPU/GPU cycles.
        if (!isVisible || opacity <= 0.0f) return;
        
        // Frustum Culling: If the container's bounding box doesn't overlap the current camera
        // view (viewport.bounds), it's completely off-screen and we can skip rendering it.
        if (!bounds.Intersects(viewport.bounds)) return; // Viewport frustum culling

        renderDirty = false;  // Mark clean: strokes have been rasterized

        // 2. CONTEXT SETUP & TRANSFORM
        // We save the global graphics state so that applying our specific transform 
        // (position/rotation/scale) doesn't affect other objects drawn after this one.
        ctx.save();
        ctx.apply_transform(transform);

        // 3. FILL RULE (CRUCIAL FOR INK)
        // We use BL_FILL_RULE_NON_ZERO instead of the default Even-Odd rule.
        // This is mathematically required so that when a single stroke loops back over itself 
        // (like in cursive writing or scribbling), the overlapping sections fuse together into 
        // a unified solid silhouette instead of canceling each other out to create empty "holes".
        ctx.set_fill_rule(BL_FILL_RULE_NON_ZERO);

        for (const auto& stroke : strokes) {
            if (stroke.outlinePath.is_empty() && stroke.segments.empty()) continue;

            // 4. MATERIAL SIMULATION (HIGHLIGHTER VS PEN)
            // Configure blend mode & color
            if (isHighlighter) {
                // Highlighters use SRC_OVER with translucent alpha (~33% opacity or 0x55).
                // This simulates real highlighter ink letting the underlying text/drawings show through.
                ctx.set_comp_op(BL_COMP_OP_SRC_OVER);
                ctx.set_fill_style(BLRgba32(stroke.color.r(), stroke.color.g(), stroke.color.b(), 0x55));
            } else {
                // Regular pens use their opaque base color and cover everything beneath them.
                ctx.set_comp_op(BL_COMP_OP_SRC_OVER);
                ctx.set_fill_style(stroke.color);
            }

            // 5. RENDERING: HIGH-FIDELITY OUTLINE FAST-PATH
            // Calculating precise variable-width brush geometry is expensive.
            // If we have a pre-calculated 2D polygon outline (stroke.outlinePath), 
            // we use it. This achieves zero seams and requires only one fast draw call.
            if (!stroke.outlinePath.is_empty()) {
                ctx.fill_path(stroke.outlinePath);
            } else {
                // 6. FALLBACK RENDERING
                // Fallback for legacy strokes or brand new strokes whose outline hasn't been built yet.
                if (stroke.segments.size() == 1) {
                    // Just draw a single thick line with round caps if it's only one segment long.
                    ctx.set_stroke_caps(BL_STROKE_CAP_ROUND);
                    ctx.set_stroke_width(stroke.segments[0].width);
                    ctx.stroke_line(stroke.segments[0].p0.x, stroke.segments[0].p0.y, stroke.segments[0].p1.x, stroke.segments[0].p1.y);
                } else {
                    // Generate the variable-width outline on the fly. This is a bit slower but guarantees 
                    // it renders correctly until the outline is permanently cached.
                    std::vector<StrokeOutlineBuilder::InputPoint> pts;
                    pts.reserve(stroke.segments.size() + 1);
                    pts.push_back({ stroke.segments[0].p0.x, stroke.segments[0].p0.y, stroke.segments[0].width });
                    for (const auto& seg : stroke.segments) {
                        pts.push_back({ seg.p1.x, seg.p1.y, seg.width });
                    }
                    BLPath fallbackOutline = StrokeOutlineBuilder::BuildOutline(pts);
                    ctx.fill_path(fallbackOutline);
                }
            }
        }

        // 7. CLEANUP
        // Restore the graphics context to its previous state (popping the transform we applied).
        ctx.restore();
    }

    // =========================================================================
    // 4. DUPLICATION & PERSISTENCE
    // =========================================================================

    /**
     * @brief Deep-copies this InkContainer entity.
     */
    std::unique_ptr<CanvasObject> Clone() const override {
        auto clone = std::make_unique<InkContainer>();
        clone->uid = this->uid;
        clone->type = this->type;
        clone->bounds = this->bounds;
        clone->transform = this->transform;
        clone->zOrder = this->zOrder;
        clone->opacity = this->opacity;
        clone->isVisible = this->isVisible;
        clone->isLocked = this->isLocked;
        clone->isSelectable = this->isSelectable;
        clone->isSelected = this->isSelected;
        clone->isTemporary = this->isTemporary;
        clone->strokes = this->strokes;
        clone->isHighlighter = this->isHighlighter;
        clone->renderDirty = true;
        return clone;
    }

    // currenlty there is centerlized system

    void Serialize(Serializer& /*writer*/) const override {
        // Reserved for binary serialization
    }

    void Deserialize(Deserializer& /*reader*/) override {
        // Reserved for binary deserialization
    }

    // =========================================================================
    // 5. FUTURE ROADMAP / EXTENSION STUBS
    // =========================================================================

    // --- True "Slice" / Point Eraser (Segment Splitting) ---
    /**
     * @brief Erases segments within an eraser radius and slices affected strokes into sub-strokes.
     * @param worldX Eraser center X in world mm
     * @param worldY Eraser center Y in world mm
     * @param radius Eraser circle radius in world mm
     * @return true if any stroke was modified or sliced
     */
    bool SliceStrokeAt(double /*worldX*/, double /*worldY*/, double /*radius*/) {
        // Reserved: Point/slice eraser algorithm.
        // Splits a stroke into multiple surviving strokes when intersected by an eraser sphere.
        return false;
    }

    // --- Post-Selection Editing (Recolor, Resize, Reorder) ---
    /**
     * @brief Batch recolors all strokes within this container.
     */
    void SetColor(BLRgba32 /*newColor*/) {
        // Reserved: Recolor all contained strokes and mark dirty
    }

    /**
     * @brief Uniformly scales stroke thickness across all strokes in this container.
     */
    void ScaleThickness(float /*factor*/) {
        // Reserved: Rescale baseWidth and segment widths, then UpdateBounds()
    }

    /**
     * @brief Incrementally brings this container forward in the layer hierarchy.
     */
    void BringForward() {
        // Reserved: Increment zOrder relative to neighbor objects
    }

    /**
     * @brief Incrementally sends this container backward in the layer hierarchy.
     */
    void SendBackward() {
        // Reserved: Decrement zOrder relative to neighbor objects
    }

    /**
     * @brief Moves this container to the top-most z-order.
     */
    void BringToFront() {
        // Reserved: Assign maximum zOrder
    }

    /**
     * @brief Moves this container to the bottom-most z-order.
     */
    void SendToBack() {
        // Reserved: Assign minimum zOrder
    }

    // --- Selection Visuals & Transform Handles ---
    /**
     * @brief Renders selection bounding box, contour glow, and resize/rotation handles.
     */
    void RenderSelectionHandles(BLContext& /*ctx*/, const Viewport& /*viewport*/) const {
        // Reserved: Interactive marquee boundary and rotation/scale gizmo rendering
    }

    // --- Curve Fitting / Bézier Simplification (RDP + Catmull-Rom) ---
    /**
     * @brief Simplifies linear segments using Ramer-Douglas-Peucker (RDP) and fits cubic Bézier curves.
     * @param tolerance Error threshold in mm for point reduction
     */
    void SimplifyCurves(double /*tolerance*/ = 0.2) {
        // Reserved: Ramer-Douglas-Peucker segment reduction + Catmull-Rom to Bézier fitting
    }

    // --- Shape Recognition / Snap-to-Geometry ("Hold to Snap") ---
    enum class DetectedShapeType {
        None,
        Line,
        Rectangle,
        Circle,
        Ellipse,
        Triangle
    };

    /**
     * @brief Evaluates whether stroke geometry matches a canonical shape and snaps to perfect vector primitives.
     */
    DetectedShapeType DetectAndSnapShape(double /*tolerance*/ = 0.8) {
        // Reserved: Geometric shape classifier (straight ruler, circle, rectangle fitting)
        return DetectedShapeType::None;
    }

    // --- Stroke Replay / Time-Lapse Telemetry ---
    /**
     * @brief Replays stroke progression over time for tutorials or animated note playback.
     * @param normalizedProgress Playback progress between 0.0f (start) and 1.0f (complete)
     */
    void RenderPlayback(BLContext& /*ctx*/, const Viewport& /*viewport*/, float /*normalizedProgress*/) const {
        // Reserved: Interpolate segment chain based on timestamp telemetry
    }
};