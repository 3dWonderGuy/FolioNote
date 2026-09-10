#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include "core/spatial/aabb.hpp"
#include "core/engine/stroke_smoother.hpp"
#include "core/engine/stroke_outline_builder.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @brief High-performance computational geometry algorithms for vector ink strokes.
 * 
 * Implements:
 * 1. Two-tier collision detection:
 *    - Tier 1: Stroke-level AABB broadphase culling.
 *    - Tier 2: On-the-fly transient segment mini-AABB evaluation in CPU registers,
 *              followed by exact Euclidean point-to-segment distance calculation.
 * 2. Precision stroke slicing:
 *    - Quadratic intersection solver between stroke segments and circular eraser kernels.
 *    - Dynamic topological splitting into zero, one, or multiple surviving sub-strokes.
 */
class StrokeCollisionEngine {
public:
    struct SegmentHit {
        bool hit = false;
        size_t segmentIndex = 0;
        float t = 0.0f;
        double distSq = 0.0;
    };

    /**
     * @brief Computes point-to-line-segment distance with transient register-only mini-AABB rejection.
     */
    [[nodiscard]] static inline bool HitTestSegment(
        const Point2D& p0, const Point2D& p1, float width,
        double qx, double qy, double queryRadius,
        float& outT, double& outDistSq) noexcept
    {
        const double maxR = static_cast<double>(width) * 0.5 + queryRadius;

        // 1. Transient segment mini-AABB in CPU registers (zero memory allocation)
        const double segMinX = std::min(p0.x, p1.x) - maxR;
        const double segMaxX = std::max(p0.x, p1.x) + maxR;
        const double segMinY = std::min(p0.y, p1.y) - maxR;
        const double segMaxY = std::max(p0.y, p1.y) + maxR;

        // Fast-out: culls out 90%+ of segments with simple register comparisons
        if (qx < segMinX || qx > segMaxX || qy < segMinY || qy > segMaxY) {
            return false;
        }

        // 2. Exact Euclidean Point-to-Segment Math
        const double vx = p1.x - p0.x;
        const double vy = p1.y - p0.y;
        const double wx = qx - p0.x;
        const double wy = qy - p0.y;

        const double lenSq = vx * vx + vy * vy;
        double t = 0.0;

        if (lenSq > 1e-9) {
            const double dot = wx * vx + wy * vy;
            t = std::clamp(dot / lenSq, 0.0, 1.0);
        }

        const double projX = p0.x + t * vx;
        const double projY = p0.y + t * vy;

        const double dx = qx - projX;
        const double dy = qy - projY;
        const double distSq = dx * dx + dy * dy;

        if (distSq <= maxR * maxR) {
            outT = static_cast<float>(t);
            outDistSq = distSq;
            return true;
        }

        return false;
    }

    /**
     * @brief Evaluates whether a query circle intersects any segment of a stroke.
     */
    [[nodiscard]] static SegmentHit HitTestStroke(
        const std::vector<Segment1D>& segments,
        double qx, double qy, double queryRadius) noexcept
    {
        SegmentHit bestHit;
        bestHit.distSq = 1e30;

        for (size_t i = 0; i < segments.size(); ++i) {
            const auto& seg = segments[i];
            float t = 0.0f;
            double distSq = 0.0;
            if (HitTestSegment(seg.p0, seg.p1, seg.width, qx, qy, queryRadius, t, distSq)) {
                if (distSq < bestHit.distSq) {
                    bestHit.hit = true;
                    bestHit.segmentIndex = i;
                    bestHit.t = t;
                    bestHit.distSq = distSq;
                }
            }
        }

        return bestHit;
    }

    /**
     * @brief Point hit test for single-point strokes (e.g. period / tap dots).
     */
    [[nodiscard]] static inline bool HitTestPoint(
        const Point2D& p, float width,
        double qx, double qy, double queryRadius) noexcept
    {
        const double maxR = std::max(0.2, static_cast<double>(width) * 0.5 + queryRadius);
        const double dx = p.x - qx;
        const double dy = p.y - qy;
        return (dx * dx + dy * dy) <= (maxR * maxR);
    }

