#include "core/ink_engine/ink_generator.hpp"

#include <vector>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =============================================================================
// 1. NIB GEOMETRY & SUPPORT OFFSETS
// =============================================================================

/**
 * @brief Returns the oriented polygon vertices for a given cap type centered at origin (0, 0).
 * 
 * Each nib shape is defined as a closed convex polygon:
 *   - Square: Upright 90-degree highlighter rectangle (0.9r wide x 2.6r tall).
 *   - Chisel: 45-degree calligraphy diamond nib (simulates fountain pen).
 *   - Round:  32-vertex circle for smooth marker/ballpoint pens.
 *
 * @param capType The pen tip geometry (Square, Chisel, Round).
 * @param r Centerline radius used to scale the nib footprint.
 * @return Ordered list of local vertex coordinates forming the nib perimeter.
 */
static std::vector<Point2D> GetNibVertices(CapType capType, double r) {
    std::vector<Point2D> verts;

    switch (capType) {
        case CapType::Square: {
            // Highlighter nib: narrow horizontal width (hw = 0.45r), tall vertical blade (hh = 1.3r)
            // Aspect ratio ~ 3:1 creates thick horizontal bands and thin vertical strokes.
            double hw = r * 0.45;
            double hh = r * 1.3;
            verts = {
                { -hw, -hh }, // Top-Left corner
                { +hw, -hh }, // Top-Right corner
                { +hw, +hh }, // Bottom-Right corner
                { -hw, +hh }  // Bottom-Left corner
            };
            break;
        }

        case CapType::Chisel: {
            // Calligraphy nib: fixed 45-degree angled diamond blade.
            // Produces hairline strokes when moving parallel to 45°, thick ribbons when moving at 135°.
            constexpr double angle = M_PI * -0.25; // Fixed 45-degree fountain pen slant
            double dx = std::cos(angle) * r * 1.3; // Major axis half-length
            double dy = std::sin(angle) * r * 1.3;
            double nx = -dy * 0.1;                 // Minor axis half-thickness (40% of length)
            double ny =  dx * 0.1;
            verts = {
                { -dx - nx, -dy - ny }, // Rear blade tip
                { +dx - nx, +dy - ny }, // Right blade corner
                { +dx + nx, +dy + ny }, // Front blade tip
                { -dx + nx, -dy + ny }  // Left blade corner
            };
            break;
        }

        case CapType::Round:
        default: {
            // Standard circular nib: sampled with 32 radial vertices for ultra-smooth curves
            constexpr int steps = 32;
            verts.reserve(steps);
            for (int i = 0; i < steps; ++i) {
                double a = (2.0 * M_PI * i) / steps;
                verts.push_back({ std::cos(a) * r, std::sin(a) * r });
            }
            break;
        }
    }
    return verts;
}

/**
 * @brief Calculate where and how out caps verticies are attached to out polygon
 * 
 * Computes the extreme boundary offsets (outLeft, outRight) for the stroke envelope
 * based on the current stroke normal vector (nx, ny):
 *   - Left edge:  Vertex of the nib maximizing projection onto +normal
 *   - Right edge: Vertex of the nib minimizing projection onto +normal (maximizing onto -normal)
 *
 * @param capType The pen tip geometry.
 * @param r Current point radius (pressure-dependent).
 * @param nx Normalized X component of the stroke perpendicular normal.
 * @param ny Normalized Y component of the stroke perpendicular normal.
 * @param outLeft Output: local offset from centerline to left ribbon boundary.
 * @param outRight Output: local offset from centerline to right ribbon boundary.
 */
static void GetRibbonOffsets(CapType capType, double r, double nx, double ny,
                             Point2D& outLeft, Point2D& outRight) {

    if (capType == CapType::Round) {
        // Circle: support points are simply along ± normal
        outLeft  = {  nx * r,  ny * r };
        outRight = { -nx * r, -ny * r };
        return;
    }

    // For Square (highlighter) and Chisel (calligraphy):
    // The ribbon edges MUST attach directly to the extreme corners of the actual nib polygon.
    // Dot product (v.x * nx + v.y * ny) finds which corner is farthest along +normal (Left) and -normal (Right).
    auto verts = GetNibVertices(capType, r);
    Point2D bestLeft = verts[0];
    Point2D bestRight = verts[0];
    double maxDot = -1e9;
    double minDot = 1e9;

    for (const auto& v : verts) {
        double dot = v.x * nx + v.y * ny;
        if (dot > maxDot) { maxDot = dot; bestLeft  = v; }
        if (dot < minDot) { minDot = dot; bestRight = v; }
    }

    outLeft  = bestLeft;
    outRight = bestRight;
}

// =============================================================================
// 2. CAP TRACERS (Single Tap & Endcaps)
// =============================================================================

