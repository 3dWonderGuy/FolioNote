#pragma once
/**
 * @file smart_arrow_container.hpp
 * @brief Two-point line and arrow connector object with draggable endpoint handles.
 *
 * SmartArrowObject represents a directed line segment on the canvas. Unlike
 * closed shapes (ShapeObject), it:
 *  - Has exactly 2 control points: start (x1, y1) and end (x2, y2)
 *  - Exposes 2 custom gizmo handles (endpoint drag) instead of 8 bounding-box grips
 *  - Supports 4 arrowhead styles on each endpoint (None, Triangle, Stealth, Open, Circle)
 *  - Cannot have a fill (it is an open path by definition)
 *  - Uses the same outline styles as ShapeObject (Solid, Dashed, Dotted, DashDot)
 *
 * Scalability note: to add new connector routing (Curved, Elbow), add a
 * ConnectorStyle field here and a corresponding case in Render(). No other
 * files require modification.
 *
 * Transform Contract:
 *  - ApplyTransform(): accumulates in BLMatrix2D without mutating x1/y1/x2/y2.
 *  - BakeTransform(): maps endpoints through the matrix, resets to identity.
 *  - Called by SelectionGizmo::OnPointerUp() same as ShapeObject.
 *
 * Gizmo Handles:
 *  - customId 0 = start endpoint (x1, y1)
 *  - customId 1 = end   endpoint (x2, y2)
 *  - OnGizmoHandleDrag updates the dragged point directly (no matrix needed
 *    for individual endpoint adjustment — it is always a world translation).
 */

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <algorithm>
#include <memory>
#include <vector>

#include <blend2d/blend2d.h>

#include "core/objects/canvas_object.hpp"
#include "core/objects/connectors/connector_types.hpp"
#include "core/objects/primitives/shape_types.hpp"  // ShapeOutlineType reuse
#include "core/objects/primitives/path_dasher.hpp"
#include "core/spatial/aabb.hpp"
#include "core/engine/canvas_transform.hpp"

namespace Folio {

/**
 * @brief Directed line segment connector with optional arrowheads at each endpoint.
 *
 * Coordinates are in world-space millimeters. The line is defined by:
 *   start = (x1, y1)
 *   end   = (x2, y2)
 *
 * Any combination of start/end arrowheads can be active simultaneously.
 * strokeWidth is in world mm and visually compensated for active zoom/scale.
 */
class SmartArrowObject : public CanvasObject {
public:
    // =========================================================================
    // FIELDS
    // =========================================================================

    double x1 = 0.0;   ///< Start point X (world mm)
    double y1 = 0.0;   ///< Start point Y (world mm)
    double x2 = 60.0;  ///< End point X (world mm)
    double y2 = 0.0;   ///< End point Y (world mm)

    BLRgba32        strokeColor{0x18, 0x1A, 0x20, 0xFF};
    double          strokeWidth  = 1.0;          ///< Line thickness in world mm
    ShapeOutlineType outlineType = ShapeOutlineType::Solid;

    ArrowHeadType   startArrow   = ArrowHeadType::None;
    ArrowHeadType   endArrow     = ArrowHeadType::Triangle;
    double          arrowHeadSize = 4.0;         ///< Arrowhead size in world mm

    ConnectorStyle  connectorStyle = ConnectorStyle::Straight;

    // =========================================================================
    // CONSTRUCTORS
    // =========================================================================

    SmartArrowObject() {
        type = ObjectType::Connector;
        UpdateBounds();
    }

    /**
     * @param sx Start X, @param sy Start Y, @param ex End X, @param ey End Y (world mm)
     */
    SmartArrowObject(double sx, double sy, double ex, double ey)
        : x1(sx), y1(sy), x2(ex), y2(ey)
    {
        type = ObjectType::Connector;
        UpdateBounds();
    }

    // =========================================================================
    // BOUNDS & SPATIAL
    // =========================================================================