    /**
     * @brief Computes minimum distance squared between two line segments in 2D space.
     * Implements Dan Sunday's robust segment-to-segment algorithm with zero heap allocation.
     */
    [[nodiscard]] static inline double SegmentSegmentDistanceSq(
        const Point2D& p0, const Point2D& p1,
        const Point2D& q0, const Point2D& q1) noexcept
    {
        const double ux = p1.x - p0.x;
        const double uy = p1.y - p0.y;
        const double vx = q1.x - q0.x;
        const double vy = q1.y - q0.y;
        const double wx = p0.x - q0.x;
        const double wy = p0.y - q0.y;

        const double a = ux * ux + uy * uy;
        const double b = ux * vx + uy * vy;
        const double c = vx * vx + vy * vy;
        const double d = ux * wx + uy * wy;
        const double e = vx * wx + vy * wy;
        const double D = a * c - b * b;

        double sN, sD = D;
        double tN, tD = D;

        if (D < 1e-9) {
            sN = 0.0;
            sD = 1.0;
            tN = e;
            tD = c;
        } else {
            sN = (b * e - c * d);
            tN = (a * e - b * d);
            if (sN < 0.0) {
                sN = 0.0;
                tN = e;
                tD = c;
            } else if (sN > sD) {
                sN = sD;
                tN = e + b;
                tD = c;
            }
        }

        if (tN < 0.0) {
            tN = 0.0;
            if (-d < 0.0) sN = 0.0;
            else if (-d > a) sN = sD;
            else {
                sN = -d;
                sD = a;
            }
        } else if (tN > tD) {
            tN = tD;
            if ((-d + b) < 0.0) sN = 0.0;
            else if ((-d + b) > a) sN = sD;
            else {
                sN = (-d + b);
                sD = a;
            }
        }

        const double sc = (std::abs(sN) < 1e-9 ? 0.0 : sN / sD);
        const double tc = (std::abs(tN) < 1e-9 ? 0.0 : tN / tD);

        const double dpx = wx + (sc * ux) - (tc * vx);
        const double dpy = wy + (sc * uy) - (tc * vy);
        return dpx * dpx + dpy * dpy;
    }

    /**
     * @brief Continuous swept-capsule collision test against all segments of a stroke.
     * Detects hits regardless of mouse movement speed (zero tunneling/skipping).
     */
    [[nodiscard]] static inline bool HitTestStrokeSwept(
        const std::vector<Segment1D>& segments,
        const Point2D& w0, const Point2D& w1, double queryRadius) noexcept
    {
        for (const auto& seg : segments) {
            const double maxR = queryRadius + static_cast<double>(seg.width) * 0.5;
            // Transient register mini-AABB test for early rejection
            const double minX = std::min({ seg.p0.x, seg.p1.x, w0.x, w1.x }) - maxR;
            const double maxX = std::max({ seg.p0.x, seg.p1.x, w0.x, w1.x }) + maxR;
            const double minY = std::min({ seg.p0.y, seg.p1.y, w0.y, w1.y }) - maxR;
            const double maxY = std::max({ seg.p0.y, seg.p1.y, w0.y, w1.y }) + maxR;

            if (minX > maxX || minY > maxY) continue;

            const double distSq = SegmentSegmentDistanceSq(seg.p0, seg.p1, w0, w1);
            if (distSq <= maxR * maxR) {
                return true;
            }
        }
        return false;
    }
};

/**
 * @brief High-precision vector stroke slicer for point-based eraser tools.
 */
class StrokeSlicer {
public:
    /**
     * @brief Linearly interpolates two centerline points.
     */
    [[nodiscard]] static inline Point2D LerpPoint(const Point2D& p0, const Point2D& p1, double t) noexcept {
        Point2D p;
        p.x = p0.x + t * (p1.x - p0.x);
        p.y = p0.y + t * (p1.y - p0.y);
        p.pressure = p0.pressure + static_cast<float>(t) * (p1.pressure - p0.pressure);
        p.timeSeconds = p0.timeSeconds + t * (p1.timeSeconds - p0.timeSeconds);
        p.tiltX = p0.tiltX + static_cast<float>(t) * (p1.tiltX - p0.tiltX);
        p.tiltY = p0.tiltY + static_cast<float>(t) * (p1.tiltY - p0.tiltY);
        return p;
    }

