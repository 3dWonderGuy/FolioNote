#pragma once
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <cmath>
#include <blend2d/blend2d.h>
#include "core/objects/canvas_object.hpp"
#include "core/spatial/aabb.hpp"
#include <imgui.h>

namespace Folio {

/**
 * @brief Categorization of 2D geometric vector shape primitives.
 */
enum class ShapeType : uint8_t {
    Rectangle = 0,
    RoundedRectangle = 1,
    Ellipse = 2,
    Triangle = 3,           ///< Isosceles triangle
    Diamond = 4,            ///< Preserved for backward file compatibility
    Star = 5,
    Arrow = 6,              ///< Preserved for backward file compatibility
    DoubleArrow = 7,        ///< Preserved for backward file compatibility
    Line = 8,               ///< Two-point linear segment with custom endpoint handles
    Hexagon = 9,            ///< Regular polygon (param1 = number of sides, default 6)
    Heart = 10,
    Cloud = 11,
    Circle = 12,            ///< Center-outward symmetric circle
    RightTriangle = 13,     ///< 90-degree right-angled triangle
    LineArrow = 14,         ///< Two-point line segment with customizable directional arrowheads
    RegularPolygon = 15     ///< Generalized regular n-sided polygon
};

/**
 * @brief Infill styling for closed vector shapes.
 */
enum class ShapeFillType : uint8_t {
    None = 0,
    Solid = 1,
    SemiTransparent = 2,
    LinearGradient = 3,
    RadialGradient = 4,
    HatchDiagonal = 5,       ///< 45-degree single diagonal drafting hatch (///)
    HatchCross = 6,          ///< 45-degree drafting cross-hatch (XXX)
    HatchHorizontal = 7,     ///< Horizontal parallel drafting lines (---)
    HatchVertical = 8,       ///< Vertical parallel drafting lines (|||)
    HatchDots = 9            ///< Drafting stipple texture (concrete, sand, soil)
};

/**
 * @brief Outline stroke styles for vector shapes.
 */
enum class ShapeOutlineType : uint8_t {
    None = 0,
    Solid = 1,
    Dashed = 2,
    Dotted = 3,
    DashDot = 4
};

/**
 * @brief Arrowhead termination styles for line and connector endpoints.
 */
enum class ArrowHeadType : uint8_t {
    None = 0,
    Triangle = 1,   ///< Classic solid equilateral/acute arrowhead
    Stealth = 2,    ///< Aerodynamic winged arrowhead with notched base
    Open = 3,       ///< Two-stroke open chevron wireframe
    Circle = 4      ///< Circular terminal dot
};

/**
 * @brief Scalable, vector-native geometric shape container.
 * 
 * In FolioNote, shapes are treated strictly as vector primitives rather than
 * rasterized bitmaps. When a shape is resized, its boundary segments and vector
 * paths are recalculated from scratch so that stroke thickness and corner radiuses
 * remain consistent and un-distorted.
 * 
 * Supports:
 *  - Primitives: Rectangles, Rounded Rectangles, Circles, Ellipses, Isosceles Triangles,
 *    Right Triangles, Regular Polygons / Hexagons, Stars, Clouds, Hearts.
 *  - Two-point connector primitives (Line, LineArrow) with custom draggable endpoint handles.
 *  - Directional Arrowheads (None, Triangle, Stealth, Open, Circle).
 *  - Non-colored (transparent) infill by default with active pen color inheritance.
 */
class ShapeObject : public CanvasObject {
public:
    ShapeType shapeType = ShapeType::Rectangle;
    ShapeFillType fillType = ShapeFillType::None;           // By default, shapes are non-colored (transparent fill)
    ShapeOutlineType outlineType = ShapeOutlineType::Solid;

    double worldX = 0.0;
    double worldY = 0.0;
    double worldWidth = 60.0;   // In canvas world millimeters (mm); for lines: deltaX
    double worldHeight = 40.0;  // In canvas world millimeters (mm); for lines: deltaY

    BLRgba32 strokeColor{0x18, 0x1A, 0x20, 0xFF};        // Outline color (synced with active pen)
    BLRgba32 fillColor{0x00, 0x78, 0xD4, 0x40};          // Infill color
    BLRgba32 secondaryFillColor{0x00, 0xC4, 0xFF, 0x20}; // Secondary color for gradients/patterns

    double strokeWidth = 1.0;    // Outline thickness in world millimeters (mm)
    double cornerRadius = 4.0;   // In world millimeters (for rounded rects)
    double param1 = 6.0;         // Generic parameter 1 (Polygon side count, default 6)
    double param2 = 0.45;        // Generic parameter 2 (Star inner/outer ratio, Arrow head ratio)

    // Arrowhead configurations for Line and LineArrow
    ArrowHeadType startArrow = ArrowHeadType::None;
    ArrowHeadType endArrow = ArrowHeadType::Triangle;
    double arrowHeadSize = 4.0;  // Arrowhead size in world millimeters (mm)

    ShapeObject() {
        type = ObjectType::Shape;
        UpdateBounds();
    }

    ShapeObject(ShapeType type, double x, double y, double w, double h)
        : shapeType(type), worldX(x), worldY(y), worldWidth(w), worldHeight(h) {
        this->type = ObjectType::Shape;
        if (type == ShapeType::LineArrow) {
            endArrow = ArrowHeadType::Triangle;
        } else if (type == ShapeType::Line) {
            endArrow = ArrowHeadType::None;
        }
        UpdateBounds();
    }

