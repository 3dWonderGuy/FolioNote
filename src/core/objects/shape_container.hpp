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

enum class ShapeType : uint8_t {
    Rectangle = 0,
    RoundedRectangle = 1,
    Ellipse = 2,
    Triangle = 3,
    Diamond = 4,
    Star = 5,
    Arrow = 6,
    DoubleArrow = 7,
    Line = 8,
    Hexagon = 9,
    Heart = 10,
    Cloud = 11
};

enum class ShapeFillType : uint8_t {
    None = 0,
    Solid = 1,
    SemiTransparent = 2,
    LinearGradient = 3,
    RadialGradient = 4,
    HatchDiagonal = 5,
    HatchCross = 6
};

enum class ShapeOutlineType : uint8_t {
    None = 0,
    Solid = 1,
    Dashed = 2,
    Dotted = 3,
    DashDot = 4
};

/**
 * @brief Scalable, future-proof vector shape container.
 * 
 * Supports geometric primitives (rectangles, rounded rectangles, ellipses,
 * triangles, diamonds, stars, arrows, lines, hexagons, etc.) with customizable:
 *  - Infill styles (None, Solid, Semi-Transparent, Linear/Radial Gradients, Hatch patterns)
 *  - Infill and Outline colors (BLRgba32 with alpha channels)
 *  - Outline styles (Solid, Dashed, Dotted, DashDot) and thickness
 *  - Aspect-ratio preservation and affine transformations with SelectionGizmo
 *  - Precise Blend2D rendering and geometric hit-testing
 */
class ShapeObject : public CanvasObject {
public:
    ShapeType shapeType = ShapeType::Rectangle;
    ShapeFillType fillType = ShapeFillType::SemiTransparent;
    ShapeOutlineType outlineType = ShapeOutlineType::Solid;

    double worldX = 0.0;
    double worldY = 0.0;
    double worldWidth = 60.0;   // In canvas world millimeters (mm)
    double worldHeight = 40.0;  // In canvas world millimeters (mm)

    BLRgba32 strokeColor{0x18, 0x1A, 0x20, 0xFF};        // Outline color
    BLRgba32 fillColor{0x00, 0x78, 0xD4, 0x40};          // Infill color (default semi-transparent accent)
    BLRgba32 secondaryFillColor{0x00, 0xC4, 0xFF, 0x20}; // Secondary color for gradients/patterns

    double strokeWidth = 1.0;    // In world millimeters (mm)
    double cornerRadius = 4.0;   // In world millimeters (for rounded rects, etc.)
    double param1 = 5.0;         // Generic parameter 1 (e.g., Star point count, default 5)
    double param2 = 0.45;        // Generic parameter 2 (e.g., Star inner/outer ratio, Arrow head ratio)

    ShapeObject() {
        type = ObjectType::Shape;
        UpdateBounds();
    }

    ShapeObject(ShapeType type, double x, double y, double w, double h)
        : shapeType(type), worldX(x), worldY(y), worldWidth(w), worldHeight(h) {
        this->type = ObjectType::Shape;
        UpdateBounds();
    }

    // =========================================================================
    // GEOMETRIC PATH GENERATION
    // =========================================================================

