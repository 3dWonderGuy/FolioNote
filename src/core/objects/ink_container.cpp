/**
 * @file ink_container.cpp
 * @brief Implementation of InkContainer: persistent vector ink stroke manager and renderer.
 *
 * Mathematical Principles & Working Process:
 * 1. Bounding Box Calculation (UpdateBounds):
 *    - Stroke Extents: Each stroke's boundary is evaluated either from its pre-baked closed
 *      polygon outline (BLPath get_bounding_box) or by expanding each segment's centerline:
 *        r = width / 2
 *        x_min = min(p0.x - r, p1.x - r),  x_max = max(p0.x + r, p1.x + r)
 *        y_min = min(p0.y - r, p1.y - r),  y_max = max(p0.y + r, p1.y + r)
 *    - Affine Coordinate Transformation:
 *      Corners of the local bounding rectangle are transformed via the 2D affine matrix M:
 *        p' = [m00*x + m10*y + m20,  m01*x + m11*y + m21]^T
 *      The enclosing world AABB is given by the min/max coordinates of the 4 transformed corners,
 *      plus an antialiasing safety padding of 2.0 mm.
 *
 * 2. Hit-Testing & Collision Detection (HitTest, HitTestCircle, HitTestSwept):
 *    - Broadphase: Fast rejection using the container's world-space AABB.
 *    - Space Transformation: Query points/capsules are mapped into object-local space
 *      via the inverted affine matrix M^-1.
 *    - Narrowphase:
 *        a. BLPath::hit_test with BL_FILL_RULE_NON_ZERO for instant polygon containment.
 *        b. StrokeCollisionEngine for segment-to-point / segment-to-circle / swept capsule distance.
 *
 * 3. Eraser Slicing (SliceStrokeAt):
 *    - Local circle radius is scaled by 1 / hypot(m00, m01) to account for object scale.
 *    - StrokeSlicer splits affected segment chains into surviving continuous sub-chains.
 *    - Surviving sub-chains are re-baked into smooth 2D closed polygon contours via StrokeOutlineBuilder.
 *
 * 4. High-Fidelity Rendering (Render):
 *    - Viewport frustum culling discards off-screen containers before context dispatch.
 *    - Context transform is isolated via save() / restore().
 *    - Uses BL_FILL_RULE_NON_ZERO to ensure self-overlapping cursive loops fuse cleanly without hollows.
 *    - Highlighter strokes utilize BL_COMP_OP_MULTIPLY to preserve underlying text and linework.
 */

#include "core/objects/ink_container.hpp"
#include "core/engine/stroke_collision.hpp"
#include "core/engine/stroke_outline_builder.hpp"

#include <algorithm>
#include <cmath>

// =============================================================================
// CONSTRUCTORS
// =============================================================================

InkContainer::InkContainer() {
    type = ObjectType::InkContainer;
}

void InkContainer::InvalidateCache() {
    renderDirty = true;
}

void InkContainer::AddStroke(const Stroke& stroke) {
    strokes.push_back(stroke);
    renderDirty = true;  // Mark dirty so static background cache composites this stroke
    UpdateBounds();
}

// =============================================================================
// 1. BOUNDS & SPATIAL QUERIES
// =============================================================================

/**
 * @brief Computes the Axis-Aligned Bounding Box (AABB) in world coordinates.
 *
 * Derivation:
 * 1. Calculate local bounding box enclosing all stroke polygon paths or centerline segments.
 * 2. Expand by half-stroke-width to guarantee envelope covers stroke caps.
 * 3. Map all 4 local bounding box corners through the affine transform matrix:
 *    [x']   [m00 m10 m20] [x]
 *    [y'] = [m01 m11 m21] [y]
 *    [1 ]   [ 0   0   1 ] [1]
 * 4. Form world AABB from [min(x'_i), min(y'_i)] to [max(x'_i), max(y'_i)].
 */
void InkContainer::UpdateBounds() {
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
                minX = (std::min)(minX, box.x0);
                minY = (std::min)(minY, box.y0);
                maxX = (std::max)(maxX, box.x1);
                maxY = (std::max)(maxY, box.y1);
            }
        } else {
            for (const auto& seg : stroke.segments) {
                double hw = seg.width * 0.5; // Stroke radius around centerline
                minX = (std::min)({minX, seg.p0.x - hw, seg.p1.x - hw});
                minY = (std::min)({minY, seg.p0.y - hw, seg.p1.y - hw});
                maxX = (std::max)({maxX, seg.p0.x + hw, seg.p1.x + hw});
                maxY = (std::max)({maxY, seg.p0.y + hw, seg.p1.y + hw});
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
        (std::min)({corners[0].x, corners[1].x, corners[2].x, corners[3].x}),
        (std::min)({corners[0].y, corners[1].y, corners[2].y, corners[3].y}),
        (std::max)({corners[0].x, corners[1].x, corners[2].x, corners[3].x}),
        (std::max)({corners[0].y, corners[1].y, corners[2].y, corners[3].y})
    };
}