    // =========================================================================
    // GEOMETRIC PATH GENERATION
    // =========================================================================

    /**
     * @brief Builds the local 2D vector outline for this shape into a Blend2D BLPath.
     * 
     * Mathematical paths:
     *  - Rectangle: Rect[x, y, w, h]
     *  - RoundedRectangle: RoundRect[x, y, w, h, r, r]
     *  - Circle: Center=(x + w/2, y + h/2), Radius=min(w, h)/2
     *  - Ellipse: Center=(x + w/2, y + h/2), Rx=w/2, Ry=h/2
     *  - Triangle (Isosceles): Top apex (x + w/2, y) -> Bottom right (x + w, y + h) -> Bottom left (x, y + h) -> Close
     *  - RightTriangle: 90 deg corner at (x, y + h) -> Top apex (x, y) -> Bottom right (x + w, y + h) -> Close
     *  - RegularPolygon: N-sided polygon, vertex i at theta = -pi/2 + i*(2*pi/N)
     *  - Line / LineArrow: Line segment from (x, y) to (x + w, y + h)
     */
    void BuildPath(BLPath& path) const {
        path.clear();
        const double x = worldX;
        const double y = worldY;
        const double w = std::max(worldWidth, 0.01);
        const double h = std::max(worldHeight, 0.01);

        switch (shapeType) {
            case ShapeType::Rectangle: {
                path.add_rect(BLRect(x, y, w, h));
                break;
            }
            case ShapeType::RoundedRectangle: {
                double r = std::min({ cornerRadius, w * 0.5, h * 0.5 });
                path.add_round_rect(BLRoundRect(x, y, w, h, r, r));
                break;
            }
            case ShapeType::Circle: {
                double r = std::min(w, h) * 0.5;
                double cx = x + w * 0.5;
                double cy = y + h * 0.5;
                path.add_circle(BLCircle(cx, cy, r));
                break;
            }
            case ShapeType::Ellipse: {
                double cx = x + w * 0.5;
                double cy = y + h * 0.5;
                path.add_ellipse(BLEllipse(cx, cy, w * 0.5, h * 0.5));
                break;
            }
            case ShapeType::Triangle: {
                // Isosceles triangle pointing vertically upward
                path.move_to(x + w * 0.5, y);
                path.line_to(x + w, y + h);
                path.line_to(x, y + h);
                path.close();
                break;
            }
            case ShapeType::RightTriangle: {
                // 90-degree right triangle with right angle at bottom-left (x, y + h)
                // Legs: Vertical leg (x, y + h) -> (x, y), Horizontal leg (x, y + h) -> (x + w, y + h)
                // Hypotenuse: (x, y) -> (x + w, y + h)
                path.move_to(x, y);
                path.line_to(x + w, y + h);
                path.line_to(x, y + h);
                path.close();
                break;
            }
            case ShapeType::Diamond: {
                path.move_to(x + w * 0.5, y);
                path.line_to(x + w, y + h * 0.5);
                path.line_to(x + w * 0.5, y + h);
                path.line_to(x, y + h * 0.5);
                path.close();
                break;
            }
            case ShapeType::Star: {
                int points = std::clamp(static_cast<int>(param1), 3, 32);
                double innerRatio = std::clamp(param2, 0.1, 0.9);
                double cx = x + w * 0.5;
                double cy = y + h * 0.5;
                double rx = w * 0.5;
                double ry = h * 0.5;
                double step = 3.14159265358979323846 / points;
                double startAngle = -3.14159265358979323846 * 0.5;

                for (int i = 0; i < points * 2; ++i) {
                    double angle = startAngle + i * step;
                    double currRx = (i % 2 == 0) ? rx : (rx * innerRatio);
                    double currRy = (i % 2 == 0) ? ry : (ry * innerRatio);
                    double px = cx + std::cos(angle) * currRx;
                    double py = cy + std::sin(angle) * currRy;
                    if (i == 0) path.move_to(px, py);
                    else path.line_to(px, py);
                }
                path.close();
                break;
            }
            case ShapeType::Arrow: {
                // Legacy 2D block arrow preserved for backwards compatibility
                double headLen = std::clamp(param2 * w, 5.0, w * 0.7);
                double shaftH = h * 0.4;
                double shaftTop = y + (h - shaftH) * 0.5;
                double shaftBottom = shaftTop + shaftH;
                double shaftRight = x + w - headLen;

                path.move_to(x, shaftTop);
                path.line_to(shaftRight, shaftTop);
                path.line_to(shaftRight, y);
                path.line_to(x + w, y + h * 0.5);
                path.line_to(shaftRight, y + h);
                path.line_to(shaftRight, shaftBottom);
                path.line_to(x, shaftBottom);
                path.close();
                break;
            }
            case ShapeType::DoubleArrow: {
                double headLen = std::clamp(param2 * w * 0.5, 4.0, w * 0.35);
                double shaftH = h * 0.4;
                double shaftTop = y + (h - shaftH) * 0.5;
                double shaftBottom = shaftTop + shaftH;

                path.move_to(x + headLen, shaftTop);
                path.line_to(x + w - headLen, shaftTop);
                path.line_to(x + w - headLen, y);
                path.line_to(x + w, y + h * 0.5);
                path.line_to(x + w - headLen, y + h);
                path.line_to(x + w - headLen, shaftBottom);
                path.line_to(x + headLen, shaftBottom);
                path.line_to(x + headLen, y + h);
                path.line_to(x, y + h * 0.5);
                path.line_to(x + headLen, y);
                path.close();
                break;
            }
            case ShapeType::Line:
            case ShapeType::LineArrow: {
                path.move_to(worldX, worldY);
                path.line_to(worldX + worldWidth, worldY + worldHeight);
                break;
            }
            case ShapeType::Hexagon:
            case ShapeType::RegularPolygon: {
                int sides = std::clamp(static_cast<int>(std::round(param1)), 3, 32);
                double cx = x + w * 0.5;
                double cy = y + h * 0.5;
                double rx = w * 0.5;
                double ry = h * 0.5;
                double step = (2.0 * 3.14159265358979323846) / sides;
                double startAngle = -3.14159265358979323846 * 0.5; // Apex at top
                for (int i = 0; i < sides; ++i) {
                    double angle = startAngle + i * step;
                    double px = cx + std::cos(angle) * rx;
                    double py = cy + std::sin(angle) * ry;
                    if (i == 0) path.move_to(px, py);
                    else path.line_to(px, py);
                }
                path.close();
                break;
            }
            case ShapeType::Heart: {
                double cx = x + w * 0.5;
                double topY = y + h * 0.3;
                path.move_to(cx, y + h);
                path.cubic_to(x - w * 0.1, y + h * 0.5, x, y, cx, topY);
                path.cubic_to(x + w, y, x + w * 1.1, y + h * 0.5, cx, y + h);
                path.close();
                break;
            }
            case ShapeType::Cloud: {
                double r = std::min(w, h) * 0.25;
                path.add_round_rect(BLRoundRect(x, y + h * 0.3, w, h * 0.7, r, r));
                path.add_ellipse(BLEllipse(x + w * 0.3, y + h * 0.4, w * 0.25, h * 0.35));
                path.add_ellipse(BLEllipse(x + w * 0.65, y + h * 0.35, w * 0.28, h * 0.38));
                break;
            }
        }
    }

