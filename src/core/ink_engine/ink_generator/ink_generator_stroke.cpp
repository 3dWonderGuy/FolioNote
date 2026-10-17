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
// Sub-tracer for smooth rounded endcap arcs
static void TraceRoundCap(BLPath& path,
                          const StrokePoint& center,
                          const Point2D& startPt,
                          const Point2D& endPt,
                          double ux, double uy) {

    double rStart = std::hypot(startPt.x - center.x, startPt.y - center.y);
    double rEnd   = std::hypot(endPt.x - center.x, endPt.y - center.y);
    if (rStart < 1e-6 && rEnd < 1e-6) {
        path.line_to(endPt.x, endPt.y);
        return;
    }

    double aStart = std::atan2(startPt.y - center.y, startPt.x - center.x);
    double aEnd   = std::atan2(endPt.y - center.y, endPt.x - center.x);

    // Compute clockwise angular distance from aStart to aEnd in (0, 2*PI]
    double sweepCW = aStart - aEnd;
    while (sweepCW <= 0.0) sweepCW += 2.0 * M_PI;
    while (sweepCW > 2.0 * M_PI) sweepCW -= 2.0 * M_PI;

    // Check if clockwise arc midpoint bows in outward direction (ux, uy)
    double midCW = aStart - 0.5 * sweepCW;
    double dotCW = std::cos(midCW) * ux + std::sin(midCW) * uy;

    double sweep = (dotCW >= 0.0) ? -sweepCW : (2.0 * M_PI - sweepCW);
    double absSweep = std::abs(sweep);

    if (absSweep < 1e-4) {
        path.line_to(endPt.x, endPt.y);
        return;
    }

    const int arcSteps = std::clamp(static_cast<int>(std::ceil(12.0 * (absSweep / M_PI))), 2, 12);
    for (int i = 1; i < arcSteps; ++i) {
        double t = static_cast<double>(i) / arcSteps;
        double a = aStart + t * sweep;
        double r = rStart + t * (rEnd - rStart);
        path.line_to(center.x + std::cos(a) * r, center.y + std::sin(a) * r);
    }

    // Seal arc to the exact destination point
    path.line_to(endPt.x, endPt.y);
}