    /**
     * @brief Computes cubic Bézier control points for Curved connector routing.
     *
     * Mathematical derivation:
     * For start point P1 = (x1, y1) and end point P2 = (x2, y2), we determine the
     * dominant orientation:
     *   dx = x2 - x1,  dy = y2 - y1
     * 
     * If horizontal span >= vertical span (|dx| >= |dy|):
     *   Tangents are horizontal:
     *   C1 = (x1 + 0.5 * dx, y1)
     *   C2 = (x2 - 0.5 * dx, y2)
     * If vertical span > horizontal span (|dy| > |dx|):
     *   Tangents are vertical:
     *   C1 = (x1, y1 + 0.5 * dy)
     *   C2 = (x2, y2 - 0.5 * dy)
     *
     * This creates a smooth, organic S-curve between endpoints common in node editors.
     */
    void GetCurvedControlPoints(Point2D& outC1, Point2D& outC2) const {
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

    /**
     * @brief Computes orthogonal waypoints for Elbow (Manhattan) connector routing.
     *
     * Stepped routing creates a 3-segment orthogonal path:
     * If |dx| >= |dy|:
     *   P0 = (x1, y1)
     *   P1 = (x1 + 0.5*dx, y1)
     *   P2 = (x1 + 0.5*dx, y2)
     *   P3 = (x2, y2)
     * If |dy| > |dx|:
     *   P0 = (x1, y1)
     *   P1 = (x1, y1 + 0.5*dy)
     *   P2 = (x2, y1 + 0.5*dy)
     *   P3 = (x2, y2)
     */
    void GetElbowWaypoints(Point2D outPoints[4]) const {
        double dx = x2 - x1;
        double dy = y2 - y1;
        outPoints[0] = Point2D(x1, y1);
        outPoints[3] = Point2D(x2, y2);
        if (std::abs(dx) >= std::abs(dy)) {
            double midX = x1 + dx * 0.5;
            outPoints[1] = Point2D(midX, y1);
            outPoints[2] = Point2D(midX, y2);
        } else {
            double midY = y1 + dy * 0.5;
            outPoints[1] = Point2D(x1, midY);
            outPoints[2] = Point2D(x2, midY);
        }
    }

    /**
     * @brief AABB expanded by stroke half-width and transform envelope.
     *
     * Encompasses all control points or waypoints based on the active connectorStyle.
     */
    void UpdateBounds() override {
        double hs  = strokeWidth * 0.5 + 2.0; // Margin includes arrowhead geometry
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

        minX -= hs;
        minY -= hs;
        maxX += hs;
        maxY += hs;

        BLPoint p[4] = {
            transform.map_point(minX, minY),
            transform.map_point(maxX, minY),
            transform.map_point(maxX, maxY),
            transform.map_point(minX, maxY)
        };
        double bMinX = p[0].x, bMaxX = p[0].x;
        double bMinY = p[0].y, bMaxY = p[0].y;
        for (int i = 1; i < 4; ++i) {
            bMinX = (std::min)(bMinX, p[i].x);
            bMaxX = (std::max)(bMaxX, p[i].x);
            bMinY = (std::min)(bMinY, p[i].y);
            bMaxY = (std::max)(bMaxY, p[i].y);
        }
        bounds = AABB(bMinX, bMinY, bMaxX, bMaxY);
    }

    /**
     * @brief Point-to-connector hit test.
     *
     * Depending on connectorStyle:
     *   - Straight: single segment distance
     *   - Elbow: minimum distance to the 3 orthogonal segments
     *   - Curved: sampled cubic Bézier polyline evaluation (16 steps)
     */
    bool HitTest(double worldXQuery, double worldYQuery) const override {
        if (!bounds.Contains(worldXQuery, worldYQuery)) return false;

        BLMatrix2D inv;
        if (BLMatrix2D::invert(inv, transform) != BL_SUCCESS)
            inv = BLMatrix2D::make_identity();
        BLPoint lp = inv.map_point(worldXQuery, worldYQuery);
        Point2D q{lp.x, lp.y};

        double hitTol = (std::max)(strokeWidth * 0.5, 3.0);
        double hitTolSq = hitTol * hitTol;

        if (connectorStyle == ConnectorStyle::Straight) {
            Point2D a{x1, y1}, b{x2, y2};
            return DistSqPointToSegment(q, a, b) <= hitTolSq;
        } else if (connectorStyle == ConnectorStyle::Elbow) {
            Point2D pts[4];
            GetElbowWaypoints(pts);
            for (int i = 0; i < 3; ++i) {
                if (DistSqPointToSegment(q, pts[i], pts[i + 1]) <= hitTolSq) return true;
            }
            return false;
        } else if (connectorStyle == ConnectorStyle::Curved) {
            Point2D c1, c2;
            GetCurvedControlPoints(c1, c2);

            // 16-step cubic Bézier sampling: B(t) = (1-t)^3 P0 + 3(1-t)^2 t C1 + 3(1-t) t^2 C2 + t^3 P1
            Point2D prevPt{x1, y1};
            constexpr int STEPS = 16;
            for (int i = 1; i <= STEPS; ++i) {
                double t = static_cast<double>(i) / STEPS;
                double u = 1.0 - t;
                double tt = t * t;
                double uu = u * u;
                double uuu = uu * u;
                double ttt = tt * t;

                Point2D curPt{
                    uuu * x1 + 3.0 * uu * t * c1.x + 3.0 * u * tt * c2.x + ttt * x2,
                    uuu * y1 + 3.0 * uu * t * c1.y + 3.0 * u * tt * c2.y + ttt * y2
                };
                if (DistSqPointToSegment(q, prevPt, curPt) <= hitTolSq) return true;
                prevPt = curPt;
            }
            return false;
        }

        return false;
    }

    bool Intersects(const AABB& selectionBounds) const override {
        return bounds.Intersects(selectionBounds);
    }

    // =========================================================================
    // TRANSFORM
    // =========================================================================

    /**
     * @brief Accumulates affine matrix without touching x1/y1/x2/y2.
     * No compounding — gizmo always calls from initialTransform snapshot.
     */
    void ApplyTransform(const BLMatrix2D& matrix) override {
        transform.post_transform(matrix);
        UpdateBounds();
    }

    /**
     * @brief Maps both endpoints through the accumulated transform, resets to identity.
     *
     * Math: new_p = M * p  (where M is the accumulated 3x3 affine matrix)
     *   x1' = m00*x1 + m10*y1 + m20
     *   y1' = m01*x1 + m11*y1 + m21  (etc for x2, y2)
     */
    void BakeTransform() override {
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

    // =========================================================================
    // CUSTOM GIZMO HANDLES
    // =========================================================================

    /**
     * @brief Returns 2 endpoint handles instead of the standard 8-point AABB grips.
     *
     * Handle 0: start point (x1, y1)
     * Handle 1: end   point (x2, y2)
     *
     * The SelectionGizmo renders these as circular grips and calls
     * OnGizmoHandleDrag() when the user drags one.
     */
    bool GetCustomGizmoHandles(std::vector<GizmoHandle>& outHandles,
                                const CanvasTransform& /*transform*/) const override {
        outHandles.clear();

        GizmoHandle h0;
        h0.customId = 0;
        h0.worldPos = Point2D(x1, y1);
        h0.role     = HandleRole::Custom;
        outHandles.push_back(h0);

        GizmoHandle h1;
        h1.customId = 1;
        h1.worldPos = Point2D(x2, y2);
        h1.role     = HandleRole::Custom;
        outHandles.push_back(h1);

        return true;
    }

    /**
     * @brief Adjusts the dragged endpoint while keeping the other fixed.
     *
     * For endpoint 0 (start): moves (x1, y1) by worldDelta.
     * For endpoint 1 (end):   moves (x2, y2) by worldDelta.
     * The delta is already in world-space (provided by SelectionGizmo).
     */
    bool OnGizmoHandleDrag(int customId,
                            const Point2D& /*worldPos*/,
                            const Point2D& worldDelta) override {
        if (customId == 0) {
            x1 += worldDelta.x;
            y1 += worldDelta.y;
            UpdateBounds();
            return true;
        } else if (customId == 1) {
            x2 += worldDelta.x;
            y2 += worldDelta.y;
            UpdateBounds();
            return true;
        }
        return false;
    }

    // =========================================================================
    // RENDERING
    // =========================================================================

    /**
     * @brief Renders the connector path (Straight, Curved, or Elbow) and arrowheads.
     *
     * Stroke width is compensated by 1/sqrt(det(transform)) to stay visually
     * invariant during active gizmo scale drag.
     */
    void Render(BLContext& ctx, const Viewport& /*viewport*/) const override {
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

    // =========================================================================
    // MAGNETIC ANCHOR SNAPPING
    // =========================================================================

    /**
     * @brief Finds the closest connection anchor point on candidate canvas objects.
     *
     * Evaluates 5 cardinal anchor points on each object's bounding box:
     *   1. Center:        (minX + 0.5*W, minY + 0.5*H)
     *   2. Top Center:    (minX + 0.5*W, minY)
     *   3. Bottom Center: (minX + 0.5*W, maxY)
     *   4. Left Center:   (minX,         minY + 0.5*H)
     *   5. Right Center:  (maxX,         minY + 0.5*H)
     *
     * @param queryPt World position of the cursor / endpoint
     * @param objects List of canvas objects on active page
     * @param outAnchor Populated with closest anchor coordinates if snapped
     * @param thresholdMm Snap capture radius in world millimeters (default 6.0 mm)
     * @param excludeUid Object UID to exclude (e.g. self)
     * @return true if an anchor was found within thresholdMm, false otherwise.
     */
    static bool FindSnapAnchor(const Point2D& queryPt,
                               const std::vector<std::shared_ptr<CanvasObject>>& objects,
                               Point2D& outAnchor,
                               double thresholdMm = 6.0,
                               uint32_t excludeUid = 0) {
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

    // =========================================================================
    // DUPLICATION & PERSISTENCE
    // =========================================================================

    std::unique_ptr<CanvasObject> Clone() const override {
        return std::make_unique<SmartArrowObject>(*this);
    }

    void Serialize(Serializer& /*writer*/) const override {}
    void Deserialize(Deserializer& /*reader*/) override {}

private:
    // =========================================================================
    // PRIVATE HELPERS
    // =========================================================================

    /**
     * @brief Squared Euclidean distance from point P to segment [A, B].
     *
     * Math:
     *   v = B-A,  w = P-A
     *   t = clamp((w·v)/(v·v), 0, 1)
     *   proj = A + t*v
     *   return ||P - proj||²
     */
    static double DistSqPointToSegment(const Point2D& p,
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

    /**
     * @brief Draws a directional arrowhead at a line endpoint.
     *
     * Forward direction = (cos θ, sin θ).
     * Perpendicular     = (-sin θ, cos θ).
     * See connector_types.hpp ArrowHeadType for style descriptions.
     */
    static void DrawArrowHead(BLContext& ctx,
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
};

} // namespace Folio