    // =========================================================================
    // BOUNDS & SPATIAL INTERSECTION
    // =========================================================================

    void UpdateBounds() override {
        double halfStroke = (outlineType != ShapeOutlineType::None) ? (strokeWidth * 0.5 + 1.0) : 1.0;

        if (shapeType == ShapeType::Line || shapeType == ShapeType::LineArrow) {
            double x1 = worldX;
            double y1 = worldY;
            double x2 = worldX + worldWidth;
            double y2 = worldY + worldHeight;

            double minX = std::min(x1, x2) - halfStroke;
            double minY = std::min(y1, y2) - halfStroke;
            double maxX = std::max(x1, x2) + halfStroke;
            double maxY = std::max(y1, y2) + halfStroke;

            BLPoint p[4] = {
                transform.map_point(minX, minY),
                transform.map_point(maxX, minY),
                transform.map_point(maxX, maxY),
                transform.map_point(minX, maxY)
            };
            double bMinX = p[0].x, bMaxX = p[0].x;
            double bMinY = p[0].y, bMaxY = p[0].y;
            for (int i = 1; i < 4; ++i) {
                bMinX = std::min(bMinX, p[i].x);
                bMaxX = std::max(bMaxX, p[i].x);
                bMinY = std::min(bMinY, p[i].y);
                bMaxY = std::max(bMaxY, p[i].y);
            }
            bounds = AABB(bMinX, bMinY, bMaxX, bMaxY);
            return;
        }

        double localMinX = worldX - halfStroke;
        double localMinY = worldY - halfStroke;
        double localMaxX = worldX + worldWidth + halfStroke;
        double localMaxY = worldY + worldHeight + halfStroke;

        BLPoint p[4] = {
            transform.map_point(localMinX, localMinY),
            transform.map_point(localMaxX, localMinY),
            transform.map_point(localMaxX, localMaxY),
            transform.map_point(localMinX, localMaxY)
        };

        double minX = p[0].x, maxX = p[0].x;
        double minY = p[0].y, maxY = p[0].y;
        for (int i = 1; i < 4; ++i) {
            minX = std::min(minX, p[i].x);
            maxX = std::max(maxX, p[i].x);
            minY = std::min(minY, p[i].y);
            maxY = std::max(maxY, p[i].y);
        }
        bounds = AABB(minX, minY, maxX, maxY);
    }

    /**
     * @brief Computes point-to-segment squared Euclidean distance.
     * 
     * Math:
     *   v = B - A, w = P - A
     *   t = clamp((w . v) / (v . v), 0, 1)
     *   dist = || P - (A + t * v) ||
     */
    static double DistSqPointToSegment(const Point2D& p, const Point2D& a, const Point2D& b) {
        double vx = b.x - a.x;
        double vy = b.y - a.y;
        double wx = p.x - a.x;
        double wy = p.y - a.y;
        double c1 = wx * vx + wy * vy;
        if (c1 <= 0.0) {
            return (p.x - a.x) * (p.x - a.x) + (p.y - a.y) * (p.y - a.y);
        }
        double c2 = vx * vx + vy * vy;
        if (c2 <= c1) {
            return (p.x - b.x) * (p.x - b.x) + (p.y - b.y) * (p.y - b.y);
        }
        double t = c1 / c2;
        double projX = a.x + t * vx;
        double projY = a.y + t * vy;
        return (p.x - projX) * (p.x - projX) + (p.y - projY) * (p.y - projY);
    }