// Traces a standalone closed cap polygon for a single-point tap (click / touch-down dot)
BLPath InkGenerator::TraceCap(const StrokePoint& pt, CapType capType) {
    BLPath path;

    // 1. Fetch the oriented nib perimeter vertices scaled by point radius pt.r
    auto verts = GetNibVertices(capType, pt.r);
    if (verts.empty()) return path;

    // 2. Begin closed polygon at the first corner/vertex offset by touch point (pt.x, pt.y)
    path.move_to(pt.x + verts[0].x, pt.y + verts[0].y);

    // 3. Connect perimeter vertices in counter-clockwise order around the nib
    for (size_t i = 1; i < verts.size(); ++i) {
        path.line_to(pt.x + verts[i].x, pt.y + verts[i].y);
    }

    // 4. Seal the loop back to the start vertex to form a solid filled shape
    path.close();
    return path;
}

// Sub-tracer for Round circular endcap arc (Start and End)
static void TraceRoundCap(BLPath& path,
                          const StrokePoint& center,
                          const Point2D& startPt,
                          const Point2D& endPt,
                          double vLen) {

    double r = center.r;

    // 1. Calculate the angular sweep spanned by chord vLen on a circle of radius r:
    //    Geometry: chord L = 2 * r * sin(sweep / 2)  ==>  sweep = 2 * asin(L / (2 * r))
    //    For a full 180° endcap across diameter (vLen = 2r), halfRatio = 1.0 ==> sweep = PI.
    double halfRatio = std::clamp(vLen / (2.0 * r), 0.0, 1.0);
    double sweep = 2.0 * std::asin(halfRatio);

    // Flat edge or degenerate angle: connect straight to endPt without generating arc points
    if (sweep < 1e-4) {
        path.line_to(endPt.x, endPt.y);
        return;
    }

    // 2. Start angle: exact angle where the ribbon wall arrived at startPt
    double aStart = std::atan2(startPt.y - center.y, startPt.x - center.x);

    // 3. Scale vertex count dynamically with angle (12 steps for full 180° cap, fewer for tight corners)
    const int arcSteps = std::max(2, static_cast<int>(std::ceil(12.0 * (sweep / M_PI))));

    // 4. Sample intermediate vertices sweeping outward from startPt towards endPt
    for (int i = 1; i < arcSteps; ++i) {
        double t = static_cast<double>(i) / arcSteps;
        double a = aStart - t * sweep;
        path.line_to(center.x + std::cos(a) * r, center.y + std::sin(a) * r);
    }

    // 5. Seal arc to the exact destination point
    path.line_to(endPt.x, endPt.y);
}

// Sub-tracer for oriented polygon endcap facets (Square & Chisel)
static void TracePolygonCap(BLPath& path,
                            const StrokePoint& center,
                            const Point2D& startPt,
                            const Point2D& endPt,
                            double ux, double uy,
                            double vx, double vy,
                            CapType capType) {
                                
    auto verts = GetNibVertices(capType, center.r);
    Point2D startRel = { startPt.x - center.x, startPt.y - center.y };
    Point2D endRel   = { endPt.x - center.x,   endPt.y - center.y };

    std::vector<Point2D> outwardVerts;
    for (const auto& v : verts) {
        double dStart = std::hypot(v.x - startRel.x, v.y - startRel.y);
        double dEnd   = std::hypot(v.x - endRel.x,   v.y - endRel.y);
        if (dStart < 1e-3 || dEnd < 1e-3) continue;

        double projOut = (v.x - startRel.x) * ux + (v.y - startRel.y) * uy;
        if (projOut > 1e-4) {
            outwardVerts.push_back(v);
        }
    }

    if (outwardVerts.size() > 1) {
        std::sort(outwardVerts.begin(), outwardVerts.end(), [&](const Point2D& a, const Point2D& b) {
            return (a.x * vx + a.y * vy) < (b.x * vx + b.y * vy);
        });
    }

    for (const auto& v : outwardVerts) {
        path.line_to(center.x + v.x, center.y + v.y);
    }
    path.line_to(endPt.x, endPt.y);
}

// Universal ribbon endcap connector (Head & Tail)
void InkGenerator::TraceCap(BLPath& path,
                            const StrokePoint& center,
                            const Point2D& leftPt,
                            const Point2D& rightPt,
                            CapType capType,
                            bool isTail) {
    const Point2D& startPt = isTail ? leftPt : rightPt;
    const Point2D& endPt   = isTail ? rightPt : leftPt;

    double vx = endPt.x - startPt.x;
    double vy = endPt.y - startPt.y;
    double vLen = std::hypot(vx, vy);

    // Degenerate zero-length / zero-radius guard
    if (vLen < 1e-7 || center.r < 1e-6) {
        path.line_to(endPt.x, endPt.y);
        return;
    }

    double ux = -vy / vLen;
    double uy =  vx / vLen;

    if (capType == CapType::Square || capType == CapType::Chisel) {
        TracePolygonCap(path, center, startPt, endPt, ux, uy, vx, vy, capType);
    } else {
        TraceRoundCap(path, center, startPt, endPt, vLen);
    }
}

