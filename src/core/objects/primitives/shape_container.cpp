/**
 * @file shape_container.cpp
 * @brief Implementation of ShapeObject — closed 2D geometric vector shape.
 *
 * All heavy Blend2D rendering, path construction, hatch pattern generation,
 * hit-testing, and bounds calculation lives here. DrawShapeIconImGui is the
 * sole function that stays in shape_container.hpp because it has an ImGui
 * draw-list dependency that must not enter non-UI translation units.
 *
 * Organization:
 *   1. Constructors
 *   2. BuildPath       — geometric path per ShapeType
 *   3. UpdateBounds    — AABB from world coords + transform
 *   4. HitTest         — point-in-shape and proximity tests
 *   5. BakeTransform   — commits accumulated matrix to intrinsic coords
 *   6. Render          — two-pass BLContext draw (infill, outline)
 *   7. CreateHatchPattern — tiled drafting texture factory
 *   8. DrawArrowHead   — directional arrowhead geometry
 *   9. DistSqPointToSegment — geometry helper
 *  10. DrawShapeIconImGui   — ImGui icon renderer (header-only, defined here
 *                            via explicit instantiation to keep it in one place)
 */

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <algorithm>

#include "core/objects/primitives/shape_container.hpp"
#include "core/objects/primitives/path_dasher.hpp"
#include "core/engine/canvas_transform.hpp"  // for Viewport

// Prevent MSVC min/max macro conflicts with <algorithm>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// ImGui included for DrawShapeIconImGui only — isolated at the bottom of this file
#include <imgui.h>