    bool HitTest(double worldXQuery, double worldYQuery) const override {
        if (!bounds.Contains(worldXQuery, worldYQuery)) return false;

        BLMatrix2D invTransform;
        if (BLMatrix2D::invert(invTransform, transform) != BL_SUCCESS) {
            invTransform = BLMatrix2D::make_identity();
        }
        BLPoint localPt = invTransform.map_point(worldXQuery, worldYQuery);

        // Lines and arrows: distance to segment test
        if (shapeType == ShapeType::Line || shapeType == ShapeType::LineArrow) {
            Point2D q(localPt.x, localPt.y);
            Point2D p1(worldX, worldY);
            Point2D p2(worldX + worldWidth, worldY + worldHeight);
            double distSq = DistSqPointToSegment(q, p1, p2);
            double hitTol = std::max(strokeWidth * 0.5, 2.5);
            return distSq <= (hitTol * hitTol);
        }

        BLPath path;
        BuildPath(path);

        // Check if inside fill
        if (fillType != ShapeFillType::None) {
            BLHitTest hit = path.hit_test(localPt, BL_FILL_RULE_NON_ZERO);
            if (hit == BL_HIT_TEST_IN) return true;
        }

        // Check proximity to stroke outline
        double hitTolerance = std::max(strokeWidth * 0.5, 2.5);
        if (outlineType != ShapeOutlineType::None || fillType == ShapeFillType::None) {
            if (localPt.x >= worldX - hitTolerance && localPt.x <= worldX + worldWidth + hitTolerance &&
                localPt.y >= worldY - hitTolerance && localPt.y <= worldY + worldHeight + hitTolerance) {
                return true;
            }
        }
        return false;
    }

    bool HitTestCircle(double worldXQuery, double worldYQuery, double radiusMm) const override {
        AABB queryBox(worldXQuery - radiusMm, worldYQuery - radiusMm, worldXQuery + radiusMm, worldYQuery + radiusMm);
        if (!bounds.Intersects(queryBox)) return false;
        return HitTest(worldXQuery, worldYQuery) || bounds.Contains(worldXQuery, worldYQuery);
    }

    bool HitTestSwept(const Point2D& w0, const Point2D& w1, double radiusMm) const override {
        AABB sweptBox(
            std::min(w0.x, w1.x) - radiusMm,
            std::min(w0.y, w1.y) - radiusMm,
            std::max(w0.x, w1.x) + radiusMm,
            std::max(w0.y, w1.y) + radiusMm
        );
        if (!bounds.Intersects(sweptBox)) return false;
        return HitTestCircle(w0.x, w0.y, radiusMm) ||
               HitTestCircle(w1.x, w1.y, radiusMm) ||
               HitTestCircle((w0.x + w1.x) * 0.5, (w0.y + w1.y) * 0.5, radiusMm);
    }

    bool Intersects(const AABB& selectionBounds) const override {
        return bounds.Intersects(selectionBounds);
    }

    /**
     * @brief Transforms vector shape coordinates while keeping stroke width invariant.
     * 
     * Shapes are geometric vector definitions rather than raster images. When resized
     * via SelectionGizmo, we update the boundary segments directly (recalculating worldX,
     * worldY, worldWidth, and worldHeight) and reset transform to identity. This guarantees
     * that strokeWidth does not bloat or scale non-uniformly.
     */
    void ApplyTransform(const BLMatrix2D& matrix) override {
        // If the transformation is pure scaling and translation (no rotation or shear):
        if (std::abs(matrix.m01) < 1e-6 && std::abs(matrix.m10) < 1e-6) {
            double p0x = matrix.m00 * worldX + matrix.m20;
            double p0y = matrix.m11 * worldY + matrix.m21;
            double p1x = matrix.m00 * (worldX + worldWidth) + matrix.m20;
            double p1y = matrix.m11 * (worldY + worldHeight) + matrix.m21;

            if (shapeType == ShapeType::Line || shapeType == ShapeType::LineArrow) {
                worldX = p0x;
                worldY = p0y;
                worldWidth = p1x - p0x;
                worldHeight = p1y - p0y;
            } else {
                worldX = std::min(p0x, p1x);
                worldY = std::min(p0y, p1y);
                worldWidth = std::max(0.5, std::abs(p1x - p0x));
                worldHeight = std::max(0.5, std::abs(p1y - p0y));
            }
            transform = BLMatrix2D::make_identity();
        } else {
            // General affine transformation (e.g. rotation)
            transform.post_transform(matrix);
        }
        UpdateBounds();
    }

    // =========================================================================
    // CUSTOM GIZMO HANDLES (Line & LineArrow Draggable Endpoints)
    // =========================================================================

    /**
     * @brief Queries custom handles for lines and arrows.
     * 
     * Instead of a bounding box with 8 grips and a rotation knob, Line and LineArrow
     * provide exactly 2 draggable endpoint grips:
     *  - Handle 0: Start point (worldX, worldY)
     *  - Handle 1: End point (worldX + worldWidth, worldY + worldHeight)
     */
    bool GetCustomGizmoHandles(std::vector<GizmoHandle>& outHandles, const CanvasTransform& /*transform*/) const override {
        if (shapeType != ShapeType::Line && shapeType != ShapeType::LineArrow) {
            return false;
        }
        outHandles.clear();

        // Handle 0: Start Endpoint
        GizmoHandle h0;
        h0.customId = 0;
        h0.worldPos = Point2D(worldX, worldY);
        h0.role = HandleRole::Custom;
        outHandles.push_back(h0);

        // Handle 1: End Endpoint
        GizmoHandle h1;
        h1.customId = 1;
        h1.worldPos = Point2D(worldX + worldWidth, worldY + worldHeight);
        h1.role = HandleRole::Custom;
        outHandles.push_back(h1);

        return true;
    }