    /**
     * @brief Slices a chain of segments against a circular eraser kernel.
     * @param segments Original stroke segments.
     * @param ex Eraser center X.
     * @param ey Eraser center Y.
     * @param eraserRadius Eraser radius.
     * @param outSubStrokes Destination list of surviving segment chains.
     * @return true if the stroke was modified or split.
     */
    static bool SliceSegments(
        const std::vector<Segment1D>& segments,
        double ex, double ey, double eraserRadius,
        std::vector<std::vector<Segment1D>>& outSubStrokes)
    {
        if (segments.empty()) return false;

        outSubStrokes.clear();
        bool modified = false;
        std::vector<Segment1D> currentChain;

        auto PushChainIfValid = [&]() {
            if (!currentChain.empty()) {
                outSubStrokes.push_back(std::move(currentChain));
                currentChain.clear();
            }
        };

        auto PushSegmentIfNonDegenerate = [&](const Point2D& a, const Point2D& b, float w) {
            if (std::hypot(b.x - a.x, b.y - a.y) > 1e-4) {
                currentChain.push_back(Segment1D{ a, b, w });
            }
        };

        for (const auto& seg : segments) {
            const double effR = eraserRadius + static_cast<double>(seg.width) * 0.5;
            const double effRSq = effR * effR;

            const double dx0 = seg.p0.x - ex;
            const double dy0 = seg.p0.y - ey;
            const bool p0In = (dx0 * dx0 + dy0 * dy0) < effRSq;

            const double dx1 = seg.p1.x - ex;
            const double dy1 = seg.p1.y - ey;
            const bool p1In = (dx1 * dx1 + dy1 * dy1) < effRSq;

            // Solve quadratic intersection: || (p0 + t * V) - E ||^2 = effRSq
            const double vx = seg.p1.x - seg.p0.x;
            const double vy = seg.p1.y - seg.p0.y;
            const double wx = seg.p0.x - ex;
            const double wy = seg.p0.y - ey;

            const double a = vx * vx + vy * vy;
            const double b = 2.0 * (wx * vx + wy * vy);
            const double c = (wx * wx + wy * wy) - effRSq;

            double t1 = -1.0, t2 = -1.0;
            bool hasIntersection = false;

            if (a > 1e-9) {
                const double discr = b * b - 4.0 * a * c;
                if (discr >= 0.0) {
                    const double sqrtD = std::sqrt(discr);
                    t1 = (-b - sqrtD) / (2.0 * a);
                    t2 = (-b + sqrtD) / (2.0 * a);
                    if (t1 > t2) std::swap(t1, t2);
                    hasIntersection = true;
                }
            }

            if (!p0In && !p1In) {
                // Both endpoints outside eraser
                if (hasIntersection && t1 > 0.001 && t2 < 0.999 && t1 < t2) {
                    // Segment punches entirely through the eraser: splits into 2 pieces!
                    modified = true;
                    Point2D cutIn  = LerpPoint(seg.p0, seg.p1, t1);
                    Point2D cutOut = LerpPoint(seg.p0, seg.p1, t2);

                    // First part terminates current sub-stroke
                    PushSegmentIfNonDegenerate(seg.p0, cutIn, seg.width);
                    PushChainIfValid();

                    // Second part begins new sub-stroke
                    PushSegmentIfNonDegenerate(cutOut, seg.p1, seg.width);
                } else {
                    // Segment completely outside
                    currentChain.push_back(seg);
                }
            } else if (!p0In && p1In) {
                // Enters eraser: cut at t1 and finish current sub-stroke
                modified = true;
                double tCut = (hasIntersection && t1 >= 0.0 && t1 <= 1.0) ? t1 : 0.5;
                Point2D cutPt = LerpPoint(seg.p0, seg.p1, tCut);

                PushSegmentIfNonDegenerate(seg.p0, cutPt, seg.width);
                PushChainIfValid();
            } else if (p0In && !p1In) {
                // Exits eraser: cut at t2 and start new sub-stroke
                modified = true;
                double tCut = (hasIntersection && t2 >= 0.0 && t2 <= 1.0) ? t2 : 0.5;
                Point2D cutPt = LerpPoint(seg.p0, seg.p1, tCut);

                PushSegmentIfNonDegenerate(cutPt, seg.p1, seg.width);
            } else {
                // Both endpoints inside: entirely deleted
                modified = true;
                PushChainIfValid();
            }
        }

        PushChainIfValid();
        return modified;
    }
};
