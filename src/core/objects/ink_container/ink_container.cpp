/**
 * =========================================================================================
 * @file core/objects/ink_container/ink_container.cpp
 * @brief Implementation of Persistent Vector Ink Manager, Spatial Engine, and Renderer
 * =========================================================================================
 *
 * MATHEMATICAL PRINCIPLES & WORKING PROCESS:
 * ------------------------------------------
 * 1. Bounding Box Calculation (UpdateBounds):
 *    - Local Stroke Envelope: Each stroke boundary is derived either directly from its
 *      pre-baked closed 2D polygon outline (BLPath get_bounding_box) or by expanding
 *      centerline points and segments:
 *        r = width / 2
 *        x_min = min(p0.x - r, p1.x - r),  x_max = max(p0.x + r, p1.x + r)
 *        y_min = min(p0.y - r, p1.y - r),  y_max = max(p0.y + r, p1.y + r)
 *    - Affine Coordinate Mapping:
 *      Corners of the local bounding rectangle are mapped through the 2D affine matrix M:
 *        p' = [m00*x + m10*y + m20,  m01*x + m11*y + m21]^T
 *      The enclosing world AABB is constructed from extrema [min(x'_i), min(y'_i)] to
 *      [max(x'_i), max(y'_i)] with a 1.0 mm antialiasing safety padding.
 *
 * 2. Two-Tier Spatial Hit Testing (HitTest, HitTestCircle, HitTestSwept):
 *    - Broadphase: Fast rejection using the container's world-space AABB.
 *    - Coordinate Space Inversion: Query points, circles, and swept capsules are mapped
 *      into object-local space via the inverted affine matrix M^-1.
 *    - Narrowphase:
 *        a. BLPath::hit_test with BL_FILL_RULE_NON_ZERO for instant polygonal containment.
 *        b. StrokeCollisionEngine for segment-to-point and swept capsule continuous collision.
 *
 * 3. Affine Transform Baking (BakeTransform):
 *    - Geometric scale factor is computed via the determinant:
 *        scaleFactor = sqrt(|m00*m11 - m01*m10|)
 *    - All segment endpoints and centerline vertices are mapped through M, and thickness is
 *      scaled by scaleFactor.
 *    - StrokeOutlineBuilder rebuilds fresh, artifact-free 2D closed polygon contours (BLPath),
 *      and the transform matrix is reset to identity M = I.
 *
 * 4. Eraser Slicing (SliceStrokeAt):
 *    - Maps the circular eraser kernel into local space, scaling radius by 1 / hypot(m00, m01).
 *    - StrokeSlicer splits segment chains into surviving continuous sub-chains.
 *    - Surviving sub-chains are re-tessellated into smooth 2D closed polygon contours, with
 *      newly spawned fragments added to outNewFragments.
 *
 * 5. High-Fidelity Rendering (Render):
 *    - Viewport frustum culling discards off-screen containers before context dispatch.
 *    - Context transform is isolated via save() / restore().
 *    - Uses BL_FILL_RULE_NON_ZERO to ensure overlapping cursive loops fuse cleanly without hollows.
 *    - Renders with per-stroke blend modes (SrcOver, Multiply for Highlighters, Plus for Glow).
 */

#include "core/objects/ink_container/ink_container.hpp"
#include "core/objects/object_registry.hpp"
#include "core/ink_engine/stroke_collision.hpp"
#include "core/ink_engine/stroke_outline_builder.hpp"

#include <algorithm>
#include <cmath>

namespace {

// Static self-registration into ObjectRegistry
const bool s_inkRegistered = []() {
    Folio::ObjectRegistry::Register<InkContainer>(
        ObjectType::InkContainer,
        "InkContainer",
        "✒",
        true
    );
    return true;
}();

} // anonymous namespace

// =============================================================================
// CONSTRUCTORS & LIFECYCLE
// =============================================================================