/**
 * @brief Point-to-stroke hit test in world coordinates.
 *
 * @param worldX Query X in world millimeters
 * @param worldY Query Y in world millimeters
 * @return true if point intersects any stroke outline or is within 1.5mm of centerline
 */
bool InkContainer::HitTest(double worldX, double worldY) const {
    if (!isVisible || !isSelectable) return false;

    // Tier 1: Stroke-level AABB Broadphase Culling
    if (!bounds.Contains(worldX, worldY)) return false;

    // Fast-path: already selected container allows immediate interaction
    if (isSelected) return true;

    // Map query point from world space into object local space
    BLMatrix2D invTransform;
    BLMatrix2D::invert(invTransform, transform);
    BLPoint localPt = invTransform.map_point(worldX, worldY);

    // Tier 2: Narrowphase evaluation
    for (const auto& stroke : strokes) {
        if (!stroke.outlinePath.is_empty()) {
            BLHitTest hit = stroke.outlinePath.hit_test(BLPoint{localPt.x, localPt.y}, BL_FILL_RULE_NON_ZERO);
            if (hit == BL_HIT_TEST_IN) return true;
        }

        auto hit = StrokeCollisionEngine::HitTestStroke(stroke.segments, localPt.x, localPt.y, 1.5);
        if (hit.hit) return true;
    }
    return false;
}

/**
 * @brief Circle-to-stroke intersection test (for round erasers and proximity selection).
 *
 * @param worldX Circle center X in world mm
 * @param worldY Circle center Y in world mm
 * @param radiusMm Circle radius in world mm
 * @return true if circle intersects any stroke
 */
bool InkContainer::HitTestCircle(double worldX, double worldY, double radiusMm) const {
    if (!isVisible) return false;

    AABB queryBox(worldX - radiusMm, worldY - radiusMm, worldX + radiusMm, worldY + radiusMm);
    if (!bounds.Intersects(queryBox)) return false;

    BLMatrix2D invTransform;
    BLMatrix2D::invert(invTransform, transform);
    BLPoint localPt = invTransform.map_point(worldX, worldY);
    double scale = std::hypot(transform.m00, transform.m01);
    double localRadius = (scale > 1e-6) ? (radiusMm / scale) : radiusMm;

    for (const auto& stroke : strokes) {
        auto hit = StrokeCollisionEngine::HitTestStroke(stroke.segments, localPt.x, localPt.y, localRadius);
        if (hit.hit) return true;

        if (!stroke.outlinePath.is_empty()) {
            BLHitTest blHit = stroke.outlinePath.hit_test(BLPoint{localPt.x, localPt.y}, BL_FILL_RULE_NON_ZERO);
            if (blHit == BL_HIT_TEST_IN) return true;
        }
    }
    return false;
}

/**
 * @brief Continuous swept capsule collision test between w0 and w1.
 * Prevents fast stylus/eraser tunneling across stroke segments.
 *
 * @param w0 Capsule start center in world mm
 * @param w1 Capsule end center in world mm
 * @param radiusMm Capsule radius in world mm
 * @return true if swept capsule intersects any stroke
 */
bool InkContainer::HitTestSwept(const Point2D& w0, const Point2D& w1, double radiusMm) const {
    if (!isVisible) return false;

    AABB sweptBox(
        (std::min)(w0.x, w1.x) - radiusMm,
        (std::min)(w0.y, w1.y) - radiusMm,
        (std::max)(w0.x, w1.x) + radiusMm,
        (std::max)(w0.y, w1.y) + radiusMm
    );
    if (!bounds.Intersects(sweptBox)) return false;

    BLMatrix2D invTransform;
    BLMatrix2D::invert(invTransform, transform);
    BLPoint local0 = invTransform.map_point(w0.x, w0.y);
    BLPoint local1 = invTransform.map_point(w1.x, w1.y);
    Point2D lw0{ local0.x, local0.y };
    Point2D lw1{ local1.x, local1.y };

    double scale = std::hypot(transform.m00, transform.m01);
    double localRadius = (scale > 1e-6) ? (radiusMm / scale) : radiusMm;

    for (const auto& stroke : strokes) {
        if (StrokeCollisionEngine::HitTestStrokeSwept(stroke.segments, lw0, lw1, localRadius)) {
            return true;
        }
    }
    return false;
}