// =============================================================================
// 3. OPEN STROKE BUILDER
// =============================================================================

BLPath InkGenerator::BuildStroke(const std::vector<StrokePoint>& centerline,
                                 CapType capType,
                                 StrokePattern pattern) {

    // check if data is even valid
    if (centerline.empty()) return BLPath{};
    if (centerline.size() == 1) return TraceCap(centerline[0], capType);

    if (pattern == StrokePattern::Solid) {
        return BuildSolidRibbon(centerline, capType);
    }
    return ApplyDiscontinuousPattern(centerline, pattern);
}

// =============================================================================
// 4. CONTINUOUS SOLID RIBBON TESSELLATION
// =============================================================================

BLPath InkGenerator::BuildSolidRibbon(const std::vector<StrokePoint>& pts, CapType capType) {
    BLPath path;
    if (pts.empty()) return path;
    if (pts.size() == 1) return TraceCap(pts[0], capType);

    // 1. Pre-filter: collapse consecutive duplicate/stationary points (< 1e-4 mm)
    std::vector<StrokePoint> cleanPts;
    cleanPts.reserve(pts.size());
    for (const auto& p : pts) {
        if (cleanPts.empty() || std::hypot(p.x - cleanPts.back().x, p.y - cleanPts.back().y) >= 1e-4) {
            cleanPts.push_back(p);
        }
    }

    if (cleanPts.size() <= 1) {
        return TraceCap(cleanPts.empty() ? pts[0] : cleanPts[0], capType);
    }

    const size_t N = cleanPts.size();
    std::vector<Point2D> left;
    std::vector<Point2D> right;
    left.reserve(N * 2);
    right.reserve(N * 2);

    // Step 1: Compute smoothed boundary chains with sharp turn subdivision (theta limit)
    for (size_t i = 0; i < N; ++i) {
        // First point (Head)
        if (i == 0) {
            double dx = cleanPts[1].x - cleanPts[0].x;
            double dy = cleanPts[1].y - cleanPts[0].y;
            double len = std::hypot(dx, dy);
            double nx = (len > 1e-7) ? -dy / len : 0.0;
            double ny = (len > 1e-7) ?  dx / len : 1.0;

            Point2D offL, offR;
            GetRibbonOffsets(capType, cleanPts[0].r, nx, ny, offL, offR);
            left.push_back({ cleanPts[0].x + offL.x,  cleanPts[0].y + offL.y });
            right.push_back({ cleanPts[0].x + offR.x, cleanPts[0].y + offR.y });
            continue;
        }

        // Last point (Tail)
        if (i == N - 1) {
            double dx = cleanPts[N - 1].x - cleanPts[N - 2].x;
            double dy = cleanPts[N - 1].y - cleanPts[N - 2].y;
            double len = std::hypot(dx, dy);
            double nx = (len > 1e-7) ? -dy / len : 0.0;
            double ny = (len > 1e-7) ?  dx / len : 1.0;

            Point2D offL, offR;
            GetRibbonOffsets(capType, cleanPts[N - 1].r, nx, ny, offL, offR);
            left.push_back({ cleanPts[N - 1].x + offL.x,  cleanPts[N - 1].y + offL.y });
            right.push_back({ cleanPts[N - 1].x + offR.x, cleanPts[N - 1].y + offR.y });
            break;
        }

        // Intermediate Joint (i-1 -> i -> i+1)
        double dxIn  = cleanPts[i].x - cleanPts[i - 1].x;
        double dyIn  = cleanPts[i].y - cleanPts[i - 1].y;
        double lenIn = std::hypot(dxIn, dyIn);

        double dxOut  = cleanPts[i + 1].x - cleanPts[i].x;
        double dyOut  = cleanPts[i + 1].y - cleanPts[i].y;
        double lenOut = std::hypot(dxOut, dyOut);

        double uxIn  = (lenIn  > 1e-7) ? dxIn  / lenIn  : 1.0;
        double uyIn  = (lenIn  > 1e-7) ? dyIn  / lenIn  : 0.0;
        double uxOut = (lenOut > 1e-7) ? dxOut / lenOut : 1.0;
        double uyOut = (lenOut > 1e-7) ? dyOut / lenOut : 0.0;

        // Angle between incoming and outgoing directions
        double dot   = uxIn * uxOut + uyIn * uyOut;
        double cross = uxIn * uyOut - uyIn * uxOut;
        double turnAngle = std::atan2(std::abs(cross), dot);

        // THETA LIMIT: If turn is gentle (<= 45°), use angle bisector with miter compensation
        constexpr double kMaxTurnPerStep = M_PI * 0.25; // 45 degrees
        if (turnAngle <= kMaxTurnPerStep) {
            double nx = -uyIn - uyOut;
            double ny =  uxIn + uxOut;
            double nLen = std::hypot(nx, ny);
            if (nLen > 1e-6) {
                nx /= nLen;
                ny /= nLen;
            } else {
                nx = -uyIn;
                ny =  uxIn;
            }

            // Miter compensation: prevents line from thinning down across the bend
            double miter = std::min(1.4, 2.0 / std::max(0.5, nLen));
            nx *= miter;
            ny *= miter;

            Point2D offL, offR;
            GetRibbonOffsets(capType, cleanPts[i].r, nx, ny, offL, offR);
            left.push_back({ cleanPts[i].x + offL.x,  cleanPts[i].y + offL.y });
            right.push_back({ cleanPts[i].x + offR.x, cleanPts[i].y + offR.y });
        } else {
            // SHARP TURN (> 45° up to 180° hairpin reversal):
            // 1. Compute exact uncorrupted incoming and outgoing normal offsets directly at joint cleanPts[i].
            //    (Avoids bisector sum cancellation (-uyIn - uyOut) which vanishes to (0,0) at 180° and normalizes noise).
            double nxIn  = -uyIn;
            double nyIn  =  uxIn;
            double nxOut = -uyOut;
            double nyOut =  uxOut;

            Point2D inL, inR, outL, outR;
            GetRibbonOffsets(capType, cleanPts[i].r, nxIn,  nyIn,  inL,  inR);
            GetRibbonOffsets(capType, cleanPts[i].r, nxOut, nyOut, outL, outR);

            // 2. Only sweep the OUTSIDE (convex) wall in small angular steps (<= 22.5° each).
            //    The INSIDE (concave) wall cleanly connects incoming edge to outgoing edge directly at joint i.
            //    This guarantees boundaries never cut through the stroke tube, preventing winding cancellation.
            constexpr double kSubStepAngle = M_PI / 8.0; // 22.5 degrees per step
            int steps = std::max(2, static_cast<int>(std::ceil(turnAngle / kSubStepAngle)));
            double turnSign = (cross >= 0.0) ? 1.0 : -1.0;
            double aIn = std::atan2(uyIn, uxIn);

            if (cross >= 0.0) {
                // Turn is LEFT:
                // Inside is Left: connects incoming inL to outgoing outL across the joint
                left.push_back({ cleanPts[i].x + inL.x,  cleanPts[i].y + inL.y });
                left.push_back({ cleanPts[i].x + outL.x, cleanPts[i].y + outL.y });

                // Outside is Right: sweeps arc from inR to outR
                for (int s = 0; s <= steps; ++s) {
                    double t = static_cast<double>(s) / steps;
                    double heading = aIn + turnSign * t * turnAngle;
                    double nx = -std::sin(heading);
                    double ny =  std::cos(heading);

                    Point2D offL, offR;
                    GetRibbonOffsets(capType, cleanPts[i].r, nx, ny, offL, offR);
                    right.push_back({ cleanPts[i].x + offR.x, cleanPts[i].y + offR.y });
                }
            } else {
                // Turn is RIGHT:
                // Inside is Right: connects incoming inR to outgoing outR across the joint
                right.push_back({ cleanPts[i].x + inR.x,  cleanPts[i].y + inR.y });
                right.push_back({ cleanPts[i].x + outR.x, cleanPts[i].y + outR.y });

                // Outside is Left: sweeps arc from inL to outL
                for (int s = 0; s <= steps; ++s) {
                    double t = static_cast<double>(s) / steps;
                    double heading = aIn + turnSign * t * turnAngle;
                    double nx = -std::sin(heading);
                    double ny =  std::cos(heading);

                    Point2D offL, offR;
                    GetRibbonOffsets(capType, cleanPts[i].r, nx, ny, offL, offR);
                    left.push_back({ cleanPts[i].x + offL.x, cleanPts[i].y + offL.y });
                }
            }
        }
    }

    // Step 2: Extrude continuous closed ribbon
    path.move_to(left[0].x, left[0].y);

    for (size_t i = 1; i < left.size(); ++i) {
        path.line_to(left[i].x, left[i].y);
    }

    // Tail Cap (left -> right around the front)
    TraceCap(path, cleanPts.back(), left.back(), right.back(), capType, /*isTail=*/true);

    for (size_t i = right.size() - 1; i > 0; --i) {
        path.line_to(right[i - 1].x, right[i - 1].y);
    }

    // Head Cap (right -> left around the back)
    TraceCap(path, cleanPts.front(), left.front(), right.front(), capType, /*isTail=*/false);

    // Step 3: Seal closed polygon
    path.close();
    return path;
}