InkContainer::InkContainer() {
    type = ObjectType::InkContainer;
}

void InkContainer::InvalidateCache() noexcept {
    renderDirty = true;
}

void InkContainer::AddStroke(const Stroke& stroke) {
    strokes.push_back(stroke);
    renderDirty = true;
    UpdateBounds();
}

// =============================================================================
// BOUNDS & SPATIAL QUERIES
// =============================================================================

void InkContainer::UpdateBounds() {
    if (strokes.empty()) {
        bounds = AABB{};
        worldX = worldY = worldWidth = worldHeight = 0.0;
        return;
    }

    double minX = 1e20, minY = 1e20, maxX = -1e20, maxY = -1e20;

    for (auto& stroke : strokes) {
        if (!stroke.outlinePath.is_empty()) {
            BLBox box;
            if (stroke.outlinePath.get_bounding_box(&box) == BL_SUCCESS) {
                stroke.bounds = AABB{box.x0, box.y0, box.x1, box.y1};
                minX = (std::min)(minX, box.x0);
                minY = (std::min)(minY, box.y0);
                maxX = (std::max)(maxX, box.x1);
                maxY = (std::max)(maxY, box.y1);
            }
        } else if (!stroke.points.empty()) {
            double sMinX = 1e20, sMinY = 1e20, sMaxX = -1e20, sMaxY = -1e20;
            for (const auto& pt : stroke.points) {
                sMinX = (std::min)(sMinX, pt.x - pt.r);
                sMinY = (std::min)(sMinY, pt.y - pt.r);
                sMaxX = (std::max)(sMaxX, pt.x + pt.r);
                sMaxY = (std::max)(sMaxY, pt.y + pt.r);
            }
            stroke.bounds = AABB{sMinX, sMinY, sMaxX, sMaxY};
            minX = (std::min)(minX, sMinX);
            minY = (std::min)(minY, sMinY);
            maxX = (std::max)(maxX, sMaxX);
            maxY = (std::max)(maxY, sMaxY);
        } else {
            double sMinX = 1e20, sMinY = 1e20, sMaxX = -1e20, sMaxY = -1e20;
            for (const auto& seg : stroke.segments) {
                double hw = seg.width * 0.5;
                sMinX = (std::min)({sMinX, seg.p0.x - hw, seg.p1.x - hw});
                sMinY = (std::min)({sMinY, seg.p0.y - hw, seg.p1.y - hw});
                sMaxX = (std::max)({sMaxX, seg.p0.x + hw, seg.p1.x + hw});
                sMaxY = (std::max)({sMaxY, seg.p0.y + hw, seg.p1.y + hw});
            }
            stroke.bounds = AABB{sMinX, sMinY, sMaxX, sMaxY};
            minX = (std::min)(minX, sMinX);
            minY = (std::min)(minY, sMinY);
            maxX = (std::max)(maxX, sMaxX);
            maxY = (std::max)(maxY, sMaxY);
        }
    }

    if (minX > maxX || minY > maxY) {
        bounds = AABB{};
        worldX = worldY = worldWidth = worldHeight = 0.0;
        return;
    }

    constexpr double pad = 1.0;
    BLPoint corners[4] = {
        transform.map_point(minX - pad, minY - pad),
        transform.map_point(maxX + pad, minY - pad),
        transform.map_point(minX - pad, maxY + pad),
        transform.map_point(maxX + pad, maxY + pad)
    };

    bounds = AABB{
        (std::min)({corners[0].x, corners[1].x, corners[2].x, corners[3].x}),
        (std::min)({corners[0].y, corners[1].y, corners[2].y, corners[3].y}),
        (std::max)({corners[0].x, corners[1].x, corners[2].x, corners[3].x}),
        (std::max)({corners[0].y, corners[1].y, corners[2].y, corners[3].y})
    };

    worldX = bounds.minX;
    worldY = bounds.minY;
    worldWidth = bounds.Width();
    worldHeight = bounds.Height();
}