    /**
     * @brief Drag handler for line and arrow endpoint handles.
     * Allows adjusting length and orientation (360 degrees) while keeping stroke thickness invariant.
     */
    bool OnGizmoHandleDrag(int customId, const Point2D& /*worldPos*/, const Point2D& worldDelta) override {
        if (shapeType != ShapeType::Line && shapeType != ShapeType::LineArrow) {
            return false;
        }
        if (customId == 0) {
            // Moving start point: moves start and counter-adjusts delta dimensions to keep end point fixed
            worldX += worldDelta.x;
            worldY += worldDelta.y;
            worldWidth -= worldDelta.x;
            worldHeight -= worldDelta.y;
            UpdateBounds();
            return true;
        } else if (customId == 1) {
            // Moving end point: adjusts delta dimensions while keeping start point fixed
            worldWidth += worldDelta.x;
            worldHeight += worldDelta.y;
            UpdateBounds();
            return true;
        }
        return false;
    }

    // =========================================================================
    // RENDERING
    // =========================================================================

    /**
     * @brief Procedurally draws an arrowhead at an endpoint.
     * 
     * @param ctx Blend2D context
     * @param tipX World X coordinate of the arrow tip
     * @param tipY World Y coordinate of the arrow tip
     * @param angleRad Directional orientation of the arrowhead (pointing toward tip)
     * @param headType ArrowHeadType (Triangle, Stealth, Open, Circle)
     * @param sizeMm Arrowhead length/size in millimeters
     * @param color Stroke/fill color
     */
    static void DrawArrowHead(BLContext& ctx, double tipX, double tipY, double angleRad,
                              ArrowHeadType headType, double sizeMm, const BLRgba32& color) {
        if (headType == ArrowHeadType::None || sizeMm <= 0.1) return;

        double cosA = std::cos(angleRad);
        double sinA = std::sin(angleRad);
        double perpX = -sinA;
        double perpY = cosA;

        ctx.save();
        ctx.set_stroke_style(color);
        ctx.set_fill_style(color);

        if (headType == ArrowHeadType::Triangle) {
            double bx = tipX - sizeMm * cosA;
            double by = tipY - sizeMm * sinA;
            double halfW = sizeMm * 0.55;

            BLPath arr;
            arr.move_to(tipX, tipY);
            arr.line_to(bx + halfW * perpX, by + halfW * perpY);
            arr.line_to(bx - halfW * perpX, by - halfW * perpY);
            arr.close();
            ctx.fill_path(arr);
        }
        else if (headType == ArrowHeadType::Stealth) {
            double bx = tipX - sizeMm * cosA;
            double by = tipY - sizeMm * sinA;
            double ix = tipX - sizeMm * 0.75 * cosA;
            double iy = tipY - sizeMm * 0.75 * sinA;
            double halfW = sizeMm * 0.65;

            BLPath arr;
            arr.move_to(tipX, tipY);
            arr.line_to(bx + halfW * perpX, by + halfW * perpY);
            arr.line_to(ix, iy);
            arr.line_to(bx - halfW * perpX, by - halfW * perpY);
            arr.close();
            ctx.fill_path(arr);
        }
        else if (headType == ArrowHeadType::Open) {
            double bx = tipX - sizeMm * cosA;
            double by = tipY - sizeMm * sinA;
            double halfW = sizeMm * 0.6;

            BLPath arr;
            arr.move_to(bx + halfW * perpX, by + halfW * perpY);
            arr.line_to(tipX, tipY);
            arr.line_to(bx - halfW * perpX, by - halfW * perpY);
            ctx.set_stroke_width(std::max(1.0, sizeMm * 0.25));
            ctx.set_stroke_caps(BL_STROKE_CAP_ROUND);
            ctx.stroke_path(arr);
        }
        else if (headType == ArrowHeadType::Circle) {
            double r = sizeMm * 0.4;
            ctx.fill_circle(tipX, tipY, r);
        }
        ctx.restore();
    }

