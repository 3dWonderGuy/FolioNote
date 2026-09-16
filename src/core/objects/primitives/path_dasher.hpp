#pragma once
/**
 * @file path_dasher.hpp
 * @brief High-precision polyline & Bézier curve stroke dasher for Blend2D.
 *
 * Background & Motivation:
 * Blend2D's rasterizer (as of beta18) accepts set_stroke_dash_array() into its internal
 * BLStrokeOptions state struct without error, but the rasterizer pipeline currently ignores
 * dash_array during raster emission and renders continuous unbroken strokes.
 *
 * PathDasher solves this cleanly at the vector geometry layer: it decomposes any arbitrary
 * input BLPath (composed of lines, quadratic splines, cubic splines, and closed figures)
 * into discrete, independent stroked sub-paths according to the specified ShapeOutlineType.
 *
 * Supported Patterns:
 *  - ShapeOutlineType::Solid:   Unchanged source geometry (continuous stroke).
 *  - ShapeOutlineType::Dashed:  Dashes (length = max(2.5, 3.5 * strokeWidth)) separated by
 *                               gaps (length = max(1.8, 2.2 * strokeWidth)).
 *  - ShapeOutlineType::Dotted:  Micro-segments (length = 0.15 mm) separated by gaps
 *                               (length = max(2.0, 2.5 * strokeWidth)). When stroked with
 *                               BL_STROKE_CAP_ROUND, these render as crisp circular dots of
 *                               diameter equal to strokeWidth.
 *  - ShapeOutlineType::DashDot: Alternating long dash, gap, dot, gap.
 *
 * Mathematical Algorithm & General Working Process:
 * 1. Curve Flattening:
 *    Iterates through the BLPath's command array (view.command_data) and vertex array
 *    (view.vertex_data).
 *    - Quadratic Béziers (BL_PATH_CMD_QUAD) with control C1 and endpoint P1 are evaluated
 *      via: B(t) = (1-t)^2 P0 + 2(1-t)t C1 + t^2 P1, where step count N = clamp(chord / 1.5, 6, 32).
 *    - Cubic Béziers (BL_PATH_CMD_CUBIC) with controls C1, C2 and endpoint P1 are evaluated
 *      via Bernstein basis polynomials:
 *      B(t) = (1-t)^3 P0 + 3(1-t)^2 t C1 + 3(1-t) t^2 C2 + t^3 P1, with step count N = clamp(chord / 1.5, 8, 48).
 *    - Line segments (BL_PATH_CMD_ON) and close commands (BL_PATH_CMD_CLOSE connecting back
 *      to figureStart) are added directly as chord vertices.
 *
 * 2. Polygonal Arc-Length Phase Dashing:
 *    The continuous vertex sequence V_0, V_1, ..., V_m is traversed segment by segment:
 *    For segment [P_A, P_B] of length L = hypot(dx, dy) with unit tangent u = (P_B - P_A) / L:
 *    - If remaining segment length fits within current pattern phase:
 *        Advances by remaining length; if in "draw" phase, emits line segment to dst.
 *    - If segment exceeds current pattern phase:
 *        Advances by remaining phase length, finishes the phase (if "draw", terminates sub-path),
 *        cycles to the next pattern phase, and repeats until the segment is fully consumed.
 *
 * 3. Sub-path Emission:
 *    Each active "draw" phase starts with dst.move_to(x, y) and proceeds with dst.line_to(x', y').
 *    Gaps advance coordinates without adding geometry, producing disconnected sub-strokes.
 */

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <vector>
#include <algorithm>
#include <blend2d/blend2d.h>
#include "core/objects/primitives/shape_types.hpp"