// =============================================================================
// 2. GEOMETRY & TRANSFORMS
// =============================================================================

void InkContainer::ApplyTransform(const BLMatrix2D& matrix) {
    transform.post_transform(matrix);
    renderDirty = true;
    UpdateBounds();
}

/**
 * @brief Bakes the active affine transformation matrix directly into all stroke geometry.
 *
 * Mathematical Process & Geometric Transformation:
 * 1. Early-out identity check:
 *    If matrix M == I (m00=1, m11=1, others=0), no baking is necessary.
 *
 * 2. Determinant & Uniform Scale Factor:
 *    The geometric area scaling factor is the determinant:
 *      det = |m00 * m11 - m01 * m10|
 *    The effective linear stroke thickness scaling factor is:
 *      scaleFactor = sqrt(det)  (if det > 1e-6, else 1.0)
 *
 * 3. Affine Coordinate Mapping:
 *    Every segment endpoint (p0, p1) and smoothed centerline vertex is mapped through M:
 *      p' = [m00 * x + m10 * y + m20,  m01 * x + m11 * y + m21]^T
 *    Segment thickness is scaled by scaleFactor to preserve visual stroke proportions.
 *
 * 4. Outline Contour Re-baking:
 *    StrokeOutlineBuilder constructs fresh, artifact-free 2D closed polygon contours (BLPath)
 *    from the transformed segment vertices with pristine round cap geometry.
 *
 * 5. State Reset:
 *    Transform matrix is reset to identity M = I, render dirty flag is raised,
 *    and the world-space bounding box (bounds) is recalculated.
 */
void InkContainer::BakeTransform() {
    if (transform.m00 == 1.0 && transform.m01 == 0.0 &&
        transform.m10 == 0.0 && transform.m11 == 1.0 &&
        transform.m20 == 0.0 && transform.m21 == 0.0) {
        return;
    }

    double det = std::abs(transform.m00 * transform.m11 - transform.m01 * transform.m10);
    double scaleFactor = (det > 1e-6) ? std::sqrt(det) : 1.0;

    for (auto& stroke : strokes) {
        for (auto& seg : stroke.segments) {
            BLPoint p0 = transform.map_point(seg.p0.x, seg.p0.y);
            BLPoint p1 = transform.map_point(seg.p1.x, seg.p1.y);
            seg.p0 = Point2D(p0.x, p0.y);
            seg.p1 = Point2D(p1.x, p1.y);
            seg.width = static_cast<float>(seg.width * scaleFactor);
        }

        for (auto& pt : stroke.centerline) {
            BLPoint p = transform.map_point(pt.x, pt.y);
            pt.x = p.x;
            pt.y = p.y;
        }

        stroke.baseWidth *= scaleFactor;

        // Re-bake 2D closed polygon outline (BLPath) with transformed coordinates
        if (!stroke.segments.empty()) {
            std::vector<StrokeOutlineBuilder::InputPoint> pts;
            pts.reserve(stroke.segments.size() + 1);
            pts.push_back({ stroke.segments[0].p0.x, stroke.segments[0].p0.y, stroke.segments[0].width });
            for (const auto& s : stroke.segments) {
                pts.push_back({ s.p1.x, s.p1.y, s.width });
            }
            stroke.outlinePath = StrokeOutlineBuilder::BuildOutline(pts, CapType::Round, stroke.pattern);
        }
    }

    transform = BLMatrix2D::make_identity();
    renderDirty = true;
    UpdateBounds();
}

// =============================================================================
// 3. VECTOR RENDERING PASS
// =============================================================================

/**
 * @brief Renders the vector strokes into the given Blend2D context.
 *
 * Renders using BL_FILL_RULE_NON_ZERO to ensure overlapping loops (e.g. cursive writing)
 * fuse seamlessly without hollow cutouts.
 *
 * @param ctx Target Blend2D context
 * @param viewport Active canvas viewport for frustum culling
 */
