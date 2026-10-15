#pragma once
/**
 * =========================================================================================
 * @file core/ink_engine/ink_generator.hpp
 * @brief Universal 2D Vector Geometry Generator (Google Ink Modeler + Blend2D)
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & TESSELLATION ALGORITHMS:
 * -----------------------------------------------
 * Constructs continuous 2D closed polygon contours (BLPath) from pressure-modeled,
 * smoothed centerline points for hardware-accelerated non-zero fill rendering via Blend2D.
 *
 * Generation Pipeline:
 *   STEP 1: Choose generation type
 *             -- Polygon (standard shapes)  -> closed loop, always Round joins, supports infill
 *             -- Stroke  (ink)              -> open line, honors CapType, no infill
 *   STEP 2: Apply line pattern to the outline (Solid, Dashed, Dotted, DashDot)
 *   STEP 3: (Polygons only) Infill — BuildInfill() region + FillInfill() paint
 *             (Solid, SemiTransparent, Linear/Radial Gradient, Drafting Hatches)
 *
 * Implementation files:
 *   - ink_generator/ink_generator_polygon.cpp  (Dispatcher & closed polygon generation)
 *   - ink_generator/ink_generator_stroke.cpp   (Open stroke & solid ribbon tessellation)
 *   - ink_generator/ink_generator_patterns.cpp (Dashed, dotted, and dash-dot algorithms)
 *   - ink_generator/ink_generator_infill.cpp   (Interior polygon infill & drafting hatches)
 */

#include <vector>
#include <blend2d/blend2d.h>

#include "core/ink_engine/ink_stroke_data.hpp"

class InkGenerator {
public:
    /**
     * @brief Builds a 2D closed ribbon polygon or patterned outline (solid, dashed, dotted) from an ordered list of points.
     *
     * @param centerline Input centerline vertices with thickness.
     * @param capType Endcap style for terminal segments (ink strokes only; polygons are always Round).
     * @param pattern Stroke pattern (Solid, Dashed, Dotted, DashDot).
     * @param isStandardShape When true (standard vector shape), closes the loop from the last point to the first point seamlessly.
     */
    static BLPath GenerateStroke(const std::vector<Point2D>& centerline,
                                 CapType capType = CapType::Round,
                                 StrokePattern pattern = StrokePattern::Solid,
                                 bool isStandardShape = false);

    /**
     * @brief Builds the interior fill region of a polygon (the area enclosed by its vertices).
     *
     * Only polygons support infill. Ink strokes never do (future stroke analysis may
     * promote enclosed ink into a polygon first).
     *
     * @param vertices Polygon perimeter vertices (open or already closed).
     * @return Closed BLPath of the enclosed area, ready for solid / gradient / hatch fill.
     */
    static BLPath BuildInfill(const std::vector<Point2D>& vertices);

    /**
     * @brief Paints a polygon interior (solid, translucent, gradient, or drafting hatch).
     *
     * @param ctx    Blend2D context (caller owns save/transform state).
     * @param region Closed interior region (e.g. from BuildInfill or a shape path).
     * @param style  Fill type, colors, outline width and world bounds.
     */
    static void FillInfill(BLContext& ctx, const BLPath& region, const InfillStyle& style);

private:
    // =========================================================================
    // STEP 1: GENERATION TYPE (Polygon vs Stroke)
    // =========================================================================

    // Builds an isolated endcap for a single-point stroke (tap / dot)
    static BLPath BuildSinglePointCap(const Point2D& pt, CapType capType);

    // Builds an open ink stroke. Continuous lines route directly to ribbon generation
    static BLPath BuildStroke(const std::vector<Point2D>& centerline,
                              CapType capType = CapType::Round,
                              StrokePattern pattern = StrokePattern::Solid);

    // Builds a polygon from vertices, ensuring the loop is closed seamlessly
    static BLPath BuildPolygon(const std::vector<Point2D>& centerline,
                               StrokePattern pattern = StrokePattern::Solid);

    // =========================================================================
    // STEP 2: STROKE & RIBBON TESSELLATION
    // =========================================================================

    // Builds a continuous 2D closed ribbon polygon from an ordered list of points
    static BLPath BuildSolidRibbon(const std::vector<Point2D>& pts, CapType capType = CapType::Round);

    // Dispatches non-solid discontinuous patterns (Dotted, Dashed, DashDot)
    static BLPath ApplyDiscontinuousPattern(const std::vector<Point2D>& pts,
                                            CapType capType,
                                            StrokePattern pattern);

    // Generates a dotted stroke contour spaced proportionally to stroke thickness
    static BLPath BuildDottedPattern(const std::vector<Point2D>& pts);

    // Generates an arc-length parameterized dashed stroke contour
    static BLPath BuildDashedPattern(const std::vector<Point2D>& pts, CapType capType);

    // Generates an arc-length parameterized dash-dot sequence contour
    static BLPath BuildDashDotPattern(const std::vector<Point2D>& pts, CapType capType);

    // =========================================================================
    // STEP 3: INFILL HATCH PATTERN FACTORY
    // =========================================================================

    // Creates a seamlessly tiling drafting hatch BLPattern
    static BLPattern CreateHatchPattern(InfillType type,
                                        const BLRgba32& color,
                                        double strokeWidth,
                                        double spacingMm = 6.0);
};