namespace Folio {

// =============================================================================
// 1. CONSTRUCTORS
// =============================================================================

ShapeObject::ShapeObject() {
    type = ObjectType::Shape;
    UpdateBounds();
}

ShapeObject::ShapeObject(ShapeType shType, double x, double y, double w, double h)
    : shapeType(shType), worldX(x), worldY(y), worldWidth(w), worldHeight(h)
{
    this->type = ObjectType::Shape;
    UpdateBounds();
}

// =============================================================================
// 2. BUILD PATH
// =============================================================================

/**
 * @brief Builds the world-space Blend2D path for this shape's outline.
 *
 * All coordinates are in world millimeters. The path is used both for
 * rendering (stroke/fill) and for hit-testing via BLPath::hit_test().
 *
 * Coordinate conventions:
 *   x, y = top-left of bounding box (worldX, worldY)
 *   w, h = dimensions (worldWidth, worldHeight) — clamped to 0.01 minimum
 */
void ShapeObject::BuildPath(BLPath& path) const {
    path.clear();
    const double x = worldX;
    const double y = worldY;
    const double w = (std::max)(worldWidth,  0.01);
    const double h = (std::max)(worldHeight, 0.01);

    switch (shapeType) {
        // -----------------------------------------------------------------------
        case ShapeType::Rectangle: {
            path.add_rect(BLRect(x, y, w, h));
            break;
        }
        // -----------------------------------------------------------------------
        case ShapeType::RoundedRectangle: {
            // Fillet radius clamped so it never exceeds half the shorter dimension
            double r = (std::min)({ cornerRadius, w * 0.5, h * 0.5 });
            path.add_round_rect(BLRoundRect(x, y, w, h, r, r));
            break;
        }
        // -----------------------------------------------------------------------
        case ShapeType::Circle: {
            // Symmetric circle centered in the bounding box; radius = min(w,h)/2
            double cx = x + w * 0.5;
            double cy = y + h * 0.5;
            double r  = (std::min)(w, h) * 0.5;
            path.add_circle(BLCircle(cx, cy, r));
            break;
        }
        // -----------------------------------------------------------------------
        case ShapeType::Ellipse: {
            // Full ellipse: rx = w/2, ry = h/2, centered in bounding box
            path.add_ellipse(BLEllipse(x + w * 0.5, y + h * 0.5, w * 0.5, h * 0.5));
            break;
        }
        // -----------------------------------------------------------------------
        case ShapeType::Triangle: {
            // Isosceles: apex top-center, base bottom-left to bottom-right
            // Vertices: (x+w/2, y) → (x+w, y+h) → (x, y+h) → close
            path.move_to(x + w * 0.5, y);
            path.line_to(x + w,       y + h);
            path.line_to(x,           y + h);
            path.close();
            break;
        }
        // -----------------------------------------------------------------------
        case ShapeType::RightTriangle: {
            // 90-degree angle at bottom-left corner
            // Vertices: (x, y+h) → (x, y) → (x+w, y+h) → close
            path.move_to(x,       y + h);  // bottom-left (90° corner)
            path.line_to(x,       y);      // apex top-left
            path.line_to(x + w,   y + h);  // bottom-right
            path.close();
            break;
        }
        // -----------------------------------------------------------------------
        case ShapeType::RegularPolygon: {
            // N-sided regular polygon, apex at top (-π/2 start angle)
            // Vertex i: (cx + cos(-π/2 + i*2π/N)*rx, cy + sin(-π/2 + i*2π/N)*ry)
            int sides = std::clamp(static_cast<int>(std::round(param1)), 3, 32);
            double cx    = x + w * 0.5;
            double cy    = y + h * 0.5;
            double rx    = w * 0.5;
            double ry    = h * 0.5;
            double step  = (2.0 * M_PI) / sides;
            double start = -M_PI * 0.5;
            for (int i = 0; i < sides; ++i) {
                double angle = start + i * step;
                double px    = cx + std::cos(angle) * rx;
                double py    = cy + std::sin(angle) * ry;
                if (i == 0) path.move_to(px, py);
                else         path.line_to(px, py);
            }
            path.close();
            break;
        }
        // -----------------------------------------------------------------------
        case ShapeType::Star: {
            // 5-point star: alternating outer/inner radius vertices
            // param2 = inner/outer ratio (default 0.45)
            int    points  = 5;
            double cx      = x + w * 0.5;
            double cy      = y + h * 0.5;
            double rOuter  = (std::min)(w, h) * 0.5;
            double rInner  = rOuter * param2;
            double step    = M_PI / points;
            double start   = -M_PI * 0.5;
            for (int i = 0; i < points * 2; ++i) {
                double r     = (i % 2 == 0) ? rOuter : rInner;
                double angle = start + i * step;
                double px    = cx + std::cos(angle) * r;
                double py    = cy + std::sin(angle) * r;
                if (i == 0) path.move_to(px, py);
                else         path.line_to(px, py);
            }
            path.close();
            break;
        }
        // -----------------------------------------------------------------------
        case ShapeType::Heart: {
            // Two cubic bezier lobes meeting at a bottom apex
            double cx   = x + w * 0.5;
            double topY = y + h * 0.3;
            path.move_to(cx, y + h);
            path.cubic_to(x - w * 0.1, y + h * 0.5,  x,           y,    cx, topY);
            path.cubic_to(x + w,        y,             x + w * 1.1, y + h * 0.5, cx, y + h);
            path.close();
            break;
        }
        // -----------------------------------------------------------------------
        // -----------------------------------------------------------------------
        case ShapeType::Cloud: {
            // Rounded rect base + two ellipse bumps on top
            double r = (std::min)(w, h) * 0.25;
            path.add_round_rect(BLRoundRect(x, y + h * 0.3, w, h * 0.7, r, r));
            path.add_ellipse(BLEllipse(x + w * 0.3,  y + h * 0.4,  w * 0.25, h * 0.35));
            path.add_ellipse(BLEllipse(x + w * 0.65, y + h * 0.35, w * 0.28, h * 0.38));
            break;
        }
        // -----------------------------------------------------------------------
        case ShapeType::SineWave: {
            // Continuous harmonic sinusoidal wave
            // param1 stores cycle count (default 3.0 periods)
            double cycles = (param1 > 0.05) ? param1 : 3.0;
            WaveShapeGenerator::BuildSineWavePath(path, x, y, w, h, cycles);
            break;
        }
        // -----------------------------------------------------------------------
        case ShapeType::SquareWave: {
            // Orthogonal digital pulse train square wave
            // param1 stores cycle count (default 3.0 periods)
            double cycles = (param1 > 0.05) ? param1 : 3.0;
            WaveShapeGenerator::BuildSquareWavePath(path, x, y, w, h, cycles, 0.5);
            break;
        }
        // -----------------------------------------------------------------------
        case ShapeType::TriangleWave: {
            // Symmetric linear triangle wave
            // param1 stores cycle count (default 3.0 periods)
            double cycles = (param1 > 0.05) ? param1 : 3.0;
            WaveShapeGenerator::BuildTriangleWavePath(path, x, y, w, h, cycles);
            break;
        }
        // -----------------------------------------------------------------------
        case ShapeType::RightTriangleWave: {
            // Right triangle / sawtooth wave
            // param1 stores cycle count (default 3.0 periods)
            // param2 switches the right angle side: 0.0 = right-angle on right, 1.0 = right-angle on left
            double cycles = (param1 > 0.05) ? param1 : 3.0;
            bool rightAngleOnRight = (param2 < 0.5);
            WaveShapeGenerator::BuildRightTriangleWavePath(path, x, y, w, h, cycles, rightAngleOnRight);
            break;
        }
        // -----------------------------------------------------------------------
        default:
            // Unknown type: fall back to rectangle bounding box
            path.add_rect(BLRect(x, y, w, h));
            break;
    }
}

// =============================================================================
// 3. BOUNDS
// =============================================================================

/**
 * @brief Computes world-space AABB including stroke half-width and transform.
 *
 * The bounding box is expanded by (strokeWidth/2 + 1.0) on all sides so
 * the rendered outline falls within the AABB for correct spatial indexing.
 * The accumulated transform matrix is applied to all four corners, and the
 * axis-aligned envelope is taken.
 */
void ShapeObject::UpdateBounds() {
    double halfStroke = (outlineType != ShapeOutlineType::None)
                         ? (strokeWidth * 0.5 + 1.0)
                         : 1.0;

    double minX = worldX - halfStroke;
    double minY = worldY - halfStroke;
    double maxX = worldX + worldWidth  + halfStroke;
    double maxY = worldY + worldHeight + halfStroke;

    // Transform the four corners and take the axis-aligned envelope
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

// =============================================================================
// 4. HIT TESTING
// =============================================================================

/**
 * @brief Squared distance from point P to line segment [A, B].
 *
 * Math:
 *   v = B-A,  w = P-A
 *   t = clamp((w·v) / (v·v), 0, 1)
 *   proj = A + t*v
 *   return ||P - proj||²
 */
double ShapeObject::DistSqPointToSegment(const Point2D& p,
                                         const Point2D& a,
                                         const Point2D& b) {
    double vx = b.x - a.x,  vy = b.y - a.y;
    double wx = p.x - a.x,  wy = p.y - a.y;
    double c1 = wx * vx + wy * vy;
    if (c1 <= 0.0) return wx * wx + wy * wy;
    double c2 = vx * vx + vy * vy;
    if (c2 <= c1) return (p.x - b.x) * (p.x - b.x) + (p.y - b.y) * (p.y - b.y);
    double t    = c1 / c2;
    double projX = a.x + t * vx;
    double projY = a.y + t * vy;
    return (p.x - projX) * (p.x - projX) + (p.y - projY) * (p.y - projY);
}

bool ShapeObject::HitTest(double worldXQuery, double worldYQuery) const {
    if (!bounds.Contains(worldXQuery, worldYQuery)) return false;

    // Map query point through inverse transform to local shape space
    BLMatrix2D invTransform;
    if (BLMatrix2D::invert(invTransform, transform) != BL_SUCCESS) {
        invTransform = BLMatrix2D::make_identity();
    }
    BLPoint localPt = invTransform.map_point(worldXQuery, worldYQuery);

    BLPath path;
    BuildPath(path);

    // Inside fill
    if (fillType != ShapeFillType::None) {
        if (path.hit_test(localPt, BL_FILL_RULE_NON_ZERO) == BL_HIT_TEST_IN)
            return true;
    }

    // Proximity to outline stroke
    double hitTol = (std::max)(strokeWidth * 0.5, 2.5);
    if (outlineType != ShapeOutlineType::None || fillType == ShapeFillType::None) {
        if (localPt.x >= worldX - hitTol && localPt.x <= worldX + worldWidth  + hitTol &&
            localPt.y >= worldY - hitTol && localPt.y <= worldY + worldHeight + hitTol)
            return true;
    }
    return false;
}

bool ShapeObject::HitTestCircle(double worldXQuery, double worldYQuery, double radiusMm) const {
    AABB queryBox(worldXQuery - radiusMm, worldYQuery - radiusMm,
                  worldXQuery + radiusMm, worldYQuery + radiusMm);
    if (!bounds.Intersects(queryBox)) return false;
    return HitTest(worldXQuery, worldYQuery) || bounds.Contains(worldXQuery, worldYQuery);
}

bool ShapeObject::HitTestSwept(const Point2D& w0, const Point2D& w1, double radiusMm) const {
    AABB sweptBox(
        (std::min)(w0.x, w1.x) - radiusMm, (std::min)(w0.y, w1.y) - radiusMm,
        (std::max)(w0.x, w1.x) + radiusMm, (std::max)(w0.y, w1.y) + radiusMm
    );
    if (!bounds.Intersects(sweptBox)) return false;
    return HitTestCircle(w0.x, w0.y, radiusMm) ||
           HitTestCircle(w1.x, w1.y, radiusMm) ||
           HitTestCircle((w0.x + w1.x) * 0.5, (w0.y + w1.y) * 0.5, radiusMm);
}

bool ShapeObject::Intersects(const AABB& selectionBounds) const {
    return bounds.Intersects(selectionBounds);
}

// =============================================================================
// 5. BAKE TRANSFORM
// =============================================================================

/**
 * @brief Commits accumulated affine matrix into intrinsic geometry coordinates.
 *
 * Only handles axis-aligned transforms (no shear). Called by
 * SelectionGizmo::OnPointerUp() so that the next drag starts from fresh
 * intrinsic coords with an identity transform.
 *
 * Math (axis-aligned case — m01 ≈ 0, m10 ≈ 0):
 *   p0 = (m00*worldX + m20,              m11*worldY + m21)
 *   p1 = (m00*(worldX+worldWidth) + m20, m11*(worldY+worldHeight) + m21)
 *   new worldX      = min(p0.x, p1.x)
 *   new worldY      = min(p0.y, p1.y)
 *   new worldWidth  = max(0.5, |p1.x - p0.x|)
 *   new worldHeight = max(0.5, |p1.y - p0.y|)
 */
void ShapeObject::BakeTransform() {
    // Only bake axis-aligned scale + translation (shear baking would require
    // rebuilding vertex geometry, which is not yet supported)
    if (std::abs(transform.m01) < 1e-6 && std::abs(transform.m10) < 1e-6) {
        if (transform.m00 == 1.0 && transform.m11 == 1.0 &&
            transform.m20 == 0.0 && transform.m21 == 0.0)
            return; // Identity — nothing to bake

        double p0x = transform.m00 * worldX               + transform.m20;
        double p0y = transform.m11 * worldY               + transform.m21;
        double p1x = transform.m00 * (worldX + worldWidth)  + transform.m20;
        double p1y = transform.m11 * (worldY + worldHeight) + transform.m21;

        worldX      = (std::min)(p0x, p1x);
        worldY      = (std::min)(p0y, p1y);
        worldWidth  = (std::max)(0.5, std::abs(p1x - p0x));
        worldHeight = (std::max)(0.5, std::abs(p1y - p0y));

        transform = BLMatrix2D::make_identity();
        UpdateBounds();
    }
}

// =============================================================================
// 6. RENDER
// =============================================================================

/**
 * @brief Full two-pass Blend2D rendering: infill then outline.
 *
 * The accumulated transform is applied via ctx.apply_transform() before
 * drawing, so all coordinates are in world space relative to the canvas origin.
 *
 * Stroke Width Invariance During Drag:
 *   While a gizmo drag is in progress, the transform matrix contains a scale
 *   factor S. Drawing strokeWidth naively would render S×strokeWidth pixels
 *   wide. We compensate by dividing:
 *       effectiveStrokeWidth = strokeWidth / sqrt(det(transform))
 *   where det = m00*m11 - m01*m10.
 *
 * Alpha Enforcement:
 *   Outlines are always 100% opaque (alpha = 255), regardless of what alpha
 *   value is stored in strokeColor. This is enforced by calling setA(255).
 */
void ShapeObject::Render(BLContext& ctx, const Viewport& viewport) const {
    (void)viewport;
    if (!isVisible) return;

    BLPath path;
    BuildPath(path);

    ctx.save();
    ctx.apply_transform(transform);

    // ----- Pass 1: Infill -----
    if (fillType != ShapeFillType::None) {
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
            // Left-to-right two-stop gradient
            BLLinearGradientValues vals{ worldX, worldY,
                                         worldX + worldWidth, worldY + worldHeight };
            BLGradient grad(vals, BL_EXTEND_MODE_PAD);
            grad.add_stop(0.0, fillColor);
            grad.add_stop(1.0, secondaryFillColor);
            ctx.set_fill_style(grad);
            ctx.fill_path(path);

        } else if (fillType == ShapeFillType::RadialGradient) {
            // Center-outward two-stop gradient
            BLRadialGradientValues vals{
                worldX + worldWidth  * 0.5,
                worldY + worldHeight * 0.5,
                0.0, 0.0,
                (std::max)(worldWidth, worldHeight) * 0.5
            };
            BLGradient grad(vals, BL_EXTEND_MODE_PAD);
            grad.add_stop(0.0, fillColor);
            grad.add_stop(1.0, secondaryFillColor);
            ctx.set_fill_style(grad);
            ctx.fill_path(path);

        } else if (fillType >= ShapeFillType::HatchDiagonal &&
                   fillType <= ShapeFillType::HatchDots) {
            // Stroke width invariance: compensate for active scale transform
            double det         = std::abs(transform.m00 * transform.m11
                                         - transform.m01 * transform.m10);
            double scaleFactor = (det > 1e-6) ? std::sqrt(det) : 1.0;
            double effWidth    = strokeWidth / scaleFactor;

            // Infill lines are half (1/2) of the outline thickness
            double infillWorldWidth = (std::max)(0.15, effWidth * 0.5);

            // Doubled spacing requirement:
            // Clear gap between lines: gap = pitch - infillWorldWidth >= 5.0 * infillWorldWidth.
            // Therefore, minimum perpendicular pitch: pitch >= 6.0 * infillWorldWidth.
            double minPitch = 6.0 * infillWorldWidth;
            double basePitch = (std::max)(5.0, minPitch);

            // Dynamic scale: increase pitch if shape is very small to avoid dense bleeding
            double minDim = std::min(worldWidth, worldHeight);
            if (minDim > 0 && minDim < 20.0) {
                basePitch *= (20.0 / minDim);
            }

            // Convert desired perpendicular line pitch to BLPattern tile spacingMm:
            // For 45-degree diagonal lines: d_perp = spacingMm / (2 * sqrt(2)) => spacingMm = pitch * 2 * sqrt(2).
            // For horizontal/vertical lines: pitch = spacingMm.
            // For dots: axis spacing = 0.5 * spacingMm => spacingMm = pitch * 2.
            double dynamicSpacing = basePitch;
            if (fillType == ShapeFillType::HatchDiagonal || fillType == ShapeFillType::HatchCross) {
                dynamicSpacing = basePitch * (2.0 * 1.414213562373);
            } else if (fillType == ShapeFillType::HatchDots) {
                dynamicSpacing = basePitch * 2.0;
            }

            // Alpha enforcement: textured infill is ALWAYS 100% solid, fully opaque (never translucent)
            BLRgba32 solidFillColor = fillColor;
            solidFillColor.setA(255);

            BLPattern hatch = CreateHatchPattern(fillType, solidFillColor, effWidth, dynamicSpacing);
            ctx.set_fill_style(hatch);
            ctx.fill_path(path);
        }

        ctx.restore();
    }

    // ----- Pass 2: Outline -----
    if (outlineType != ShapeOutlineType::None && strokeWidth > 0.0) {
        ctx.save();

        // Alpha enforcement: outline is always solid
        BLRgba32 solidStroke = strokeColor;
        solidStroke.setA(255);
        ctx.set_stroke_style(solidStroke);

        // Stroke width invariance: compensate for active scale transform
        // det(M) = m00*m11 - m01*m10  (determinant of 2x2 sub-matrix)
        double det         = std::abs(transform.m00 * transform.m11
                                     - transform.m01 * transform.m10);
        double scaleFactor = (det > 1e-6) ? std::sqrt(det) : 1.0;
        double effWidth    = strokeWidth / scaleFactor;
        ctx.set_stroke_width(effWidth);
        ctx.set_stroke_caps(BL_STROKE_CAP_ROUND);
        ctx.set_stroke_join(BL_STROKE_JOIN_ROUND);

        if (outlineType == ShapeOutlineType::Solid) {
            ctx.stroke_path(path);
        } else {
            BLPath dashedPath;
            PathDasher::BuildDashedPath(path, dashedPath, outlineType, effWidth);
            ctx.stroke_path(dashedPath);
        }

        ctx.restore();
    }

    ctx.restore();
}

// =============================================================================
// 7. HATCH PATTERN FACTORY
// =============================================================================

/**
 * @brief Creates a seamlessly tiling drafting hatch BLPattern.
 *
 * Draws one 32×32 pixel tile using Blend2D into a BLImage, then wraps it
 * in a BLPattern with BL_EXTEND_MODE_REPEAT. A scale transform maps pixel
 * tile coordinates to world-space: scale = spacingMm / tileSize.
 *
 * Hatch types:
 *   HatchDiagonal  — three diagonal lines at 45°, wrapping seamlessly:
 *                    (0,0)→(sz,sz), (0,sz/2)→(sz/2,sz), (sz/2,0)→(sz,sz/2)
 *   HatchCross     — same as Diagonal plus the perpendicular set
 *   HatchHorizontal— single horizontal line at y = sz/2
 *   HatchVertical  — single vertical line at x = sz/2
 *   HatchDots      — two offset dots at (sz/4, sz/4) and (sz*3/4, sz*3/4)
 */
BLPattern ShapeObject::CreateHatchPattern(ShapeFillType type,
                                           const BLRgba32& color,
                                           double strokeWidth,
                                           double spacingMm) {
    // Use high-resolution 256×256 tile to eliminate pixelation and bilinear magnification blur
    constexpr int sz = 256;
    BLImage   img(sz, sz, BL_FORMAT_PRGB32);
    BLContext ictx(img);
    ictx.clear_all();

    // Alpha enforcement: infill patterns are ALWAYS 100% solid, fully opaque (never translucent)
    BLRgba32 strokeCol = color;
    strokeCol.setA(255);
    ictx.set_stroke_style(strokeCol);

    // Infill lines are specified to be exactly half (0.5) of the outline thickness.
    // The 256×256 image tile is scaled by scale = effectiveSpacingMm / sz in world space.
    // Therefore, a desired world line width W_world = strokeWidth * 0.5 requires a
    // pixel stroke width on the tile of: W_px = W_world / scale = (strokeWidth * 0.5) * (sz / effectiveSpacingMm).
    double infillWorldWidth = (std::max)(0.15, strokeWidth * 0.5);

    // Enforce doubled spacing requirement:
    // Pitch between lines must satisfy: pitch = gap + infillWorldWidth >= 6.0 * infillWorldWidth.
    double minPitch = 6.0 * infillWorldWidth;
    double effectiveSpacingMm = spacingMm;

    if (type == ShapeFillType::HatchDiagonal || type == ShapeFillType::HatchCross) {
        // Perpendicular line distance for 45° lines is d_perp = spacingMm / (2 * sqrt(2)).
        // Enforcing d_perp >= minPitch yields spacingMm >= minPitch * 2 * sqrt(2).
        double minSpacing = minPitch * (2.0 * 1.414213562373);
        effectiveSpacingMm = (std::max)(effectiveSpacingMm, minSpacing);
    } else if (type == ShapeFillType::HatchDots) {
        // Dot spacing along grid axes is 0.5 * spacingMm => spacingMm >= minPitch * 2.
        double minSpacing = minPitch * 2.0;
        effectiveSpacingMm = (std::max)(effectiveSpacingMm, minSpacing);
    } else {
        // HatchHorizontal, HatchVertical: pitch = spacingMm.
        effectiveSpacingMm = (std::max)(effectiveSpacingMm, minPitch);
    }

    double scale = effectiveSpacingMm / static_cast<double>(sz);
    double infillPixelWidth = (std::max)(1.0, infillWorldWidth / scale);

    ictx.set_stroke_width(infillPixelWidth);
    ictx.set_stroke_caps(BL_STROKE_CAP_SQUARE);
    ictx.set_stroke_join(BL_STROKE_JOIN_MITER_BEVEL);

    // Continuous boundary wrapping:
    // When stroking lines across a tiling box, lines that touch the borders must extend
    // beyond the [0, sz] boundary by at least 1 tile period. This guarantees that stroke
    // width is not truncated at tile boundaries, eliminating blurred or faded seam artifacts.
    const double d = sz * 0.5;

    if (type == ShapeFillType::HatchDiagonal || type == ShapeFillType::HatchCross) {
        // 45-degree diagonal lines: y = x + k * d (for k = -2, -1, 0, 1, 2)
        // All lines are stroked from x = -sz to x = 2*sz so ends wrap seamlessly across boundaries.
        for (int k = -2; k <= 2; ++k) {
            double offset = k * d;
            ictx.stroke_line(-sz, -sz + offset, sz * 2.0, sz * 2.0 + offset);
        }
    }
    if (type == ShapeFillType::HatchCross) {
        // Perpendicular 45-degree diagonal lines: y = -x + c (for c = 0, d, 2d, 3d, 4d)
        for (int k = 0; k <= 4; ++k) {
            double c = k * d;
            ictx.stroke_line(-sz, sz + c, sz * 2.0, -2.0 * sz + c);
        }
    }
    if (type == ShapeFillType::HatchHorizontal) {
        ictx.stroke_line(-sz, d, sz * 2.0, d);
    }
    if (type == ShapeFillType::HatchVertical) {
        ictx.stroke_line(d, -sz, d, sz * 2.0);
    }
    if (type == ShapeFillType::HatchDots) {
        ictx.set_fill_style(strokeCol);
        double dotRadiusPx = (std::max)(1.5, infillPixelWidth * 0.5);
        ictx.fill_circle(sz * 0.25, sz * 0.25, dotRadiusPx);
        ictx.fill_circle(sz * 0.75, sz * 0.75, dotRadiusPx);
    }
    ictx.end();

    return BLPattern(img, BL_EXTEND_MODE_REPEAT,
                     BLMatrix2D::make_scaling(scale, scale));
}

// =============================================================================
// 8. DRAW ARROWHEAD
// =============================================================================

/**
 * @brief Draws a directional arrowhead at a line endpoint.
 *
 * Direction math (all relative to line angle θ):
 *   forward = (cos θ, sin θ)
 *   perpendicular = (-sin θ, cos θ)
 *
 *  Triangle: solid filled isosceles triangle, tip at (tipX, tipY)
 *            base center at tip - sizeMm * forward
 *            half-width = sizeMm * 0.55 along perpendicular
 *
 *  Stealth:  same as Triangle with notch indentation at 75% of shaft:
 *            notch point = tip - sizeMm * 0.75 * forward
 *
 *  Open:     wireframe V — two lines from base corners to tip, no fill
 *
 *  Circle:   filled circle at tip, radius = sizeMm * 0.4
 */
void ShapeObject::DrawArrowHead(BLContext& ctx,
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
            double bx     = tipX - sizeMm * cosA;
            double by     = tipY - sizeMm * sinA;
            double halfW  = sizeMm * 0.55;
            BLPath arr;
            arr.move_to(tipX, tipY);
            arr.line_to(bx + halfW * perpX, by + halfW * perpY);
            arr.line_to(bx - halfW * perpX, by - halfW * perpY);
            arr.close();
            ctx.fill_path(arr);
            break;
        }
        case ArrowHeadType::Stealth: {
            double bx    = tipX - sizeMm * cosA;
            double by    = tipY - sizeMm * sinA;
            double ix    = tipX - sizeMm * 0.75 * cosA;
            double iy    = tipY - sizeMm * 0.75 * sinA;
            double halfW = sizeMm * 0.65;
            BLPath arr;
            arr.move_to(tipX, tipY);
            arr.line_to(bx + halfW * perpX, by + halfW * perpY);
            arr.line_to(ix, iy);
            arr.line_to(bx - halfW * perpX, by - halfW * perpY);
            arr.close();
            ctx.fill_path(arr);
            break;
        }
        case ArrowHeadType::Open: {
            double bx    = tipX - sizeMm * cosA;
            double by    = tipY - sizeMm * sinA;
            double halfW = sizeMm * 0.6;
            BLPath arr;
            arr.move_to(bx + halfW * perpX, by + halfW * perpY);
            arr.line_to(tipX, tipY);
            arr.line_to(bx - halfW * perpX, by - halfW * perpY);
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

// =============================================================================
// 9. IMGUI ICONS (header-only definition — included here for single-file authoring)
// =============================================================================

/**
 * @brief Procedurally draws a shape icon in an ImGui draw list.
 *
 * This function is declared in shape_container.hpp (header-only / inline) so
 * ribbon_bar.hpp can call it without requiring a .cpp link dependency on ImGui
 * from the core library.
 *
 * Implementation is placed here (below the #include <imgui.h>) so it can be
 * maintained alongside the rest of the shape rendering code. It is marked
 * inline in the header so the linker does not complain about multiple
 * definitions when included from multiple translation units.
 */

} // namespace Folio