    /**
     * @brief Procedurally creates seamless repeating drafting hatch patterns for closed vector shapes.
     * 
     * In technical drafting and architectural sketches, crosshatching and textured lines
     * represent materials (e.g. section cuts, masonry, ground hatching).
     * 
     * Seamless Tiling Mathematics:
     * - HatchDiagonal (45 deg): Parallel lines y = x + c wrapped at boundaries:
     *     Line 1: (0, 0) -> (sz, sz)
     *     Line 2: (0, sz*0.5) -> (sz*0.5, sz)
     *     Line 3: (sz*0.5, 0) -> (sz, sz*0.5)
     * - HatchCross (45 deg): Adds the perpendicular lines y = -x + c wrapped at boundaries.
     * - HatchHorizontal: Line across y = sz * 0.5.
     * - HatchVertical: Line down x = sz * 0.5.
     * - HatchDots (Stipple): Evenly staggered circular dots representing concrete or stippled shading.
     * 
     * @param type ShapeFillType hatch variant
     * @param color Line/dot stroke color
     * @param spacingMm World spacing between lines in millimeters (default 3.0mm)
     * @return BLPattern with repeat extend mode and scaled transform
     */
    static BLPattern CreateHatchPattern(ShapeFillType type, const BLRgba32& color, double spacingMm = 3.0) {
        int sz = 32;
        BLImage img(sz, sz, BL_FORMAT_PRGB32);
        BLContext ictx(img);
        ictx.clear_all();

        BLRgba32 strokeCol = color;
        if (strokeCol.a() == 0) {
            strokeCol = BLRgba32(0x18, 0x1A, 0x20, 0xFF);
        }

        ictx.set_stroke_style(strokeCol);
        ictx.set_stroke_width(2.0);
        ictx.set_stroke_caps(BL_STROKE_CAP_SQUARE);

        if (type == ShapeFillType::HatchDiagonal) {
            ictx.stroke_line(0, 0, sz, sz);
            ictx.stroke_line(0, sz * 0.5, sz * 0.5, sz);
            ictx.stroke_line(sz * 0.5, 0, sz, sz * 0.5);
        } else if (type == ShapeFillType::HatchCross) {
            ictx.stroke_line(0, 0, sz, sz);
            ictx.stroke_line(0, sz * 0.5, sz * 0.5, sz);
            ictx.stroke_line(sz * 0.5, 0, sz, sz * 0.5);

            ictx.stroke_line(0, sz, sz, 0);
            ictx.stroke_line(0, sz * 0.5, sz * 0.5, 0);
            ictx.stroke_line(sz * 0.5, sz, sz, sz * 0.5);
        } else if (type == ShapeFillType::HatchHorizontal) {
            ictx.stroke_line(0, sz * 0.5, sz, sz * 0.5);
        } else if (type == ShapeFillType::HatchVertical) {
            ictx.stroke_line(sz * 0.5, 0, sz * 0.5, sz);
        } else if (type == ShapeFillType::HatchDots) {
            ictx.set_fill_style(strokeCol);
            ictx.fill_circle(sz * 0.25, sz * 0.25, 2.2);
            ictx.fill_circle(sz * 0.75, sz * 0.75, 2.2);
        }
        ictx.end();

        double scale = spacingMm / static_cast<double>(sz);
        return BLPattern(img, BL_EXTEND_MODE_REPEAT, BLMatrix2D::make_scaling(scale, scale));
    }

    void Render(BLContext& ctx, const Viewport& viewport) const override {
        (void)viewport;
        if (!isVisible) return;

        BLPath path;
        BuildPath(path);

        ctx.save();
        ctx.apply_transform(transform);

        // 1. Infill Pass (Only for closed 2D shapes, not lines or line-arrows)
        bool isLinear = (shapeType == ShapeType::Line || shapeType == ShapeType::LineArrow);
        if (fillType != ShapeFillType::None && !isLinear) {
            ctx.save();
            if (fillType == ShapeFillType::Solid) {
                BLRgba32 col = fillColor;
                col.setA(255);
                ctx.set_fill_style(col);
                ctx.fill_path(path);
            } else if (fillType == ShapeFillType::SemiTransparent) {
                BLRgba32 col = fillColor;
                if (col.a() > 180 || col.a() == 0) col.setA(90);
                ctx.set_fill_style(col);
                ctx.fill_path(path);
            } else if (fillType == ShapeFillType::LinearGradient) {
                BLLinearGradientValues values{ worldX, worldY, worldX + worldWidth, worldY + worldHeight };
                BLGradient grad(values, BL_EXTEND_MODE_PAD);
                grad.add_stop(0.0, fillColor);
                grad.add_stop(1.0, secondaryFillColor);
                ctx.set_fill_style(grad);
                ctx.fill_path(path);
            } else if (fillType == ShapeFillType::RadialGradient) {
                BLRadialGradientValues values{ worldX + worldWidth * 0.5, worldY + worldHeight * 0.5, 0.0, 0.0, std::max(worldWidth, worldHeight) * 0.5 };
                BLGradient grad(values, BL_EXTEND_MODE_PAD);
                grad.add_stop(0.0, fillColor);
                grad.add_stop(1.0, secondaryFillColor);
                ctx.set_fill_style(grad);
                ctx.fill_path(path);
            } else if (fillType == ShapeFillType::HatchDiagonal ||
                       fillType == ShapeFillType::HatchCross ||
                       fillType == ShapeFillType::HatchHorizontal ||
                       fillType == ShapeFillType::HatchVertical ||
                       fillType == ShapeFillType::HatchDots) {
                BLPattern hatch = CreateHatchPattern(fillType, fillColor);
                ctx.set_fill_style(hatch);
                ctx.fill_path(path);
            }
            ctx.restore();
        }

        // 2. Outline Pass
        if (outlineType != ShapeOutlineType::None && strokeWidth > 0.0) {
            ctx.save();
            ctx.set_stroke_style(strokeColor);
            ctx.set_stroke_width(strokeWidth);
            ctx.set_stroke_caps(BL_STROKE_CAP_ROUND);
            ctx.set_stroke_join(BL_STROKE_JOIN_ROUND);

            if (outlineType == ShapeOutlineType::Dashed) {
                BLArray<double> dashes;
                dashes.append(strokeWidth * 4.0);
                dashes.append(strokeWidth * 2.0);
                ctx.set_stroke_dash_array(dashes);
            } else if (outlineType == ShapeOutlineType::Dotted) {
                BLArray<double> dashes;
                dashes.append(strokeWidth * 1.2);
                dashes.append(strokeWidth * 2.0);
                ctx.set_stroke_dash_array(dashes);
            } else if (outlineType == ShapeOutlineType::DashDot) {
                BLArray<double> dashes;
                dashes.append(strokeWidth * 4.0);
                dashes.append(strokeWidth * 2.0);
                dashes.append(strokeWidth * 1.2);
                dashes.append(strokeWidth * 2.0);
                ctx.set_stroke_dash_array(dashes);
            }

            ctx.stroke_path(path);
            ctx.restore();
        }

        // 3. Arrowheads Pass for Line and LineArrow
        if (shapeType == ShapeType::LineArrow || (shapeType == ShapeType::Line && (startArrow != ArrowHeadType::None || endArrow != ArrowHeadType::None))) {
            double p1x = worldX;
            double p1y = worldY;
            double p2x = worldX + worldWidth;
            double p2y = worldY + worldHeight;
            double angle = std::atan2(p2y - p1y, p2x - p1x);
            double headSz = std::max(2.5, arrowHeadSize * (strokeWidth * 0.75));

            if (endArrow != ArrowHeadType::None) {
                DrawArrowHead(ctx, p2x, p2y, angle, endArrow, headSz, strokeColor);
            }
            if (startArrow != ArrowHeadType::None) {
                DrawArrowHead(ctx, p1x, p1y, angle + 3.14159265358979323846, startArrow, headSz, strokeColor);
            }
        }

        ctx.restore();
    }

