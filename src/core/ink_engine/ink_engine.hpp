#pragma once
/**
 * @file core/ink_engine/ink_engine.hpp
 * @brief Unified gateway to Folio-Sketch vector inking engine library.
 *
 * All modules in FolioNote access the Folio-Sketch inking engine, stroke data,
 * collision detection, width evaluation, post-processing, and contour generation
 * through this single header.
 */

// Include Blend2D before Folio-Sketch headers so that BLEND2D_H and BL_PATH_H
// are defined first. This prevents folio_sketch/ink_path.hpp from creating a
// conflicting `using BLPath = Folio::InkPath;` fallback alias.
#include <blend2d/blend2d.h>

#include <folio_sketch/folio_sketch.hpp>

namespace Folio {

/**
 * @brief Overload PackGpuColor for Blend2D's BLRgba32 type.
 * BLRgba32 has member functions .r(), .g(), .b(), .a() rather than member fields.
 */
[[nodiscard]] inline constexpr uint32_t PackGpuColor(BLRgba32 c, float opacity = 1.0f) noexcept {
    uint32_t r = c.r();
    uint32_t g = c.g();
    uint32_t b = c.b();
    uint32_t a = static_cast<uint32_t>(static_cast<float>(c.a()) * (opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity)) + 0.5f);
    return (a << 24) | (b << 16) | (g << 8) | r;
}

/**
 * @brief Helper conversion from BLRgba32 to Folio::ColorRgba.
 */
inline ColorRgba ToColorRgba(BLRgba32 c) noexcept {
    return ColorRgba(static_cast<uint8_t>(c.r()),
                     static_cast<uint8_t>(c.g()),
                     static_cast<uint8_t>(c.b()),
                     static_cast<uint8_t>(c.a()));
}

/**
 * @brief Helper conversion from Folio::ColorRgba to BLRgba32.
 */
inline BLRgba32 ToBLRgba32(ColorRgba c) noexcept {
    return BLRgba32(c.r, c.g, c.b, c.a);
}

/**
 * @brief Tiled hatching pattern generator for shape infill rendering.
 */
