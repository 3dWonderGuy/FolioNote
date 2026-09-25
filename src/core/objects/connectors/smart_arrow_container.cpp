/**
 * @file smart_arrow_container.cpp
 * @brief Implementation of SmartArrowObject: two-point line and smart arrow connector.
 *
 * Mathematical Principles & Working Process:
 * 1. Routing Algorithms:
 *    - Straight: Direct linear interpolation between (x1, y1) and (x2, y2).
 *    - Curved (Cubic Bézier): Evaluates dominant orientation (|dx| >= |dy| vs |dy| > |dx|)
 *      to place horizontal or vertical control points at mid-span:
 *      C1 = (x1 + 0.5*dx, y1), C2 = (x2 - 0.5*dx, y2)
 *      producing smooth organic S-curves.
 *    - Elbow (Manhattan): Right-angle orthogonal stepped routing through 4 waypoints:
 *      P0 = (x1, y1) -> P1 = (x1 + 0.5*dx, y1) -> P2 = (x1 + 0.5*dx, y2) -> P3 = (x2, y2).
 *
 * 2. Hit-Testing:
 *    - Straight: Segment-point minimum distance test using vector projection:
 *      t = clamp((w·v) / (v·v), 0, 1), dist = ||P - (A + t*v)||.
 *    - Curved: 16-step polyline decomposition with segment distance checks.
 *    - Elbow: 3 orthogonal segment distance checks.
 *
 * 3. Arrowhead Apex Placement:
 *    - Displaces apex forward along local tangent: d_push = 0.55 * headSz (filled)
 *      or 0.45 * effWidth (open wireframe) so the line's rounded end cap sits cleanly inside
 *      the arrowhead body with zero apex poke-through.
 *
 * 4. Dashed Stroke Outlines:
 *    - Delegated to PathDasher::BuildDashedPath() to bypass Blend2D rasterizer limitations.
 */

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <algorithm>
#include <vector>

#include "core/objects/connectors/smart_arrow_container.hpp"
#include "core/objects/primitives/path_dasher.hpp"
#include "core/engine/canvas_transform.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Folio {

// =============================================================================
// CONSTRUCTORS
// =============================================================================

SmartArrowObject::SmartArrowObject() {
    type = ObjectType::Connector;
    UpdateBounds();
}

SmartArrowObject::SmartArrowObject(double sx, double sy, double ex, double ey)
    : x1(sx), y1(sy), x2(ex), y2(ey) {
    type = ObjectType::Connector;
    UpdateBounds();
}

// =============================================================================
// ROUTING GEOMETRY
// =============================================================================

void SmartArrowObject::GetCurvedControlPoints(Point2D& outC1, Point2D& outC2) const {
    double dx = x2 - x1;
    double dy = y2 - y1;
    if (std::abs(dx) >= std::abs(dy)) {
        outC1 = Point2D(x1 + dx * 0.5, y1);
        outC2 = Point2D(x2 - dx * 0.5, y2);
    } else {
        outC1 = Point2D(x1, y1 + dy * 0.5);
        outC2 = Point2D(x2, y2 - dy * 0.5);
    }
}

void SmartArrowObject::GetElbowWaypoints(Point2D outPoints[4]) const {
    double dx = x2 - x1;
    double dy = y2 - y1;
    if (std::abs(dx) >= std::abs(dy)) {
        outPoints[0] = Point2D(x1, y1);
        outPoints[1] = Point2D(x1 + dx * 0.5, y1);
        outPoints[2] = Point2D(x1 + dx * 0.5, y2);
        outPoints[3] = Point2D(x2, y2);
    } else {
        outPoints[0] = Point2D(x1, y1);
        outPoints[1] = Point2D(x1, y1 + dy * 0.5);
        outPoints[2] = Point2D(x2, y1 + dy * 0.5);
        outPoints[3] = Point2D(x2, y2);
    }
}

// =============================================================================
// BOUNDS & HIT TESTING
// =============================================================================

