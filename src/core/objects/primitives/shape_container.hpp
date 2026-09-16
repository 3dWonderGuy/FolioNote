#pragma once
/**
 * @file shape_container.hpp
 * @brief Scalable vector-native closed 2D geometric shape container.
 *
 * Design Principles:
 *  - ShapeObject owns all rendering, hit-testing, bounds, gizmo, and serialization
 *    behavior for closed 2D shapes. The engine dispatches polymorphically via the
 *    CanvasObject interface — adding a new shape type requires only a new BuildPath
 *    case in shape_container.cpp and a new ShapeType enum value.
 *  - Line/Arrow types have been promoted to SmartArrowObject
 *    (connectors/smart_arrow_container.hpp) which provides 2-point endpoint handles.
 *  - Heavy implementations (BuildPath, Render, CreateHatchPattern, DrawArrowHead)
 *    live in shape_container.cpp to keep compile times short. DrawShapeIconImGui
 *    stays header-only because it has an ImGui draw-list dependency that the .cpp
 *    must not carry (avoids linking ImGui into non-UI translation units).
 *
 * Transform Contract (resize without exponential blowup):
 *  - During gizmo drag: ApplyTransform() accumulates into the BLMatrix2D transform
 *    field WITHOUT touching worldX/Y/W/H. No compounding across move events.
 *  - On drag end: BakeTransform() commits the matrix into worldX/Y/W/H and resets
 *    transform to identity. Called by SelectionGizmo::OnPointerUp().
 *  - Render() compensates stroke width by 1/sqrt(det(transform)) so the visual
 *    stroke pixel width stays invariant during live drag preview.
 */

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <algorithm>
#include <memory>
#include <vector>
#include <string>

#include <blend2d/blend2d.h>
#include <imgui.h>

#include "core/objects/canvas_object.hpp"
#include "core/objects/primitives/shape_types.hpp"
#include "core/objects/connectors/connector_types.hpp"
#include "core/spatial/aabb.hpp"

namespace Folio {

/**
 * @brief Scalable, vector-native geometric shape container.
 *
 * Manages one closed 2D shape primitive (rectangle, circle, triangle, star, etc.).
 * Rendering is fully procedural — shapes are never rasterized, so they scale and
 * resize without quality loss. Stroke width and corner radii remain physically
 * consistent in world-space millimeters regardless of canvas zoom.
 *
 * Infill and outline are composited in two separate Blend2D passes so that
 * transparent shapes with visible outlines work correctly at all alpha values.
 *
 * Drafting hatch patterns (diagonal, cross-hatch, horizontal lines, etc.) are
 * generated as seamlessly tiling BLPatterns via CreateHatchPattern().
 */
class ShapeObject : public CanvasObject {
public:
    // =========================================================================
    // FIELDS
    // =========================================================================

    ShapeType       shapeType   = ShapeType::Rectangle;
    ShapeFillType   fillType    = ShapeFillType::None;          ///< Default: no fill (transparent)
    ShapeOutlineType outlineType = ShapeOutlineType::Solid;

    double worldX      = 0.0;    ///< Top-left corner X in world millimeters
    double worldY      = 0.0;    ///< Top-left corner Y in world millimeters
    double worldWidth  = 60.0;   ///< Width in world millimeters
    double worldHeight = 40.0;   ///< Height in world millimeters

    BLRgba32 strokeColor{0x18, 0x1A, 0x20, 0xFF};        ///< Outline color (alpha always forced to 255 on render)
    BLRgba32 fillColor{0x00, 0x78, 0xD4, 0x40};          ///< Primary infill color
    BLRgba32 secondaryFillColor{0x00, 0xC4, 0xFF, 0x20}; ///< Secondary color (gradients only)

    double strokeWidth  = 1.0;   ///< Outline thickness in world millimeters
    double cornerRadius = 4.0;   ///< Fillet radius for RoundedRectangle (world mm)
    double param1       = 6.0;   ///< Generic param: RegularPolygon side count (3..32)
    double param2       = 0.45;  ///< Generic param: Star inner/outer radius ratio