inline BLPattern CreateHatchPattern(InfillType type,
                                    const BLRgba32& color,
                                    double strokeWidth,
                                    double spacingMm) {
    constexpr int sz = 256;
    BLImage   img(sz, sz, BL_FORMAT_PRGB32);
    BLContext ictx(img);
    ictx.clear_all();

    BLRgba32 strokeCol = color;
    strokeCol.setA(255);
    ictx.set_stroke_style(strokeCol);

    double infillWorldWidth = (std::max)(0.15, strokeWidth * 0.5);
    double minPitch = 6.0 * infillWorldWidth;
    double effectiveSpacingMm = spacingMm;

    if (type == InfillType::HatchDiagonal || type == InfillType::HatchCross) {
        double minSpacing = minPitch * (2.0 * 1.414213562373);
        effectiveSpacingMm = (std::max)(effectiveSpacingMm, minSpacing);
    } else if (type == InfillType::HatchDots) {
        double minSpacing = minPitch * 2.0;
        effectiveSpacingMm = (std::max)(effectiveSpacingMm, minSpacing);
    } else {
        effectiveSpacingMm = (std::max)(effectiveSpacingMm, minPitch);
    }

    double scale = effectiveSpacingMm / static_cast<double>(sz);
    double infillPixelWidth = (std::max)(1.0, infillWorldWidth / scale);

    ictx.set_stroke_width(infillPixelWidth);
    ictx.set_stroke_caps(BL_STROKE_CAP_SQUARE);
    ictx.set_stroke_join(BL_STROKE_JOIN_MITER_BEVEL);

    const double d = sz * 0.5;

    if (type == InfillType::HatchDiagonal || type == InfillType::HatchCross) {
        for (int k = -2; k <= 2; ++k) {
            double offset = k * d;
            ictx.stroke_line(-sz, -sz + offset, sz * 2.0, sz * 2.0 + offset);
        }
    }
    if (type == InfillType::HatchCross) {
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

/**
 * @brief Paints a polygon interior (solid, translucent, gradient, or drafting hatch).
 */
inline void RenderShapeInfill(BLContext& ctx, const BLPath& region, const InfillStyle& style) {
    if (style.type == InfillType::None || region.is_empty()) return;

    BLBox b = BLBox(style.bounds.x0, style.bounds.y0, style.bounds.x1, style.bounds.y1);
    if (b.x1 <= b.x0 || b.y1 <= b.y0) {
        region.get_bounding_box(&b);
    }
    const double w = b.x1 - b.x0;
    const double h = b.y1 - b.y0;

    BLRgba32 primary = ToBLRgba32(style.primaryColor);
    BLRgba32 secondary = ToBLRgba32(style.secondaryColor);

    if (style.type == InfillType::Solid) {
        primary.setA(255);
        ctx.set_fill_style(primary);
        ctx.fill_path(region);
    } else if (style.type == InfillType::SemiTransparent) {
        if (primary.a() > 180 || primary.a() == 0) primary.setA(90);
        ctx.set_fill_style(primary);
        ctx.fill_path(region);
    } else if (style.type == InfillType::LinearGradient) {
        BLLinearGradientValues vals{ b.x0, b.y0, b.x1, b.y1 };
        BLGradient grad(vals, BL_EXTEND_MODE_PAD);
        grad.add_stop(0.0, primary);
        grad.add_stop(1.0, secondary);
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
        grad.add_stop(0.0, primary);
        grad.add_stop(1.0, secondary);
        ctx.set_fill_style(grad);
        ctx.fill_path(region);
    } else if (style.type >= InfillType::HatchDiagonal &&
               style.type <= InfillType::HatchDots) {
        const double effWidth = style.outlineWidthMm;
        double infillWorldWidth = (std::max)(0.15, effWidth * 0.5);
        double minPitch = 6.0 * infillWorldWidth;
        double basePitch = (std::max)(5.0, minPitch);

        double minDim = (std::min)(w, h);
        if (minDim > 0 && minDim < 20.0) {
            basePitch *= (20.0 / minDim);
        }

        double dynamicSpacing = basePitch;
        if (style.type == InfillType::HatchDiagonal || style.type == InfillType::HatchCross) {
            dynamicSpacing = basePitch * (2.0 * 1.414213562373);
        } else if (style.type == InfillType::HatchDots) {
            dynamicSpacing = basePitch * 2.0;
        }

        primary.setA(255);
        BLPattern hatch = CreateHatchPattern(style.type, primary, effWidth, dynamicSpacing);
        ctx.set_fill_style(hatch);
        ctx.fill_path(region);
    }
}

} // namespace Folio

// Expose Folio namespace types globally for backward compatibility across FolioNote
using Folio::ColorRgba;
using Folio::Point2D;
using Folio::StrokePoint;
using Folio::StylusInputPoint;
using Folio::StylusTelemetry;
using Folio::Segment1D;
using Folio::InkVertex;
using Folio::InkMesh;
using Folio::PackGpuColor;
using Folio::ToColorRgba;
using Folio::ToBLRgba32;
using Folio::RenderShapeInfill;
using Folio::FinishedStrokeData;

using Folio::CapType;
using Folio::StrokePattern;
using Folio::InfillType;
using Folio::InfillStyle;
using Folio::StrokeStyle;
using Folio::SplineMode;
using Folio::InkingConfig;
using Folio::InkingTuningConfig;

using Folio::InkPath;
using Folio::PathVerb;
using Folio::PathPoint;
using Folio::BoundingBox2D;

using Folio::StrokeDebugInfo;
using Folio::InkGenerator;
using Folio::InkMeshBuilder;
using Folio::InkEngine;
using Folio::StrokeCollisionEngine;
using Folio::StrokeWidthEvaluator;
using Folio::StrokePostProcessor;
using Folio::StrokeTopology;
using Folio::StrokeAnalysisResult;