void InkContainer::Render(BLContext& ctx, const Viewport& viewport) const {
    // 1. Visibility & Frustum Culling
    if (!isVisible || opacity <= 0.0f) return;
    if (!bounds.Intersects(viewport.bounds)) return;

    renderDirty = false;

    // 2. Isolate context state & apply transform
    ctx.save();
    ctx.apply_transform(transform);

    // 3. Winding Rule: Non-zero prevents holes at self-intersections
    ctx.set_fill_rule(BL_FILL_RULE_NON_ZERO);

    for (const auto& stroke : strokes) {
        if (stroke.outlinePath.is_empty() && stroke.segments.empty()) continue;

        // 4. Material Simulation (Highlighter vs Regular Pen)
        if (isHighlighter) {
            ctx.set_comp_op(BL_COMP_OP_MULTIPLY);
            ctx.set_fill_style(BLRgba32(stroke.color.r(), stroke.color.g(), stroke.color.b(), 0xFF));
        } else {
            ctx.set_comp_op(BL_COMP_OP_SRC_OVER);
            ctx.set_fill_style(stroke.color);
        }

        // 5. Fast-path: pre-baked closed polygon outline
        if (!stroke.outlinePath.is_empty()) {
            ctx.fill_path(stroke.outlinePath);
        } else {
            // 6. Fallback rendering for dynamic / unbaked strokes
            if (stroke.segments.size() == 1) {
                ctx.set_stroke_caps(BL_STROKE_CAP_ROUND);
                ctx.set_stroke_width(stroke.segments[0].width);
                ctx.stroke_line(stroke.segments[0].p0.x, stroke.segments[0].p0.y,
                                stroke.segments[0].p1.x, stroke.segments[0].p1.y);
            } else {
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

    ctx.restore();
}

// =============================================================================
// 4. DUPLICATION & PERSISTENCE
// =============================================================================

std::unique_ptr<CanvasObject> InkContainer::Clone() const {
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

// =============================================================================
// 5. ERASER SLICING
// =============================================================================

/**
 * @brief Point/slice eraser implementation: carves holes in strokes and splits them into fragments.
 *
 * @param worldX Center X of eraser circle in world mm
 * @param worldY Center Y of eraser circle in world mm
 * @param radius Radius of eraser circle in world mm
 * @param[out] outNewFragments Receives newly spawned split fragments beyond the primary container
 * @return true if any stroke was sliced or modified
 */
bool InkContainer::SliceStrokeAt(double worldX, double worldY, double radius,
                                 std::vector<std::shared_ptr<InkContainer>>& outNewFragments) {
    if (strokes.empty()) return false;

    // Broadphase test against container bounds
    AABB eraserAABB(worldX - radius, worldY - radius, worldX + radius, worldY + radius);
    if (!bounds.Intersects(eraserAABB)) return false;

    // Map circle center into object local space
    BLMatrix2D invTransform;
    BLMatrix2D::invert(invTransform, transform);
    BLPoint localCenter = invTransform.map_point(worldX, worldY);

    double scale = std::hypot(transform.m00, transform.m01);
    double localRadius = (scale > 1e-6) ? (radius / scale) : radius;

    std::vector<Stroke> newStrokes;
    bool anyModified = false;

    for (const auto& stroke : strokes) {
        std::vector<std::vector<Segment1D>> subChains;
        bool strokeModified = StrokeSlicer::SliceSegments(
            stroke.segments, localCenter.x, localCenter.y, localRadius, subChains);

        if (strokeModified) {
            anyModified = true;
            for (size_t subIdx = 0; subIdx < subChains.size(); ++subIdx) {
                auto& subChain = subChains[subIdx];
                if (subChain.empty()) continue;

                Stroke newStroke;
                newStroke.color = stroke.color;
                newStroke.baseWidth = stroke.baseWidth;
                newStroke.pattern = stroke.pattern;
                newStroke.segments = std::move(subChain);

                // Re-bake 2D closed polygon outline (BLPath) for each surviving sub-stroke
                std::vector<StrokeOutlineBuilder::InputPoint> pts;
                pts.reserve(newStroke.segments.size() + 1);
                pts.push_back({ newStroke.segments[0].p0.x, newStroke.segments[0].p0.y, newStroke.segments[0].width });
                for (const auto& s : newStroke.segments) {
                    pts.push_back({ s.p1.x, s.p1.y, s.width });
                }
                newStroke.outlinePath = StrokeOutlineBuilder::BuildOutline(pts, CapType::Round, newStroke.pattern);

                // First surviving fragment stays in this container; additional fragments become separate objects
                if (newStrokes.empty() && subIdx == 0) {
                    newStrokes.push_back(std::move(newStroke));
                } else {
                    auto fragContainer = std::make_shared<InkContainer>();
                    fragContainer->transform = this->transform;
                    fragContainer->isHighlighter = this->isHighlighter;
                    fragContainer->AddStroke(newStroke);
                    outNewFragments.push_back(fragContainer);
                }
            }
        } else {
            newStrokes.push_back(stroke);
        }
    }

    if (anyModified) {
        strokes = std::move(newStrokes);
        renderDirty = true;
        UpdateBounds();
        return true;
    }

    return false;
}

bool InkContainer::SliceStrokeAt(double worldX, double worldY, double radius) {
    std::vector<std::shared_ptr<InkContainer>> dummy;
    return SliceStrokeAt(worldX, worldY, radius, dummy);
}