void SmartArrowObject::UpdateBounds() {
    double minX = (std::min)(x1, x2);
    double minY = (std::min)(y1, y2);
    double maxX = (std::max)(x1, x2);
    double maxY = (std::max)(y1, y2);

    if (connectorStyle == ConnectorStyle::Curved) {
        Point2D c1, c2;
        GetCurvedControlPoints(c1, c2);
        minX = (std::min)({minX, c1.x, c2.x});
        minY = (std::min)({minY, c1.y, c2.y});
        maxX = (std::max)({maxX, c1.x, c2.x});
        maxY = (std::max)({maxY, c1.y, c2.y});
    } else if (connectorStyle == ConnectorStyle::Elbow) {
        Point2D pts[4];
        GetElbowWaypoints(pts);
        for (int i = 0; i < 4; ++i) {
            minX = (std::min)(minX, pts[i].x);
            minY = (std::min)(minY, pts[i].y);
            maxX = (std::max)(maxX, pts[i].x);
            maxY = (std::max)(maxY, pts[i].y);
        }
    }

    // Safety padding for stroke width and arrowhead caps
    double pad = (std::max)(strokeWidth * 0.5, arrowHeadSize) + 2.5;
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
}

bool SmartArrowObject::HitTest(double worldX, double worldY) const {
    if (!isVisible || !isSelectable) return false;
    if (!bounds.Contains(worldX, worldY)) return false;

    // Fast-path: already selected arrow allows immediate interaction inside bounds
    if (isSelected) return true;

    // Transform query point into local object space
    BLMatrix2D inv;
    BLMatrix2D::invert(inv, transform);
    BLPoint lp = inv.map_point(worldX, worldY);
    Point2D p{lp.x, lp.y};

    double threshold = (std::max)(strokeWidth * 0.5 + 2.0, 3.5);
    double threshSq  = threshold * threshold;

    if (connectorStyle == ConnectorStyle::Straight) {
        return DistSqPointToSegment(p, Point2D(x1, y1), Point2D(x2, y2)) <= threshSq;
    } else if (connectorStyle == ConnectorStyle::Curved) {
        Point2D c1, c2;
        GetCurvedControlPoints(c1, c2);
        constexpr int steps = 16;
        Point2D prevPt(x1, y1);
        for (int s = 1; s <= steps; ++s) {
            double t = static_cast<double>(s) / steps;
            double u = 1.0 - t;
            Point2D currPt(
                u * u * u * x1 + 3.0 * u * u * t * c1.x + 3.0 * u * t * t * c2.x + t * t * t * x2,
                u * u * u * y1 + 3.0 * u * u * t * c1.y + 3.0 * u * t * t * c2.y + t * t * t * y2
            );
            if (DistSqPointToSegment(p, prevPt, currPt) <= threshSq) return true;
            prevPt = currPt;
        }
    } else if (connectorStyle == ConnectorStyle::Elbow) {
        Point2D pts[4];
        GetElbowWaypoints(pts);
        for (int i = 0; i < 3; ++i) {
            if (DistSqPointToSegment(p, pts[i], pts[i + 1]) <= threshSq) return true;
        }
    }

    return false;
}

// =============================================================================
// TRANSFORMS & GIZMO HANDLES
// =============================================================================

void SmartArrowObject::ApplyTransform(const BLMatrix2D& matrix) {
    transform.post_transform(matrix);
    UpdateBounds();
}

void SmartArrowObject::BakeTransform() {
    if (transform.m00 == 1.0 && transform.m11 == 1.0 &&
        transform.m01 == 0.0 && transform.m10 == 0.0 &&
        transform.m20 == 0.0 && transform.m21 == 0.0)
        return;

    BLPoint p1 = transform.map_point(x1, y1);
    BLPoint p2 = transform.map_point(x2, y2);
    x1 = p1.x; y1 = p1.y;
    x2 = p2.x; y2 = p2.y;
    transform = BLMatrix2D::make_identity();
    UpdateBounds();
}


// =============================================================================
// RENDERING
// =============================================================================