    ArrowHeadType startArrow = ArrowHeadType::None; ///< Start connector arrowhead (Line/LineArrow compatibility)
    ArrowHeadType endArrow   = ArrowHeadType::None; ///< End connector arrowhead (Line/LineArrow compatibility)
    double arrowHeadSize    = 4.0;                 ///< Arrowhead length in world mm

    // =========================================================================
    // CONSTRUCTORS
    // =========================================================================

    ShapeObject();
    ShapeObject(ShapeType type, double x, double y, double w, double h);

    // =========================================================================
    // PATH GENERATION (implemented in shape_container.cpp)
    // =========================================================================

    /**
     * @brief Builds the world-space Blend2D path for this shape's outline.
     *
     * Each ShapeType has a distinct mathematical construction. The path is built
     * in local (intrinsic) coordinates and then transformed via ctx.apply_transform()
     * in Render(). Calling code must apply transform separately.
     *
     * @param path Output BLPath — cleared before building.
     */
    void BuildPath(BLPath& path) const;

    // =========================================================================
    // BOUNDS & SPATIAL (implemented in shape_container.cpp)
    // =========================================================================

    void UpdateBounds() override;
    bool HitTest(double worldXQuery, double worldYQuery) const override;
    bool HitTestCircle(double worldXQuery, double worldYQuery, double radiusMm) const override;
    bool HitTestSwept(const Point2D& w0, const Point2D& w1, double radiusMm) const override;
    bool Intersects(const AABB& selectionBounds) const override;

    // =========================================================================
    // TRANSFORM (partially inline — bake is trivial axis-aligned only)
    // =========================================================================

    /**
     * @brief Accumulates transform matrix without mutating intrinsic coordinates.
     *
     * This is the critical hot path during gizmo drag. We MUST NOT modify worldX,
     * worldY, worldWidth, worldHeight here or the scale compounds exponentially
     * across 60+ mouse move events per second. Only the BLMatrix2D is updated.
     *
     * @param matrix Cumulative affine matrix from SelectionGizmo::OnPointerMove.
     */
    void ApplyTransform(const BLMatrix2D& matrix) override {
        transform.post_transform(matrix);
        UpdateBounds();
    }

    /**
     * @brief Bakes accumulated transform into intrinsic geometry on drag-end.
     *
     * Only handles axis-aligned scale + translation (no shear/rotation bake —
     * shapes are re-drawn from scratch, so rotation is not "burned in").
     * Called by SelectionGizmo::OnPointerUp() for all selected objects.
     *
     * Math:
     *   p0 = transform * (worldX, worldY)
     *   p1 = transform * (worldX + worldWidth, worldY + worldHeight)
     *   new worldX = min(p0.x, p1.x)  etc.
     */
    void BakeTransform() override;

    // =========================================================================
    // RENDERING (implemented in shape_container.cpp)
    // =========================================================================

    /**
     * @brief Full two-pass Blend2D render: infill then outline.
     *
     * Pass 1 — Infill: solid, semi-transparent, gradient, or tiled hatch pattern.
     * Pass 2 — Outline: always alpha=255 (solid), with optional dash/dot array.
     * Stroke width is scaled by 1/sqrt(det(transform)) to keep visual width
     * invariant during active gizmo preview drag.
     */
    void Render(BLContext& ctx, const Viewport& viewport) const override;

    // =========================================================================
    // DUPLICATION & PERSISTENCE
    // =========================================================================

    std::unique_ptr<CanvasObject> Clone() const override {
        return std::make_unique<ShapeObject>(*this);
    }

    void Serialize(Serializer& /*writer*/) const override {}
    void Deserialize(Deserializer& /*reader*/) override {}

    // =========================================================================
    // GIZMO HANDLES
    // The standard 8-point bounding box is used for all closed shapes.
    // GetCustomGizmoHandles returns false so the engine generates the default grip set.
    // =========================================================================

    // (Uses default CanvasObject::GetCustomGizmoHandles → returns false)
    // (Uses default CanvasObject::OnGizmoHandleDrag → returns false)