namespace Folio {

class PathDasher {
public:
    /**
     * @brief Converts any source BLPath into a dashed/dotted BLPath based on ShapeOutlineType.
     *
     * @param src Source vector path.
     * @param dst Destination path where dashed sub-paths are emitted.
     * @param outlineType Outline pattern (Dashed, Dotted, DashDot).
     * @param strokeWidth Line width in millimeters for proportional dash/gap sizing.
     */
    static void BuildDashedPath(const BLPath& src, BLPath& dst, ShapeOutlineType outlineType, double strokeWidth) {
        dst.reset();
        if (outlineType == ShapeOutlineType::Solid || outlineType == ShapeOutlineType::None) {
            dst = src;
            return;
        }

        double sw = (std::max)(strokeWidth, 0.5);

        // Pattern definition: alternating [drawLen, gapLen, ...]
        std::vector<double> pattern;
        if (outlineType == ShapeOutlineType::Dashed) {
            double dashLen = (std::max)(2.5, sw * 3.5);
            double gapLen  = (std::max)(1.8, sw * 2.2);
            pattern = { dashLen, gapLen };
        } else if (outlineType == ShapeOutlineType::Dotted) {
            double dotLen = 0.15; // Tiny segment rendered with BL_STROKE_CAP_ROUND -> circular dot
            double gapLen = (std::max)(2.0, sw * 2.5);
            pattern = { dotLen, gapLen };
        } else if (outlineType == ShapeOutlineType::DashDot) {
            double dashLen = (std::max)(2.5, sw * 3.5);
            double gapLen  = (std::max)(1.8, sw * 2.0);
            double dotLen  = 0.15;
            pattern = { dashLen, gapLen, dotLen, gapLen };
        } else {
            dst = src;
            return;
        }

        size_t count = src.size();
        if (count < 2) return;

        const BLPoint* vtx = src.vertex_data();
        const uint8_t* cmd = src.command_data();

        // Separate path into continuous polygonal figures
        std::vector<BLPoint> currentFigure;
        BLPoint figureStart{0.0, 0.0};
        BLPoint lastPt{0.0, 0.0};

        // Traverses flattened polygonal chain and emits dashed sub-paths into dst
        auto flushFigureDashes = [&](const std::vector<BLPoint>& figPts) {
            if (figPts.size() < 2) return;

            size_t patternIdx = 0;
            double phaseRem = pattern[0];
            bool isDraw = true;
            bool inSubPath = false;

            for (size_t k = 0; k + 1 < figPts.size(); ++k) {
                BLPoint pA = figPts[k];
                BLPoint pB = figPts[k + 1];
                double dx = pB.x - pA.x;
                double dy = pB.y - pA.y;
                double segLen = std::hypot(dx, dy);
                if (segLen < 1e-6) continue;

                double invLen = 1.0 / segLen;
                double ux = dx * invLen;
                double uy = dy * invLen;

                BLPoint curr = pA;
                double remLen = segLen;

                while (remLen > 0.0) {
                    if (remLen < phaseRem) {
                        BLPoint nextPt{curr.x + ux * remLen, curr.y + uy * remLen};
                        if (isDraw) {
                            if (!inSubPath) {
                                dst.move_to(curr.x, curr.y);
                                inSubPath = true;
                            }
                            dst.line_to(nextPt.x, nextPt.y);
                        }
                        phaseRem -= remLen;
                        curr = nextPt;
                        remLen = 0.0;
                    } else {
                        BLPoint nextPt{curr.x + ux * phaseRem, curr.y + uy * phaseRem};
                        if (isDraw) {
                            if (!inSubPath) {
                                dst.move_to(curr.x, curr.y);
                                inSubPath = true;
                            }
                            dst.line_to(nextPt.x, nextPt.y);
                            inSubPath = false; // End of current draw dash
                        }
                        remLen -= phaseRem;
                        curr = nextPt;
                        patternIdx = (patternIdx + 1) % pattern.size();
                        phaseRem = pattern[patternIdx];
                        isDraw = (patternIdx % 2 == 0);
                    }
                }
            }
        };

        size_t i = 0;
        while (i < count) {
            uint8_t c = cmd[i];
            switch (c) {
                case BL_PATH_CMD_MOVE: {
                    if (!currentFigure.empty()) {
                        flushFigureDashes(currentFigure);
                        currentFigure.clear();
                    }
                    figureStart = vtx[i];
                    lastPt = vtx[i];
                    currentFigure.push_back(vtx[i]);
                    i++;
                    break;
                }
                case BL_PATH_CMD_ON: {
                    lastPt = vtx[i];
                    currentFigure.push_back(vtx[i]);
                    i++;
                    break;
                }
                case BL_PATH_CMD_QUAD: {
                    BLPoint p0 = lastPt;
                    BLPoint c1 = vtx[i];
                    BLPoint p1 = vtx[i + 1];
                    double chord = std::hypot(p1.x - p0.x, p1.y - p0.y);
                    int steps = std::clamp(static_cast<int>(chord / 1.5), 6, 32);
                    for (int s = 1; s <= steps; ++s) {
                        double t = static_cast<double>(s) / steps;
                        double u = 1.0 - t;
                        BLPoint pt{
                            u * u * p0.x + 2.0 * u * t * c1.x + t * t * p1.x,
                            u * u * p0.y + 2.0 * u * t * c1.y + t * t * p1.y
                        };
                        currentFigure.push_back(pt);
                    }
                    lastPt = p1;
                    i += 2;
                    break;
                }
                case BL_PATH_CMD_CUBIC: {
                    BLPoint p0 = lastPt;
                    BLPoint c1 = vtx[i];
                    BLPoint c2 = vtx[i + 1];
                    BLPoint p1 = vtx[i + 2];
                    double chord = std::hypot(p1.x - p0.x, p1.y - p0.y);
                    int steps = std::clamp(static_cast<int>(chord / 1.5), 8, 48);
                    for (int s = 1; s <= steps; ++s) {
                        double t = static_cast<double>(s) / steps;
                        double u = 1.0 - t;
                        double tt = t * t;
                        double uu = u * u;
                        BLPoint pt{
                            uu * u * p0.x + 3.0 * uu * t * c1.x + 3.0 * u * tt * c2.x + tt * t * p1.x,
                            uu * u * p0.y + 3.0 * uu * t * c1.y + 3.0 * u * tt * c2.y + tt * t * p1.y
                        };
                        currentFigure.push_back(pt);
                    }
                    lastPt = p1;
                    i += 3;
                    break;
                }
                case BL_PATH_CMD_CLOSE: {
                    if (!currentFigure.empty() && (lastPt.x != figureStart.x || lastPt.y != figureStart.y)) {
                        currentFigure.push_back(figureStart);
                        lastPt = figureStart;
                    }
                    flushFigureDashes(currentFigure);
                    currentFigure.clear();
                    i++;
                    break;
                }
                default:
                    i++;
                    break;
            }
        }

        if (!currentFigure.empty()) {
            flushFigureDashes(currentFigure);
            currentFigure.clear();
        }
    }
};

} // namespace Folio