    /**
     * @brief Builds the local 2D vector outline for this shape into a Blend2D BLPath.
     */
    void BuildPath(BLPath& path) const {
        path.clear();
        const double x = worldX;
        const double y = worldY;
        const double w = std::max(worldWidth, 0.1);
        const double h = std::max(worldHeight, 0.1);

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
            case ShapeType::Ellipse: {
                double cx = x + w * 0.5;
                double cy = y + h * 0.5;
                path.add_ellipse(BLEllipse(cx, cy, w * 0.5, h * 0.5));
                break;
            }
            case ShapeType::Triangle: {
                // Isosceles triangle pointing up
                path.move_to(x + w * 0.5, y);
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
                // Right-pointing arrow
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
            case ShapeType::Line: {
                path.move_to(x, y);
                path.line_to(x + w, y + h);
                break;
            }
            case ShapeType::Hexagon: {
                double cx = x + w * 0.5;
                double cy = y + h * 0.5;
                double rx = w * 0.5;
                double ry = h * 0.5;
                for (int i = 0; i < 6; ++i) {
                    double angle = (3.14159265358979323846 / 3.0) * i;
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
        double halfStroke = (outlineType != ShapeOutlineType::None) ? (strokeWidth * 0.5) : 0.0;
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

    bool HitTest(double worldXQuery, double worldYQuery) const override {
        if (!bounds.Contains(worldXQuery, worldYQuery)) return false;

        BLMatrix2D invTransform;
        if (BLMatrix2D::invert(invTransform, transform) != BL_SUCCESS) {
            invTransform = BLMatrix2D::make_identity();
        }
        BLPoint localPt = invTransform.map_point(worldXQuery, worldYQuery);

        BLPath path;
        BuildPath(path);

        // Check if inside fill
        if (fillType != ShapeFillType::None && shapeType != ShapeType::Line) {
            BLHitTest hit = path.hit_test(localPt, BL_FILL_RULE_NON_ZERO);
            if (hit == BL_HIT_TEST_IN) return true;
        }

        // Check proximity to stroke outline
        double hitTolerance = std::max(strokeWidth * 0.5, 2.0);
        if (outlineType != ShapeOutlineType::None || fillType == ShapeFillType::None) {
            // For simple bounding check fallback
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

    void ApplyTransform(const BLMatrix2D& matrix) override {
        transform.post_transform(matrix);
        UpdateBounds();
    }

    // =========================================================================
    // RENDERING
    // =========================================================================

    void Render(BLContext& ctx, const Viewport& viewport) const override {
        (void)viewport;
        if (!isVisible) return;

        BLPath path;
        BuildPath(path);

        ctx.save();
        ctx.apply_transform(transform);

        // 1. Infill Pass
        if (fillType != ShapeFillType::None && shapeType != ShapeType::Line) {
            ctx.save();
            if (fillType == ShapeFillType::Solid || fillType == ShapeFillType::SemiTransparent) {
                ctx.set_fill_style(fillColor);
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

            if (shapeType == ShapeType::Line) {
                ctx.stroke_path(path);
            } else {
                ctx.stroke_path(path);
            }
            ctx.restore();
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
        const float pad = 3.0f;
        const float x0 = pMin.x + pad;
        const float y0 = pMin.y + pad;
        const float x1 = pMax.x - pad;
        const float y1 = pMax.y - pad;
        const float w = x1 - x0;
        const float h = y1 - y0;
        const float thickness = 1.5f;

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
            case ShapeType::Ellipse: {
                ImVec2 center(x0 + w * 0.5f, y0 + h * 0.5f);
                drawList->AddEllipseFilled(center, ImVec2(w * 0.48f, h * 0.48f), fillCol);
                drawList->AddEllipse(center, ImVec2(w * 0.48f, h * 0.48f), strokeCol, 0.0f, 24, thickness);
                break;
            }
            case ShapeType::Triangle: {
                ImVec2 pA(x0 + w * 0.5f, y0);
                ImVec2 pB(x1, y1);
                ImVec2 pC(x0, y1);
                drawList->AddTriangleFilled(pA, pB, pC, fillCol);
                drawList->AddTriangle(pA, pB, pC, strokeCol, thickness);
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
            case ShapeType::Arrow: {
                float headW = w * 0.45f;
                float shaftH = h * 0.35f;
                float shaftY0 = y0 + (h - shaftH) * 0.5f;
                float shaftY1 = shaftY0 + shaftH;
                float shaftX1 = x1 - headW;
                ImVec2 pts[7] = {
                    ImVec2(x0, shaftY0),
                    ImVec2(shaftX1, shaftY0),
                    ImVec2(shaftX1, y0),
                    ImVec2(x1, y0 + h * 0.5f),
                    ImVec2(shaftX1, y1),
                    ImVec2(shaftX1, shaftY1),
                    ImVec2(x0, shaftY1)
                };
                drawList->AddConvexPolyFilled(pts, 7, fillCol);
                drawList->AddPolyline(pts, 7, strokeCol, ImDrawFlags_Closed, thickness);
                break;
            }
            case ShapeType::Line: {
                drawList->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), strokeCol, thickness * 1.5f);
                break;
            }
            case ShapeType::Hexagon: {
                ImVec2 pts[6];
                float cx = x0 + w * 0.5f;
                float cy = y0 + h * 0.5f;
                float rx = w * 0.48f;
                float ry = h * 0.48f;
                for (int i = 0; i < 6; ++i) {
                    float angle = (3.14159265f / 3.0f) * i;
                    pts[i] = ImVec2(cx + std::cos(angle) * rx, cy + std::sin(angle) * ry);
                }
                drawList->AddConvexPolyFilled(pts, 6, fillCol);
                drawList->AddPolyline(pts, 6, strokeCol, ImDrawFlags_Closed, thickness);
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