void SmartArrowObject::Render(BLContext& ctx, const Viewport& /*viewport*/) const {
    if (!isVisible) return;

    ctx.save();
    ctx.apply_transform(transform);

    // Stroke width invariance during active drag
    double det         = std::abs(transform.m00 * transform.m11
                                 - transform.m01 * transform.m10);
    double scaleFactor = (det > 1e-6) ? std::sqrt(det) : 1.0;
    double effWidth    = strokeWidth / scaleFactor;

    // Alpha lock: line outlines are always solid
    BLRgba32 col = strokeColor;
    col.setA(255);
    ctx.set_stroke_style(col);
    ctx.set_stroke_width(effWidth);
    ctx.set_stroke_caps(BL_STROKE_CAP_ROUND);
    ctx.set_stroke_join(BL_STROKE_JOIN_ROUND);

    // Construct raw connector path
    BLPath rawPath;
    double endAngle = 0.0;
    double startAngle = 0.0;
    if (connectorStyle == ConnectorStyle::Straight) {
        rawPath.move_to(x1, y1);
        rawPath.line_to(x2, y2);
        endAngle = std::atan2(y2 - y1, x2 - x1);
        startAngle = endAngle + M_PI;
    } else if (connectorStyle == ConnectorStyle::Curved) {
        Point2D c1, c2;
        GetCurvedControlPoints(c1, c2);
        rawPath.move_to(x1, y1);
        rawPath.cubic_to(c1.x, c1.y, c2.x, c2.y, x2, y2);
        endAngle = std::atan2(y2 - c2.y, x2 - c2.x);
        startAngle = std::atan2(y1 - c1.y, x1 - c1.x);
    } else if (connectorStyle == ConnectorStyle::Elbow) {
        Point2D pts[4];
        GetElbowWaypoints(pts);
        rawPath.move_to(pts[0].x, pts[0].y);
        rawPath.line_to(pts[1].x, pts[1].y);
        rawPath.line_to(pts[2].x, pts[2].y);
        rawPath.line_to(pts[3].x, pts[3].y);
        endAngle = std::atan2(pts[3].y - pts[2].y, pts[3].x - pts[2].x);
        startAngle = std::atan2(pts[0].y - pts[1].y, pts[0].x - pts[1].x);
    }

    // Render line stroke (solid or dashed)
    if (outlineType == ShapeOutlineType::Solid) {
        ctx.stroke_path(rawPath);
    } else {
        BLPath dashedPath;
        PathDasher::BuildDashedPath(rawPath, dashedPath, outlineType, effWidth);
        ctx.stroke_path(dashedPath);
    }

    // Arrowheads (rendered in local line direction angles)
    // Push arrowheads forward along tangent so the line segment end sits cleanly inside
    // the arrowhead cap rather than poking through the apex.
    double headSz = (std::max)(2.5, arrowHeadSize * strokeWidth * 0.75);

    if (endArrow != ArrowHeadType::None) {
        double pushDist = (endArrow == ArrowHeadType::Open) ? (effWidth * 0.45) : (headSz * 0.55);
        double arrowTipX = x2 + pushDist * std::cos(endAngle);
        double arrowTipY = y2 + pushDist * std::sin(endAngle);
        DrawArrowHead(ctx, arrowTipX, arrowTipY, endAngle, endArrow, headSz, strokeColor);
    }
    if (startArrow != ArrowHeadType::None) {
        double pushDist = (startArrow == ArrowHeadType::Open) ? (effWidth * 0.45) : (headSz * 0.55);
        double arrowTipX = x1 + pushDist * std::cos(startAngle);
        double arrowTipY = y1 + pushDist * std::sin(startAngle);
        DrawArrowHead(ctx, arrowTipX, arrowTipY, startAngle, startArrow, headSz, strokeColor);
    }

    ctx.restore();
}

// =============================================================================
// MAGNETIC ANCHOR SNAPPING
// =============================================================================

bool SmartArrowObject::FindSnapAnchor(const Point2D& queryPt,
                                      const std::vector<std::shared_ptr<CanvasObject>>& objects,
                                      Point2D& outAnchor,
                                      double thresholdMm,
                                      uint32_t excludeUid) {
    double closestDistSq = thresholdMm * thresholdMm;
    bool found = false;

    for (const auto& obj : objects) {
        if (!obj || !obj->isVisible || obj->uid == excludeUid) continue;
        // Only snap to 2D bounded objects (shapes, text boxes, images, tables, PDFs)
        if (obj->type == ObjectType::InkContainer || obj->type == ObjectType::Connector) continue;

        const AABB& b = obj->bounds;
        double w = b.maxX - b.minX;
        double h = b.maxY - b.minY;
        if (w < 1.0 || h < 1.0) continue;

        Point2D anchors[5] = {
            Point2D(b.minX + w * 0.5, b.minY + h * 0.5), // Center
            Point2D(b.minX + w * 0.5, b.minY),           // Top
            Point2D(b.minX + w * 0.5, b.maxY),           // Bottom
            Point2D(b.minX,           b.minY + h * 0.5), // Left
            Point2D(b.maxX,           b.minY + h * 0.5)  // Right
        };

        for (int i = 0; i < 5; ++i) {
            double dSq = (queryPt.x - anchors[i].x) * (queryPt.x - anchors[i].x) +
                         (queryPt.y - anchors[i].y) * (queryPt.y - anchors[i].y);
            if (dSq < closestDistSq) {
                closestDistSq = dSq;
                outAnchor = anchors[i];
                found = true;
            }
        }
    }
    return found;
}