    std::unique_ptr<CanvasObject> Clone() const override {
        return std::make_unique<ShapeObject>(*this);
    }

    void Serialize(Serializer& /*writer*/) const override {}
    void Deserialize(Deserializer& /*reader*/) override {}

    // =========================================================================
    // IMGUI PROCEDURAL VECTOR ICONS FOR RIBBON BUTTONS
    // =========================================================================

    /**
     * @brief Procedurally draws high-DPI vector logos for shape buttons in ImGui.
     */
    static void DrawShapeIconImGui(ImDrawList* drawList, ShapeType type, ImVec2 pMin, ImVec2 pMax, ImU32 strokeCol, ImU32 fillCol) {
        if (!drawList) return;
        const float pad = 3.5f;
        const float x0 = pMin.x + pad;
        const float y0 = pMin.y + pad;
        const float x1 = pMax.x - pad;
        const float y1 = pMax.y - pad;
        const float w = x1 - x0;
        const float h = y1 - y0;
        const float thickness = 1.6f;

        switch (type) {
            case ShapeType::Rectangle: {
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), fillCol, 1.0f);
                drawList->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), strokeCol, 1.0f, 0, thickness);
                break;
            }
            case ShapeType::RoundedRectangle: {
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), fillCol, 4.0f);
                drawList->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), strokeCol, 4.0f, 0, thickness);
                break;
            }
            case ShapeType::Circle: {
                ImVec2 center(x0 + w * 0.5f, y0 + h * 0.5f);
                float r = std::min(w, h) * 0.46f;
                drawList->AddCircleFilled(center, r, fillCol);
                drawList->AddCircle(center, r, strokeCol, 24, thickness);
                break;
            }
            case ShapeType::Ellipse: {
                ImVec2 center(x0 + w * 0.5f, y0 + h * 0.5f);
                drawList->AddEllipseFilled(center, ImVec2(w * 0.48f, h * 0.35f), fillCol);
                drawList->AddEllipse(center, ImVec2(w * 0.48f, h * 0.35f), strokeCol, 0.0f, 24, thickness);
                break;
            }
            case ShapeType::Triangle: {
                // Isosceles triangle
                ImVec2 pA(x0 + w * 0.5f, y0);
                ImVec2 pB(x1, y1);
                ImVec2 pC(x0, y1);
                drawList->AddTriangleFilled(pA, pB, pC, fillCol);
                drawList->AddTriangle(pA, pB, pC, strokeCol, thickness);
                break;
            }
            case ShapeType::RightTriangle: {
                // Right-angled triangle (90 degrees at bottom-left corner)
                ImVec2 pTop(x0, y0);
                ImVec2 pRight(x1, y1);
                ImVec2 pCorner(x0, y1);
                drawList->AddTriangleFilled(pTop, pRight, pCorner, fillCol);
                drawList->AddTriangle(pTop, pRight, pCorner, strokeCol, thickness);
                // Right angle small marker
                float sq = std::min(w, h) * 0.22f;
                drawList->AddLine(ImVec2(x0, y1 - sq), ImVec2(x0 + sq, y1 - sq), strokeCol, 1.0f);
                drawList->AddLine(ImVec2(x0 + sq, y1 - sq), ImVec2(x0 + sq, y1), strokeCol, 1.0f);
                break;
            }
            case ShapeType::Diamond: {
                ImVec2 pts[4] = {
                    ImVec2(x0 + w * 0.5f, y0),
                    ImVec2(x1, y0 + h * 0.5f),
                    ImVec2(x0 + w * 0.5f, y1),
                    ImVec2(x0, y0 + h * 0.5f)
                };
                drawList->AddConvexPolyFilled(pts, 4, fillCol);
                drawList->AddPolyline(pts, 4, strokeCol, ImDrawFlags_Closed, thickness);
                break;
            }
            case ShapeType::Star: {
                ImVec2 pts[10];
                float cx = x0 + w * 0.5f;
                float cy = y0 + h * 0.5f;
                float rOuter = std::min(w, h) * 0.48f;
                float rInner = rOuter * 0.45f;
                float step = 3.14159265f / 5.0f;
                float startAngle = -3.14159265f * 0.5f;
                for (int i = 0; i < 10; ++i) {
                    float r = (i % 2 == 0) ? rOuter : rInner;
                    float angle = startAngle + i * step;
                    pts[i] = ImVec2(cx + std::cos(angle) * r, cy + std::sin(angle) * r);
                }
                drawList->AddConvexPolyFilled(pts, 10, fillCol);
                drawList->AddPolyline(pts, 10, strokeCol, ImDrawFlags_Closed, thickness);
                break;
            }
            case ShapeType::Line: {
                drawList->AddLine(ImVec2(x0, y1), ImVec2(x1, y0), strokeCol, thickness * 1.5f);
                // End dots
                drawList->AddCircleFilled(ImVec2(x0, y1), 2.2f, strokeCol);
                drawList->AddCircleFilled(ImVec2(x1, y0), 2.2f, strokeCol);
                break;
            }
            case ShapeType::LineArrow: {
                ImVec2 startPt(x0, y1);
                ImVec2 endPt(x1, y0);
                drawList->AddLine(startPt, endPt, strokeCol, thickness * 1.5f);
                // Arrowhead triangle at endPt
                float angle = std::atan2(y0 - y1, x1 - x0);
                float aLen = std::min(w, h) * 0.45f;
                float aHalf = aLen * 0.45f;
                float cosA = std::cos(angle);
                float sinA = std::sin(angle);
                ImVec2 bPt(endPt.x - aLen * cosA, endPt.y - aLen * sinA);
                ImVec2 wing1(bPt.x - aHalf * sinA, bPt.y + aHalf * cosA);
                ImVec2 wing2(bPt.x + aHalf * sinA, bPt.y - aHalf * cosA);
                drawList->AddTriangleFilled(endPt, wing1, wing2, strokeCol);
                // Start endpoint dot
                drawList->AddCircleFilled(startPt, 2.0f, strokeCol);
                break;
            }
            case ShapeType::Hexagon:
            case ShapeType::RegularPolygon: {
                ImVec2 pts[6];
                float cx = x0 + w * 0.5f;
                float cy = y0 + h * 0.5f;
                float rx = w * 0.48f;
                float ry = h * 0.48f;
                for (int i = 0; i < 6; ++i) {
                    float angle = -3.14159265f * 0.5f + (3.14159265f / 3.0f) * i;
                    pts[i] = ImVec2(cx + std::cos(angle) * rx, cy + std::sin(angle) * ry);
                }
                drawList->AddConvexPolyFilled(pts, 6, fillCol);
                drawList->AddPolyline(pts, 6, strokeCol, ImDrawFlags_Closed, thickness);
                break;
            }
            case ShapeType::Heart: {
                float cx = x0 + w * 0.5f;
                drawList->AddCircleFilled(ImVec2(cx - w * 0.22f, y0 + h * 0.32f), w * 0.24f, fillCol);
                drawList->AddCircle(ImVec2(cx - w * 0.22f, y0 + h * 0.32f), w * 0.24f, strokeCol, 16, thickness);
                drawList->AddCircleFilled(ImVec2(cx + w * 0.22f, y0 + h * 0.32f), w * 0.24f, fillCol);
                drawList->AddCircle(ImVec2(cx + w * 0.22f, y0 + h * 0.32f), w * 0.24f, strokeCol, 16, thickness);
                drawList->AddTriangleFilled(ImVec2(x0 + 1.0f, y0 + h * 0.42f), ImVec2(x1 - 1.0f, y0 + h * 0.42f), ImVec2(cx, y1), fillCol);
                drawList->AddLine(ImVec2(x0 + 1.0f, y0 + h * 0.42f), ImVec2(cx, y1), strokeCol, thickness);
                drawList->AddLine(ImVec2(x1 - 1.0f, y0 + h * 0.42f), ImVec2(cx, y1), strokeCol, thickness);
                break;
            }
            case ShapeType::Cloud: {
                drawList->AddCircleFilled(ImVec2(x0 + w * 0.35f, y0 + h * 0.45f), w * 0.26f, fillCol);
                drawList->AddCircle(ImVec2(x0 + w * 0.35f, y0 + h * 0.45f), w * 0.26f, strokeCol, 16, thickness);
                drawList->AddCircleFilled(ImVec2(x0 + w * 0.65f, y0 + h * 0.40f), w * 0.30f, fillCol);
                drawList->AddCircle(ImVec2(x0 + w * 0.65f, y0 + h * 0.40f), w * 0.30f, strokeCol, 16, thickness);
                drawList->AddRectFilled(ImVec2(x0 + 2.0f, y0 + h * 0.50f), ImVec2(x1 - 2.0f, y1), fillCol, 4.0f);
                drawList->AddRect(ImVec2(x0 + 2.0f, y0 + h * 0.50f), ImVec2(x1 - 2.0f, y1), strokeCol, 4.0f, 0, thickness);
                break;
            }
            default: {
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), fillCol, 2.0f);
                drawList->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), strokeCol, 2.0f, 0, thickness);
                break;
            }
        }
    }
};

using ShapeContainer = ShapeObject;

} // namespace Folio
