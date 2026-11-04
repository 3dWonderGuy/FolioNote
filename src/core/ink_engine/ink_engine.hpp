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
 * @brief Converts Folio::InkPath to Blend2D BLPath.
 */
inline void ToBLPath(const InkPath& path, BLPath& dst) {
    dst.clear();
    size_t ptIdx = 0;
    const auto& points = path.points();
    for (PathVerb verb : path.verbs()) {
        switch (verb) {
            case PathVerb::MoveTo:
                if (ptIdx < points.size()) {
                    dst.move_to(points[ptIdx].x, points[ptIdx].y);
                    ptIdx++;
                }
                break;
            case PathVerb::LineTo:
                if (ptIdx < points.size()) {
                    dst.line_to(points[ptIdx].x, points[ptIdx].y);
                    ptIdx++;
                }
                break;
            case PathVerb::CubicTo:
                if (ptIdx + 2 < points.size()) {
                    dst.cubic_to(points[ptIdx].x, points[ptIdx].y,
                                 points[ptIdx + 1].x, points[ptIdx + 1].y,
                                 points[ptIdx + 2].x, points[ptIdx + 2].y);
                    ptIdx += 3;
                }
                break;
            case PathVerb::Close:
                dst.close();
                break;
        }
    }
}

inline BLPath ToBLPath(const InkPath& path) {
    BLPath dst;
    ToBLPath(path, dst);
    return dst;
}

/**
 * @brief Compatibility stroke mesh builder for FolioNote.
 */
inline void BuildStrokeMesh(std::span<const StrokePoint> points,
                            CapType capType,
                            uint32_t colorRgba,
                            InkMesh& outMesh,
                            float scale = 3.7795f,
                            StrokePattern pattern = StrokePattern::Solid) {
    (void)capType;
    (void)scale;
    (void)pattern;
    outMesh.clear();
    const size_t n = points.size();
    if (n == 0) return;

    if (n == 1) {
        const auto& p = points[0];
        const float x = static_cast<float>(p.x);
        const float y = static_cast<float>(p.y);
        const float r = p.r > 0.0f ? p.r : 0.5f;
        constexpr int steps = 16;
        const uint32_t cIdx = static_cast<uint32_t>(outMesh.vertices.size());
        outMesh.vertices.push_back(InkVertex{x, y, 0.0f, r, colorRgba});
        for (int i = 0; i <= steps; ++i) {
            float th = static_cast<float>(i) * (6.28318530718f / static_cast<float>(steps));
            outMesh.vertices.push_back(InkVertex{x + std::cos(th) * r, y + std::sin(th) * r, 1.0f, r, colorRgba});
        }
        for (int i = 0; i < steps; ++i) {
            outMesh.indices.push_back(cIdx);
            outMesh.indices.push_back(cIdx + 1 + i);
            outMesh.indices.push_back(cIdx + 1 + ((i + 1) % steps));
        }
        outMesh.coreIndexCount = static_cast<uint32_t>(outMesh.indices.size());
        return;
    }

    outMesh.vertices.reserve(n * 4);
    outMesh.indices.reserve(n * 6);

    for (size_t i = 0; i < n; ++i) {
        float r = points[i].r > 0.0f ? points[i].r : 0.5f;
        float nx = 0.0f, ny = 1.0f;
        if (i + 1 < n) {
            float dx = static_cast<float>(points[i + 1].x - points[i].x);
            float dy = static_cast<float>(points[i + 1].y - points[i].y);
            float len = std::hypot(dx, dy);
            if (len > 1e-4f) { nx = -dy / len; ny = dx / len; }
        } else if (i > 0) {
            float dx = static_cast<float>(points[i].x - points[i - 1].x);
            float dy = static_cast<float>(points[i].y - points[i - 1].y);
            float len = std::hypot(dx, dy);
            if (len > 1e-4f) { nx = -dy / len; ny = dx / len; }
        }
        float x = static_cast<float>(points[i].x);
        float y = static_cast<float>(points[i].y);
        outMesh.vertices.push_back(InkVertex{x + nx * r, y + ny * r, 1.0f, r, colorRgba});
        outMesh.vertices.push_back(InkVertex{x - nx * r, y - ny * r, -1.0f, r, colorRgba});
    }

    for (size_t i = 0; i + 1 < n; ++i) {
        uint32_t v0 = static_cast<uint32_t>(i * 2);
        uint32_t v1 = v0 + 1;
        uint32_t v2 = v0 + 2;
        uint32_t v3 = v0 + 3;
        outMesh.indices.push_back(v0);
        outMesh.indices.push_back(v1);
        outMesh.indices.push_back(v2);
        outMesh.indices.push_back(v2);
        outMesh.indices.push_back(v1);
        outMesh.indices.push_back(v3);
    }
    outMesh.coreIndexCount = static_cast<uint32_t>(outMesh.indices.size());
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

    BLBox b;
    region.get_bounding_box(&b);
    const double w = b.x1 - b.x0;
    const double h = b.y1 - b.y0;

    BLRgba32 fillCol = ToBLRgba32(style.color);

    if (style.type == InfillType::Solid) {
        ctx.set_fill_style(fillCol);
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

        BLPattern hatch = CreateHatchPattern(style.type, fillCol, effWidth, dynamicSpacing);
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
using Folio::Segment1D;
using Folio::InkVertex;
using Folio::InkMesh;
using Folio::PackGpuColor;
using Folio::ToColorRgba;
using Folio::ToBLRgba32;
using Folio::ToBLPath;
using Folio::BuildStrokeMesh;
using Folio::RenderShapeInfill;
using Folio::FinishedStrokeData;
using Folio::CapsuleVec2;
using Folio::CapsuleInstance;
using Folio::CapsuleBuilder;
using Folio::PackPremultipliedGpuColor;

using Folio::CapType;
using Folio::StrokePattern;
using Folio::InfillType;
using Folio::InfillStyle;
using Folio::StrokeStyle;
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
using Folio::StrokeSlicer;
using Folio::StrokeWidthEvaluator;
using Folio::StrokePostProcessor;