std::unique_ptr<CanvasObject> SmartArrowObject::Clone() const {
    return std::make_unique<SmartArrowObject>(*this);
}

// =============================================================================
// PRIVATE HELPERS
// =============================================================================

double SmartArrowObject::DistSqPointToSegment(const Point2D& p,
                                             const Point2D& a,
                                             const Point2D& b) {
    double vx = b.x - a.x, vy = b.y - a.y;
    double wx = p.x - a.x, wy = p.y - a.y;
    double c1 = wx * vx + wy * vy;
    if (c1 <= 0.0) return wx * wx + wy * wy;
    double c2 = vx * vx + vy * vy;
    if (c2 <= c1) return (p.x - b.x) * (p.x - b.x) + (p.y - b.y) * (p.y - b.y);
    double t     = c1 / c2;
    double projX = a.x + t * vx;
    double projY = a.y + t * vy;
    return (p.x - projX) * (p.x - projX) + (p.y - projY) * (p.y - projY);
}

void SmartArrowObject::DrawArrowHead(BLContext& ctx,
                                     double tipX, double tipY, double angleRad,
                                     ArrowHeadType headType, double sizeMm,
                                     const BLRgba32& color) {
    if (headType == ArrowHeadType::None || sizeMm <= 0.1) return;

    const double cosA  = std::cos(angleRad);
    const double sinA  = std::sin(angleRad);
    const double perpX = -sinA;
    const double perpY =  cosA;

    ctx.save();
    ctx.set_stroke_style(color);
    ctx.set_fill_style(color);

    switch (headType) {
        case ArrowHeadType::Triangle: {
            double bx = tipX - sizeMm * cosA;
            double by = tipY - sizeMm * sinA;
            double hw = sizeMm * 0.55;
            BLPath arr;
            arr.move_to(tipX, tipY);
            arr.line_to(bx + hw * perpX, by + hw * perpY);
            arr.line_to(bx - hw * perpX, by - hw * perpY);
            arr.close();
            ctx.fill_path(arr);
            break;
        }
        case ArrowHeadType::Stealth: {
            double bx = tipX - sizeMm * cosA;
            double by = tipY - sizeMm * sinA;
            double ix = tipX - sizeMm * 0.75 * cosA;
            double iy = tipY - sizeMm * 0.75 * sinA;
            double hw = sizeMm * 0.65;
            BLPath arr;
            arr.move_to(tipX, tipY);
            arr.line_to(bx + hw * perpX, by + hw * perpY);
            arr.line_to(ix, iy);
            arr.line_to(bx - hw * perpX, by - hw * perpY);
            arr.close();
            ctx.fill_path(arr);
            break;
        }
        case ArrowHeadType::Open: {
            double bx = tipX - sizeMm * cosA;
            double by = tipY - sizeMm * sinA;
            double hw = sizeMm * 0.6;
            BLPath arr;
            arr.move_to(bx + hw * perpX, by + hw * perpY);
            arr.line_to(tipX, tipY);
            arr.line_to(bx - hw * perpX, by - hw * perpY);
            ctx.set_stroke_width((std::max)(1.0, sizeMm * 0.25));
            ctx.set_stroke_caps(BL_STROKE_CAP_ROUND);
            ctx.stroke_path(arr);
            break;
        }
        case ArrowHeadType::Circle: {
            ctx.fill_circle(tipX, tipY, sizeMm * 0.4);
            break;
        }
        default: break;
    }
    ctx.restore();
}

} // namespace Folio