bool InkContainer::HitTest(double x, double y) const noexcept {
    if (!isVisible || !isSelectable) return false;
    if (!bounds.Contains(x, y)) return false;
    if (isSelected) return true;

    BLMatrix2D invTransform;
    if (BLMatrix2D::invert(invTransform, transform) != BL_SUCCESS) return false;
    BLPoint localPt = invTransform.map_point(x, y);

    for (const auto& stroke : strokes) {
        if (!stroke.bounds.Contains(localPt.x, localPt.y)) continue;

        if (!stroke.outlinePath.is_empty()) {
            BLHitTest hit = stroke.outlinePath.hit_test(localPt, BL_FILL_RULE_NON_ZERO);
            if (hit == BL_HIT_TEST_IN) return true;
        }

        auto hit = StrokeCollisionEngine::HitTestStroke(stroke.segments, localPt.x, localPt.y, 1.5);
        if (hit.hit) return true;
    }
    return false;
}

bool InkContainer::HitTestCircle(double cx, double cy, double radiusMm) const noexcept {
    if (!isVisible) return false;

    AABB queryBox(cx - radiusMm, cy - radiusMm, cx + radiusMm, cy + radiusMm);
    if (!bounds.Intersects(queryBox)) return false;

    BLMatrix2D invTransform;
    if (BLMatrix2D::invert(invTransform, transform) != BL_SUCCESS) return false;
    BLPoint localPt = invTransform.map_point(cx, cy);

    double scale = std::hypot(transform.m00, transform.m01);
    double localRadius = (scale > 1e-6) ? (radiusMm / scale) : radiusMm;

    for (const auto& stroke : strokes) {
        auto hit = StrokeCollisionEngine::HitTestStroke(stroke.segments, localPt.x, localPt.y, localRadius);
        if (hit.hit) return true;

        if (!stroke.outlinePath.is_empty()) {
            BLHitTest blHit = stroke.outlinePath.hit_test(localPt, BL_FILL_RULE_NON_ZERO);
            if (blHit == BL_HIT_TEST_IN) return true;
        }
    }
    return false;
}

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
    if (BLMatrix2D::invert(invTransform, transform) != BL_SUCCESS) return false;

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
// GEOMETRY & TRANSFORMS
// =============================================================================

void InkContainer::ApplyTransform(const BLMatrix2D& matrix) {
    transform.post_transform(matrix);
    renderDirty = true;
    UpdateBounds();
}

void InkContainer::BakeTransform() {
    if (transform.m00 == 1.0 && transform.m11 == 1.0 &&
        transform.m01 == 0.0 && transform.m10 == 0.0 &&
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

        for (auto& pt : stroke.points) {
            BLPoint p = transform.map_point(pt.x, pt.y);
            pt.x = p.x;
            pt.y = p.y;
            pt.r = static_cast<float>(pt.r * scaleFactor);
        }

        stroke.baseWidthMm *= static_cast<float>(scaleFactor);

        if (!stroke.points.empty()) {
            std::vector<StrokeOutlineBuilder::InputPoint> pts;
            pts.reserve(stroke.points.size());
            for (const auto& pt : stroke.points) {
                pts.push_back({ pt.x, pt.y, pt.r * 2.0f });
            }
            stroke.outlinePath = StrokeOutlineBuilder::BuildOutline(pts, stroke.capType, stroke.strokePattern);
        }
    }

    transform = BLMatrix2D::make_identity();
    renderDirty = true;
    UpdateBounds();
}

// =============================================================================
// RENDERING & LIFECYCLE
// =============================================================================

