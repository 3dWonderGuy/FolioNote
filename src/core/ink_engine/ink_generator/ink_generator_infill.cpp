#include "core/ink_engine/ink_generator.hpp"

#include <vector>
#include <cmath>
#include <algorithm>

// =============================================================================
// 1. POLYGON INTERIOR BOUNDARY PATH
// =============================================================================

BLPath InkGenerator::BuildInfill(const std::vector<Point2D>& vertices) {
    BLPath path;
    if (vertices.size() < 3) return path;

    path.move_to(vertices[0].x, vertices[0].y);
    for (size_t i = 1; i < vertices.size(); ++i) {
        path.line_to(vertices[i].x, vertices[i].y);
    }
    path.close();
    return path;
}

// =============================================================================
// 2. INFILL PAINTER (Solid, Translucent, Gradient, Hatch)
// =============================================================================

void InkGenerator::FillInfill(BLContext& ctx, const BLPath& region, const InfillStyle& style) {
    if (style.type == InfillType::None || region.is_empty()) return;

    const BLBox& b = style.bounds;
    const double w = b.x1 - b.x0;
    const double h = b.y1 - b.y0;

    if (style.type == InfillType::Solid) {
        BLRgba32 col = style.primaryColor;
        col.setA(255);
        ctx.set_fill_style(col);
        ctx.fill_path(region);

    } else if (style.type == InfillType::SemiTransparent) {
        BLRgba32 col = style.primaryColor;
        if (col.a() > 180 || col.a() == 0) col.setA(90);
        ctx.set_fill_style(col);
        ctx.fill_path(region);

    } else if (style.type == InfillType::LinearGradient) {
        BLLinearGradientValues vals{ b.x0, b.y0, b.x1, b.y1 };
        BLGradient grad(vals, BL_EXTEND_MODE_PAD);
        grad.add_stop(0.0, style.primaryColor);
        grad.add_stop(1.0, style.secondaryColor);
        ctx.set_fill_style(grad);
        ctx.fill_path(region);

    } else if (style.type == InfillType::RadialGradient) {
        BLRadialGradientValues vals{
            b.x0 + w * 0.5,
            b.y0 + h * 0.5,
            0.0, 0.0,
            (std::max)(w, h) * 0.5
        };
        BLGradient grad(vals, BL_EXTEND_MODE_PAD);
        grad.add_stop(0.0, style.primaryColor);
        grad.add_stop(1.0, style.secondaryColor);
        ctx.set_fill_style(grad);
        ctx.fill_path(region);

    } else if (style.type >= InfillType::HatchDiagonal &&
               style.type <= InfillType::HatchDots) {
        const double effWidth = style.outlineWidthMm;

        // Infill lines are half (1/2) of the outline thickness
        double infillWorldWidth = (std::max)(0.15, effWidth * 0.5);

        // Doubled spacing requirement:
        // Clear gap between lines: gap = pitch - infillWorldWidth >= 5.0 * infillWorldWidth.
        // Therefore, minimum perpendicular pitch: pitch >= 6.0 * infillWorldWidth.
        double minPitch = 6.0 * infillWorldWidth;
        double basePitch = (std::max)(5.0, minPitch);

        // Dynamic scale: increase pitch if shape is very small to avoid dense bleeding
        double minDim = (std::min)(w, h);
        if (minDim > 0 && minDim < 20.0) {
            basePitch *= (20.0 / minDim);
        }

        // Convert desired perpendicular line pitch to BLPattern tile spacingMm:
        // For 45-degree diagonal lines: d_perp = spacingMm / (2 * sqrt(2)) => spacingMm = pitch * 2 * sqrt(2).
        // For horizontal/vertical lines: pitch = spacingMm.
        // For dots: axis spacing = 0.5 * spacingMm => spacingMm = pitch * 2.
        double dynamicSpacing = basePitch;
        if (style.type == InfillType::HatchDiagonal || style.type == InfillType::HatchCross) {
            dynamicSpacing = basePitch * (2.0 * 1.414213562373);
        } else if (style.type == InfillType::HatchDots) {
            dynamicSpacing = basePitch * 2.0;
        }

        // Alpha enforcement: textured infill is ALWAYS 100% solid, fully opaque (never translucent)
        BLRgba32 solidFillColor = style.primaryColor;
        solidFillColor.setA(255);

        BLPattern hatch = CreateHatchPattern(style.type, solidFillColor, effWidth, dynamicSpacing);
        ctx.set_fill_style(hatch);
        ctx.fill_path(region);
    }
}

// =============================================================================
// 3. TILED DRAFTING HATCH PATTERN FACTORY
// =============================================================================

BLPattern InkGenerator::CreateHatchPattern(InfillType type,
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

    if (type == InfillType::HatchDiagonal || type == InfillType::HatchCross) {
        // Perpendicular line distance for 45° lines is d_perp = spacingMm / (2 * sqrt(2)).
        // Enforcing d_perp >= minPitch yields spacingMm >= minPitch * 2 * sqrt(2).
        double minSpacing = minPitch * (2.0 * 1.414213562373);
        effectiveSpacingMm = (std::max)(effectiveSpacingMm, minSpacing);
    } else if (type == InfillType::HatchDots) {
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

    if (type == InfillType::HatchDiagonal || type == InfillType::HatchCross) {
        // 45-degree diagonal lines: y = x + k * d (for k = -2, -1, 0, 1, 2)
        // All lines are stroked from x = -sz to x = 2*sz so ends wrap seamlessly across boundaries.
        for (int k = -2; k <= 2; ++k) {
            double offset = k * d;
            ictx.stroke_line(-sz, -sz + offset, sz * 2.0, sz * 2.0 + offset);
        }
    }
    if (type == InfillType::HatchCross) {
        // Perpendicular 45-degree diagonal lines: y = -x + c (for c = 0, d, 2d, 3d, 4d)
        for (int k = 0; k <= 4; ++k) {
            double c = k * d;
            ictx.stroke_line(-sz, sz + c, sz * 2.0, -2.0 * sz + c);
        }
    }
    if (type == InfillType::HatchHorizontal) {
        ictx.stroke_line(-sz, d, sz * 2.0, d);
    }
    if (type == InfillType::HatchVertical) {
        ictx.stroke_line(d, -sz, d, sz * 2.0);
    }
    if (type == InfillType::HatchDots) {
        ictx.set_fill_style(strokeCol);
        double dotRadiusPx = (std::max)(1.5, infillPixelWidth * 0.5);
        ictx.fill_circle(sz * 0.25, sz * 0.25, dotRadiusPx);
        ictx.fill_circle(sz * 0.75, sz * 0.75, dotRadiusPx);
    }
    ictx.end();

    return BLPattern(img, BL_EXTEND_MODE_REPEAT,
                     BLMatrix2D::make_scaling(scale, scale));
}