// Sub-tracer for oriented polygon endcap facets (Square & Chisel)
static void TracePolygonCap(BLPath& path,
                            const StrokePoint& center,
                            const Point2D& startPt,
                            const Point2D& endPt,
                            double ux, double uy,
                            CapType capType) {
                                
    auto verts = GetNibVertices(capType, center.r);
    if (verts.empty()) {
        path.line_to(endPt.x, endPt.y);
        return;
    }

    Point2D startRel = { startPt.x - center.x, startPt.y - center.y };
    Point2D endRel   = { endPt.x - center.x,   endPt.y - center.y };

    int startIdx = 0, endIdx = 0;
    double minDStart = 1e9, minDEnd = 1e9;
    for (int i = 0; i < static_cast<int>(verts.size()); ++i) {
        double ds = std::hypot(verts[i].x - startRel.x, verts[i].y - startRel.y);
        double de = std::hypot(verts[i].x - endRel.x,   verts[i].y - endRel.y);
        if (ds < minDStart) { minDStart = ds; startIdx = i; }
        if (de < minDEnd)   { minDEnd = de;   endIdx = i; }
    }

    if (startIdx == endIdx) {
        path.line_to(endPt.x, endPt.y);
        return;
    }

    const int K = static_cast<int>(verts.size());

    // Path CCW: startIdx -> endIdx
    std::vector<int> pathCCW;
    for (int idx = (startIdx + 1) % K; idx != endIdx; idx = (idx + 1) % K) {
        pathCCW.push_back(idx);
    }

    // Path CW: startIdx -> endIdx
    std::vector<int> pathCW;
    for (int idx = (startIdx - 1 + K) % K; idx != endIdx; idx = (idx - 1 + K) % K) {
        pathCW.push_back(idx);
    }

    // Evaluate which path faces outward along (ux, uy)
    auto evalProj = [&](const std::vector<int>& p) -> double {
        if (p.empty()) return -1e9;
        double sum = 0.0;
        for (int idx : p) {
            sum += (verts[idx].x - startRel.x) * ux + (verts[idx].y - startRel.y) * uy;
        }
        return sum / static_cast<double>(p.size());
    };

    double projCCW = evalProj(pathCCW);
    double projCW  = evalProj(pathCW);

    const auto& chosenPath = (projCCW >= projCW) ? pathCCW : pathCW;
    for (int idx : chosenPath) {
        path.line_to(center.x + verts[idx].x, center.y + verts[idx].y);
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
        TracePolygonCap(path, center, startPt, endPt, ux, uy, capType);
    } else {
        TraceRoundCap(path, center, startPt, endPt, ux, uy);
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

    // 2. Precompute segment unit tangents and normals
    struct SegmentInfo {
        double len = 0.0;
        double ux = 1.0;
        double uy = 0.0;
        double nx = 0.0;
        double ny = 1.0;
    };

    std::vector<SegmentInfo> segs(N - 1);
    for (size_t i = 0; i < N - 1; ++i) {
        double dx = cleanPts[i + 1].x - cleanPts[i].x;
        double dy = cleanPts[i + 1].y - cleanPts[i].y;
        double len = std::hypot(dx, dy);
        segs[i].len = len;
        if (len > 1e-7) {
            segs[i].ux = dx / len;
            segs[i].uy = dy / len;
            segs[i].nx = -segs[i].uy;
            segs[i].ny =  segs[i].ux;
        } else {
            if (i > 0) {
                segs[i] = segs[i - 1];
            } else {
                segs[i].ux = 1.0;
                segs[i].uy = 0.0;
                segs[i].nx = 0.0;
                segs[i].ny = 1.0;
            }
        }
    }

    std::vector<Point2D> leftPts;
    std::vector<Point2D> rightPts;
    leftPts.reserve(N * 2);
    rightPts.reserve(N * 2);

    // 3. Start point P0
    {
        Point2D offL0, offR0;
        GetRibbonOffsets(capType, std::max(0.0f, cleanPts[0].r), segs[0].nx, segs[0].ny, offL0, offR0);
        leftPts.push_back({ cleanPts[0].x + offL0.x, cleanPts[0].y + offL0.y });
        rightPts.push_back({ cleanPts[0].x + offR0.x, cleanPts[0].y + offR0.y });
    }

    // 4. Interior vertices Pi (i = 1 .. N - 2): Exact closed-form angle bisector for inner corner & outer miter/arc
    for (size_t i = 1; i < N - 1; ++i) {
        const auto& sIn = segs[i - 1];
        const auto& sOut = segs[i];
        const auto& Pi = cleanPts[i];
        double r = std::max(0.0f, Pi.r);

        Point2D offL_in, offR_in, offL_out, offR_out;
        GetRibbonOffsets(capType, r, sIn.nx, sIn.ny, offL_in, offR_in);
        GetRibbonOffsets(capType, r, sOut.nx, sOut.ny, offL_out, offR_out);

        double cross = sIn.ux * sOut.uy - sIn.uy * sOut.ux;
        double dot   = sIn.ux * sOut.ux + sIn.uy * sOut.uy;
        double turnAngle = std::atan2(std::abs(cross), dot);

        if (std::abs(cross) < 1e-4 && dot > 0.0) {
            // Collinear: smooth continuous offset
            leftPts.push_back({ Pi.x + offL_in.x, Pi.y + offL_in.y });
            rightPts.push_back({ Pi.x + offR_in.x, Pi.y + offR_in.y });
            continue;
        }

        // Polygonal nibs (Square/Highlighter, Chisel/Calligraphy):
        // Ribbon edges MUST attach directly to the nib's extreme corners.
        // When adjacent segments share the same corner (dL < 1e-4), pass directly through Pi + corner.
        // When the corner changes: bevel the outer elbow, join the inner corner at facet midpoint.
        if (capType == CapType::Square || capType == CapType::Chisel) {
            double dL = std::hypot(offL_in.x - offL_out.x, offL_in.y - offL_out.y);
            if (dL < 1e-4) {
                leftPts.push_back({ Pi.x + offL_in.x, Pi.y + offL_in.y });
            } else if (cross > 0.0) {
                // Left is inner
                leftPts.push_back({ Pi.x + (offL_in.x + offL_out.x) * 0.5,
                                    Pi.y + (offL_in.y + offL_out.y) * 0.5 });
            } else {
                // Left is outer
                leftPts.push_back({ Pi.x + offL_in.x, Pi.y + offL_in.y });
                leftPts.push_back({ Pi.x + offL_out.x, Pi.y + offL_out.y });
            }

            double dR = std::hypot(offR_in.x - offR_out.x, offR_in.y - offR_out.y);
            if (dR < 1e-4) {
                rightPts.push_back({ Pi.x + offR_in.x, Pi.y + offR_in.y });
            } else if (cross < 0.0) {
                // Right is inner
                rightPts.push_back({ Pi.x + (offR_in.x + offR_out.x) * 0.5,
                                     Pi.y + (offR_in.y + offR_out.y) * 0.5 });
            } else {
                // Right is outer
                rightPts.push_back({ Pi.x + offR_in.x, Pi.y + offR_in.y });
                rightPts.push_back({ Pi.x + offR_out.x, Pi.y + offR_out.y });
            }
            continue;
        }

        // Determine inner vs outer offsets based on turn direction:
        // cross > 0: Left turn (CCW) -> Left is inner, Right is outer
        // cross < 0: Right turn (CW) -> Right is inner, Left is outer
        const Point2D& offIn_inner  = (cross > 0.0) ? offL_in  : offR_in;
        const Point2D& offOut_inner = (cross > 0.0) ? offL_out : offR_out;
        const Point2D& offIn_outer  = (cross > 0.0) ? offR_in  : offL_in;
        const Point2D& offOut_outer = (cross > 0.0) ? offR_out : offL_out;

        // 1. INNER CORNER: Closed-form bisector intersection (eliminates notches, spikes, and division bugs)
        Point2D innerCorner;
        double sumInX = offIn_inner.x + offOut_inner.x;
        double sumInY = offIn_inner.y + offOut_inner.y;
        double bInLen = std::hypot(sumInX, sumInY);
        if (bInLen > 1e-6) {
            double bInX = sumInX / bInLen;
            double bInY = sumInY / bInLen;
            double cosHalf = std::sqrt(std::max(0.0, (1.0 + dot) * 0.5));
            double miterRatio = (cosHalf > 1e-4) ? (1.0 / cosHalf) : 2.0;
            double maxAllowedMiter = (0.85 * std::min(sIn.len, sOut.len)) / std::max(0.01, r);
            double clampedMiter = std::clamp(miterRatio, 1.0, std::max(1.0, std::min(1.5, maxAllowedMiter)));
            innerCorner = { Pi.x + bInX * (r * clampedMiter), Pi.y + bInY * (r * clampedMiter) };
        } else {
            innerCorner = { Pi.x + (offIn_inner.x + offOut_inner.x) * 0.5,
                            Pi.y + (offIn_inner.y + offOut_inner.y) * 0.5 };
        }

        // 2. OUTER CORNER: Miter bisector for gentle curves (<= 45 deg) or smooth arc/bevel for sharper elbows
        auto emitOuter = [&](std::vector<Point2D>& outPts) {
            if (turnAngle <= M_PI * 0.25) {
                // Gentle turn: single miter corner along outer bisector
                double sumOutX = offIn_outer.x + offOut_outer.x;
                double sumOutY = offIn_outer.y + offOut_outer.y;
                double bOutLen = std::hypot(sumOutX, sumOutY);
                if (bOutLen > 1e-6) {
                    double bOutX = sumOutX / bOutLen;
                    double bOutY = sumOutY / bOutLen;
                    double cosHalf = std::sqrt(std::max(0.0, (1.0 + dot) * 0.5));
                    double miterRatio = std::min(1.414, 1.0 / std::max(0.707, cosHalf));
                    outPts.push_back({ Pi.x + bOutX * (r * miterRatio), Pi.y + bOutY * (r * miterRatio) });
                } else {
                    outPts.push_back({ Pi.x + offIn_outer.x, Pi.y + offIn_outer.y });
                    outPts.push_back({ Pi.x + offOut_outer.x, Pi.y + offOut_outer.y });
                }
            } else if (capType == CapType::Round) {
                // Sharper turn: round arc around outer elbow
                int steps = std::clamp(static_cast<int>(std::ceil(turnAngle / (M_PI / 6.0))), 2, 5);
                double aStart = std::atan2(offIn_outer.y, offIn_outer.x);
                double aEnd   = std::atan2(offOut_outer.y, offOut_outer.x);
                double aDiff  = aEnd - aStart;
                while (aDiff < -M_PI) aDiff += 2.0 * M_PI;
                while (aDiff >  M_PI) aDiff -= 2.0 * M_PI;
                for (int s = 0; s <= steps; ++s) {
                    double t = static_cast<double>(s) / steps;
                    double a = aStart + t * aDiff;
                    outPts.push_back({ Pi.x + std::cos(a) * r, Pi.y + std::sin(a) * r });
                }
            } else {
                // Bevel join for square/chisel nibs
                outPts.push_back({ Pi.x + offIn_outer.x, Pi.y + offIn_outer.y });
                outPts.push_back({ Pi.x + offOut_outer.x, Pi.y + offOut_outer.y });
            }
        };

        if (cross > 0.0) {
            leftPts.push_back(innerCorner);
            emitOuter(rightPts);
        } else {
            emitOuter(leftPts);
            rightPts.push_back(innerCorner);
        }
    }

    // 5. End point P_{N-1}
    {
        const auto& lastSeg = segs.back();
        Point2D offL_end, offR_end;
        GetRibbonOffsets(capType, std::max(0.0f, cleanPts.back().r), lastSeg.nx, lastSeg.ny, offL_end, offR_end);
        leftPts.push_back({ cleanPts.back().x + offL_end.x, cleanPts.back().y + offL_end.y });
        rightPts.push_back({ cleanPts.back().x + offR_end.x, cleanPts.back().y + offR_end.y });
    }

    // 6. Extrude single continuous closed contour with head/tail endcaps
    path.move_to(leftPts[0].x, leftPts[0].y);

    for (size_t i = 1; i < leftPts.size(); ++i) {
        path.line_to(leftPts[i].x, leftPts[i].y);
    }

    // Tail Cap: leftPts.back() -> rightPts.back() around cleanPts.back()
    TraceCap(path, cleanPts.back(), leftPts.back(), rightPts.back(), capType, /*isTail=*/true);

    for (size_t i = rightPts.size(); i > 0; --i) {
        path.line_to(rightPts[i - 1].x, rightPts[i - 1].y);
    }

    // Head Cap: rightPts[0] -> leftPts[0] around cleanPts.front()
    TraceCap(path, cleanPts.front(), leftPts[0], rightPts[0], capType, /*isTail=*/false);

    path.close();
    return path;
}

static StrokePoint ResolveEnvelopePoint(const StrokePoint& pt, StrokePattern pattern) noexcept {
    if (pattern != StrokePattern::TexturedPencil) {
        return pt;
    }
    float tiltMag = std::hypot(pt.tiltX, pt.tiltY);
    double ux = (tiltMag > 1e-4f) ? (pt.tiltX / tiltMag) : 0.0;
    double uy = (tiltMag > 1e-4f) ? (pt.tiltY / tiltMag) : 0.0;

    float tiltFactor = 1.0f;
    if (tiltMag > 0.22f) {
        float t = std::clamp((tiltMag - 0.22f) / (1.05f - 0.22f), 0.0f, 1.0f);
        float smoothT = t * t * (3.0f - 2.0f * t);
        tiltFactor = 1.0f + smoothT * 1.8f;
    }
    float rTip = pt.r / tiltFactor;
    float rTiltExtra = std::max(0.0f, pt.r - rTip);
    return StrokePoint(pt.x + ux * rTiltExtra, pt.y + uy * rTiltExtra, pt.r, pt.pressure, pt.tiltX, pt.tiltY);
}

void InkGenerator::AppendRibbonSegments(BLPath& path,
                                        const std::vector<StrokePoint>& pts,
                                        size_t startIdx,
                                        CapType capType,
                                        StrokePattern pattern) {
    if (pts.empty() || startIdx >= pts.size()) return;

    const CapType effectiveCap = (pattern == StrokePattern::TexturedPencil) ? CapType::Round : capType;

    auto emitCap = [&](const StrokePoint& p) {
        if (p.r <= 0.002f) return;
        if (effectiveCap == CapType::Round) {
            path.add_circle(BLCircle(p.x, p.y, p.r), BL_GEOMETRY_DIRECTION_CW);
        } else {
            auto nibVerts = GetNibVertices(effectiveCap, p.r);
            if (!nibVerts.empty()) {
                path.move_to(p.x + nibVerts[0].x, p.y + nibVerts[0].y);
                for (size_t k = 1; k < nibVerts.size(); ++k) {
                    path.line_to(p.x + nibVerts[k].x, p.y + nibVerts[k].y);
                }
                path.close();
            }
        }
    };

    if (startIdx == 0) {
        emitCap(ResolveEnvelopePoint(pts[0], pattern));
        startIdx = 1;
    }

    for (size_t i = startIdx; i < pts.size(); ++i) {
        StrokePoint p0 = ResolveEnvelopePoint(pts[i - 1], pattern);
        StrokePoint p1 = ResolveEnvelopePoint(pts[i],     pattern);

        emitCap(p1);

        if (p0.r <= 0.002f && p1.r <= 0.002f) continue;
        double dx = p1.x - p0.x;
        double dy = p1.y - p0.y;
        double len = std::hypot(dx, dy);
        if (len < 1e-6) continue;

        double nx = -dy / len;
        double ny =  dx / len;

        Point2D offL0, offR0, offL1, offR1;
        GetRibbonOffsets(effectiveCap, std::max(0.0f, p0.r), nx, ny, offL0, offR0);
        GetRibbonOffsets(effectiveCap, std::max(0.0f, p1.r), nx, ny, offL1, offR1);

        path.move_to(p0.x + offL0.x, p0.y + offL0.y);
        path.line_to(p0.x + offR0.x, p0.y + offR0.y);
        path.line_to(p1.x + offR1.x, p1.y + offR1.y);
        path.line_to(p1.x + offL1.x, p1.y + offL1.y);
        path.close();
    }
}