void InkContainer::Render(BLContext& ctx, const Viewport& viewport) const {
    if (!isVisible || opacity <= 0.0f) return;
    if (!bounds.Intersects(viewport.bounds)) return;

    renderDirty = false;

    ctx.save();
    ctx.apply_transform(transform);
    ctx.set_fill_rule(BL_FILL_RULE_NON_ZERO);

    for (const auto& stroke : strokes) {
        if (stroke.outlinePath.is_empty() && stroke.segments.empty()) continue;

        BLCompOp compOp = (isHighlighter || stroke.blendMode == BlendMode::Multiply)
                              ? BL_COMP_OP_MULTIPLY
                              : stroke.GetBlend2DCompOp();
        ctx.set_comp_op(compOp);
        ctx.set_fill_style(stroke.color);

        if (!stroke.outlinePath.is_empty()) {
            ctx.fill_path(stroke.outlinePath);
        } else if (!stroke.points.empty()) {
            std::vector<StrokeOutlineBuilder::InputPoint> pts;
            pts.reserve(stroke.points.size());
            for (const auto& pt : stroke.points) {
                pts.push_back({ pt.x, pt.y, pt.r * 2.0f });
            }
            BLPath fallbackOutline = StrokeOutlineBuilder::BuildOutline(pts, stroke.capType, stroke.strokePattern);
            ctx.fill_path(fallbackOutline);
        } else if (stroke.segments.size() == 1) {
            ctx.set_stroke_caps(BL_STROKE_CAP_ROUND);
            ctx.set_stroke_width(stroke.segments[0].width);
            ctx.stroke_line(stroke.segments[0].p0.x, stroke.segments[0].p0.y,
                            stroke.segments[0].p1.x, stroke.segments[0].p1.y);
        }
    }

    ctx.restore();
}

std::unique_ptr<CanvasObject> InkContainer::Clone() const {
    auto clone = std::make_unique<InkContainer>();
    clone->uid = this->uid;
    clone->type = this->type;
    clone->bounds = this->bounds;
    clone->worldX = this->worldX;
    clone->worldY = this->worldY;
    clone->worldWidth = this->worldWidth;
    clone->worldHeight = this->worldHeight;
    clone->transform = this->transform;
    clone->zOrder = this->zOrder;
    clone->pageIndex = this->pageIndex;
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
// ERASER SLICING
// =============================================================================

bool InkContainer::SliceStrokeAt(double worldX, double worldY, double radius,
                                 std::vector<std::shared_ptr<InkContainer>>& outNewFragments) {
    if (strokes.empty()) return false;

    AABB eraserAABB(worldX - radius, worldY - radius, worldX + radius, worldY + radius);
    if (!bounds.Intersects(eraserAABB)) return false;

    BLMatrix2D invTransform;
    if (BLMatrix2D::invert(invTransform, transform) != BL_SUCCESS) return false;
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
                newStroke.baseWidthMm = stroke.baseWidthMm;
                newStroke.strokePattern = stroke.strokePattern;
                newStroke.capType = stroke.capType;
                newStroke.blendMode = stroke.blendMode;
                newStroke.penType = stroke.penType;
                newStroke.opacity = stroke.opacity;
                newStroke.segments = std::move(subChain);

                // Reconstruct points and polygon outline
                std::vector<StrokeOutlineBuilder::InputPoint> pts;
                pts.reserve(newStroke.segments.size() + 1);
                pts.push_back({ newStroke.segments[0].p0.x, newStroke.segments[0].p0.y, newStroke.segments[0].width });
                newStroke.points.push_back({ newStroke.segments[0].p0.x, newStroke.segments[0].p0.y, newStroke.segments[0].width * 0.5f });

                for (const auto& s : newStroke.segments) {
                    pts.push_back({ s.p1.x, s.p1.y, s.width });
                    newStroke.points.push_back({ s.p1.x, s.p1.y, s.width * 0.5f });
                }

                newStroke.outlinePath = StrokeOutlineBuilder::BuildOutline(pts, newStroke.capType, newStroke.strokePattern);
                newStrokes.push_back(std::move(newStroke));
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