    // =========================================================================
    // IMGUI VECTOR ICONS (header-only — ImGui dependency must NOT enter .cpp)
    // =========================================================================

    /**
     * @brief Procedurally draws a high-DPI vector icon for a shape type button.
     *
     * Called from ribbon_bar.hpp to render shape picker cells. Uses only
     * ImDrawList primitives so no Blend2D context is required.
     *
     * @param drawList ImGui draw list to render into.
     * @param type     Shape type to draw icon for.
     * @param pMin     Top-left of the icon bounding box (screen pixels).
     * @param pMax     Bottom-right of the icon bounding box (screen pixels).
     * @param strokeCol RGBA outline color (ImU32).
     * @param fillCol   RGBA fill color (ImU32).
     */
    inline static void DrawShapeIconImGui(ImDrawList* drawList, ShapeType type,
                                          ImVec2 pMin, ImVec2 pMax,
                                          ImU32 strokeCol, ImU32 fillCol) {
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
                drawList->AddEllipseFilled(center, ImVec2(w * 0.48f, h * 0.38f), fillCol);
                drawList->AddEllipse(center, ImVec2(w * 0.48f, h * 0.38f), strokeCol, 0.0f, 24, thickness);
                break;
            }
            case ShapeType::Circle: {
                ImVec2 center(x0 + w * 0.5f, y0 + h * 0.5f);
                float r = (std::min)(w, h) * 0.46f;
                drawList->AddCircleFilled(center, r, fillCol);
                drawList->AddCircle(center, r, strokeCol, 24, thickness);
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
            case ShapeType::RightTriangle: {
                ImVec2 pA(x0, y0);
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
                float rOuter = (std::min)(w, h) * 0.48f;
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
            case ShapeType::RegularPolygon: {
                constexpr int sides = 6;
                ImVec2 pts[sides];
                float cx = x0 + w * 0.5f;
                float cy = y0 + h * 0.5f;
                float r = (std::min)(w, h) * 0.48f;
                for (int i = 0; i < sides; ++i) {
                    float angle = (2.0f * 3.14159265f / sides) * i - 3.14159265f * 0.5f;
                    pts[i] = ImVec2(cx + std::cos(angle) * r, cy + std::sin(angle) * r);
                }
                drawList->AddConvexPolyFilled(pts, sides, fillCol);
                drawList->AddPolyline(pts, sides, strokeCol, ImDrawFlags_Closed, thickness);
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
            case ShapeType::DoubleArrow: {
                float headW = w * 0.35f;
                float shaftH = h * 0.30f;
                float shaftY0 = y0 + (h - shaftH) * 0.5f;
                float shaftY1 = shaftY0 + shaftH;
                ImVec2 pts[10] = {
                    ImVec2(x0, y0 + h * 0.5f),
                    ImVec2(x0 + headW, y0),
                    ImVec2(x0 + headW, shaftY0),
                    ImVec2(x1 - headW, shaftY0),
                    ImVec2(x1 - headW, y0),
                    ImVec2(x1, y0 + h * 0.5f),
                    ImVec2(x1 - headW, y1),
                    ImVec2(x1 - headW, shaftY1),
                    ImVec2(x0 + headW, shaftY1),
                    ImVec2(x0 + headW, y1)
                };
                drawList->AddConvexPolyFilled(pts, 10, fillCol);
                drawList->AddPolyline(pts, 10, strokeCol, ImDrawFlags_Closed, thickness);
                break;
            }
            case ShapeType::Line: {
                drawList->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), strokeCol, thickness * 1.5f);
                break;
            }
            case ShapeType::LineArrow: {
                drawList->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), strokeCol, thickness * 1.5f);
                float dx = x1 - x0, dy = y1 - y0;
                float len = std::hypot(dx, dy);
                if (len > 4.0f) {
                    float ux = dx / len, uy = dy / len;
                    float px = -uy, py = ux;
                    float sz = 6.0f;
                    ImVec2 t0(x1, y1);
                    ImVec2 t1(x1 - sz * ux + sz * 0.5f * px, y1 - sz * uy + sz * 0.5f * py);
                    ImVec2 t2(x1 - sz * ux - sz * 0.5f * px, y1 - sz * uy - sz * 0.5f * py);
                    drawList->AddTriangleFilled(t0, t1, t2, strokeCol);
                }
                break;
            }
            case ShapeType::Heart: {
                ImVec2 center(x0 + w * 0.5f, y0 + h * 0.4f);
                float r = (std::min)(w, h) * 0.22f;
                drawList->AddCircleFilled(ImVec2(center.x - r, center.y), r, fillCol);
                drawList->AddCircleFilled(ImVec2(center.x + r, center.y), r, fillCol);
                ImVec2 pts[3] = {
                    ImVec2(center.x - r * 1.8f, center.y + r * 0.4f),
                    ImVec2(center.x + r * 1.8f, center.y + r * 0.4f),
                    ImVec2(center.x, y1)
                };
                drawList->AddTriangleFilled(pts[0], pts[1], pts[2], fillCol);
                drawList->AddTriangle(pts[0], pts[1], pts[2], strokeCol, thickness);
                break;
            }
            case ShapeType::Cloud: {
                ImVec2 center(x0 + w * 0.5f, y0 + h * 0.5f);
                drawList->AddRectFilled(ImVec2(x0 + 2.0f, center.y), ImVec2(x1 - 2.0f, y1), fillCol, 4.0f);
                drawList->AddCircleFilled(ImVec2(x0 + w * 0.35f, center.y), w * 0.22f, fillCol);
                drawList->AddCircleFilled(ImVec2(x0 + w * 0.65f, center.y - 2.0f), w * 0.25f, fillCol);
                drawList->AddRect(ImVec2(x0 + 2.0f, center.y), ImVec2(x1 - 2.0f, y1), strokeCol, 4.0f, 0, thickness);
                break;
            }
            default: {
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), fillCol, 2.0f);
                drawList->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), strokeCol, 2.0f, 0, thickness);
                break;
            }
        }
    }

    /**
     * @brief Creates a seamlessly tiling Blend2D hatch pattern for drafting fills.
     *
     * Generates a 32×32 pixel tile image with the requested hatch geometry, then
     * wraps it in a BLPattern with BL_EXTEND_MODE_REPEAT and a scale transform
     * so the visible line spacing is spacingMm world millimeters.
     *
     * @param type      Hatch variant (HatchDiagonal, HatchCross, etc.)
     * @param color     Line/dot color for the pattern.
     * @param spacingMm World-space spacing between lines in millimeters.
     * @return Fully configured BLPattern ready to use as a fill style.
     */
    static BLPattern CreateHatchPattern(ShapeFillType type,
                                        const BLRgba32& color,
                                        double strokeWidth,
                                        double spacingMm = 3.0);

    /**
     * @brief Draws a directional arrowhead at a line endpoint.
     *
     * Math: given line direction angle θ,
     *   forward = (cos θ, sin θ),  perp = (-sin θ, cos θ)
     *
     * @param ctx      Blend2D context.
     * @param tipX/Y   World coordinates of the arrowhead tip.
     * @param angleRad Direction angle pointing toward tip (atan2).
     * @param headType Style of termination to draw.
     * @param sizeMm   Arrowhead length in world millimeters.
     * @param color    Stroke and fill color.
     */
    static void DrawArrowHead(BLContext& ctx,
                              double tipX, double tipY, double angleRad,
                              ArrowHeadType headType, double sizeMm,
                              const BLRgba32& color);

    /**
     * @brief Euclidean squared distance from point P to line segment [A, B].
     *
     * Math:
     *   v = B-A,  w = P-A
     *   t = clamp((w·v)/(v·v), 0, 1)
     *   proj = A + t*v
     *   return ||P - proj||²
     */
    static double DistSqPointToSegment(const Point2D& p,
                                       const Point2D& a,
                                       const Point2D& b);
};

/// Backward-compatible alias
using ShapeContainer = ShapeObject;

} // namespace Folio
