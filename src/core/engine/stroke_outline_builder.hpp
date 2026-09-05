#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <blend2d/blend2d.h>
#include "core/engine/stroke_smoother.hpp"
#include "input/pen_palette.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class StrokeOutlineBuilder {
public:
    struct InputPoint {
        double x = 0.0;
        double y = 0.0;
        float width = 1.0f; // Total thickness in mm
    };

    /**
     * @brief Builds a unified, continuous 2D closed ribbon polygon from an ordered list of points.
     * 
     * Constructs a single closed boundary:
     * - Semicircular round start cap (cubic Bezier)
     * - Left side edge with smooth circular outer arcs / safely clamped inner bisectors
     * - Semicircular round end cap (cubic Bezier)
     * - Right side edge returning to start
     * 
     * Because this is a single closed contour, it has zero internal overlapping shapes,
     * zero winding cancellations, zero holes, and zero swallowtail gaps.
     */
    static BLPath BuildOutline(const std::vector<InputPoint>& rawPoints, CapType capType = CapType::Round) {
        BLPath path;
        if (rawPoints.empty()) return path;

        constexpr double STEP_MM = 0.5;
        thread_local std::vector<InputPoint> pts;
        ResampleArcLength(rawPoints, STEP_MM, pts);

        if (pts.empty()) return path;

        // Single point case: Draw an exact circular or square dot
        if (pts.size() == 1) {
            double r = std::max(0.05, (double)pts[0].width * 0.5);
            if (capType == CapType::Square) {
                path.add_rect(pts[0].x - r, pts[0].y - r, r * 2.0, r * 2.0);
            } else {
                path.add_circle(BLCircle{pts[0].x, pts[0].y, r});
            }
            return path;
        }

        const size_t N = pts.size();

        struct Vec2 {
            double x = 0.0;
            double y = 0.0;
        };

        thread_local std::vector<Vec2> tangents;
        thread_local std::vector<Vec2> segNormals;
        thread_local std::vector<double> segLens;
        tangents.resize(N - 1);
        segNormals.resize(N - 1);
        segLens.resize(N - 1);

        for (size_t i = 0; i < N - 1; ++i) {
            double dx = pts[i + 1].x - pts[i].x;
            double dy = pts[i + 1].y - pts[i].y;
            double len = std::hypot(dx, dy);
            segLens[i] = len;

            if (len > 1e-7) {
                tangents[i] = { dx / len, dy / len };
                segNormals[i] = { -tangents[i].y, tangents[i].x }; // 90 deg CCW (Left)
            } else {
                tangents[i] = { 1.0, 0.0 };
                segNormals[i] = { 0.0, 1.0 };
            }
        }

        // Precompute joints for interior vertices
        struct Joint {
            int turn = 0; // 0 = straight, 1 = left turn (left inner, right outer), -1 = right turn (right inner, left outer)
            BLPoint leftPt;
            BLPoint rightPt;
            // Outer arc parameters
            BLPoint arcStart;
            BLPoint arcEnd;
            double arcRadius = 0.0;
            bool arcCCW = false;
        };

        thread_local std::vector<Joint> joints;
        joints.resize(N);

        for (size_t i = 1; i < N - 1; ++i) {
            Joint& j = joints[i];
            double ri = std::max(0.05, (double)pts[i].width * 0.5);
            const auto& tPrev = tangents[i - 1];
            const auto& tNext = tangents[i];
            const auto& nPrev = segNormals[i - 1];
            const auto& nNext = segNormals[i];

            double cross = tPrev.x * tNext.y - tPrev.y * tNext.x;
            double dot = tPrev.x * tNext.x + tPrev.y * tNext.y;

            if (std::abs(cross) < 1e-4 || dot > 0.998) {
                // Nearly straight segment
                j.turn = 0;
                j.leftPt = BLPoint(pts[i].x + nNext.x * ri, pts[i].y + nNext.y * ri);
                j.rightPt = BLPoint(pts[i].x - nNext.x * ri, pts[i].y - nNext.y * ri);
            } else if (cross > 0.0) {
                // Turning LEFT: Left is INNER, Right is OUTER
                j.turn = 1;
                
                // Safe inner clamp: completely eliminates swallowtail loops & missing triangle holes
                double maxSafeOffset = std::min(segLens[i - 1], segLens[i]) * 0.45;
                double rSafe = std::min(ri, maxSafeOffset);

                double bx = nPrev.x + nNext.x;
                double by = nPrev.y + nNext.y;
                double blen = std::hypot(bx, by);
                if (blen > 1e-6) {
                    bx /= blen;
                    by /= blen;
                }
                j.leftPt = BLPoint(pts[i].x + bx * rSafe, pts[i].y + by * rSafe);

                // Right is outer: circular fillet from previous normal to next normal
                j.arcStart = BLPoint(pts[i].x - nPrev.x * ri, pts[i].y - nPrev.y * ri);
                j.arcEnd = BLPoint(pts[i].x - nNext.x * ri, pts[i].y - nNext.y * ri);
                j.arcRadius = ri;
                j.arcCCW = false; // Clockwise arc from arcStart to arcEnd
            } else {
                // Turning RIGHT: Right is INNER, Left is OUTER
                j.turn = -1;

                // Safe inner clamp
                double maxSafeOffset = std::min(segLens[i - 1], segLens[i]) * 0.45;
                double rSafe = std::min(ri, maxSafeOffset);

                double bx = nPrev.x + nNext.x;
                double by = nPrev.y + nNext.y;
                double blen = std::hypot(bx, by);
                if (blen > 1e-6) {
                    bx /= blen;
                    by /= blen;
                }
                j.rightPt = BLPoint(pts[i].x - bx * rSafe, pts[i].y - by * rSafe);

                // Left is outer: circular fillet from previous normal to next normal
                j.arcStart = BLPoint(pts[i].x + nPrev.x * ri, pts[i].y + nPrev.y * ri);
                j.arcEnd = BLPoint(pts[i].x + nNext.x * ri, pts[i].y + nNext.y * ri);
                j.arcRadius = ri;
                j.arcCCW = true; // Counter-clockwise arc from arcStart to arcEnd
            }
        }

        // Build single continuous closed contour
        double r0 = std::max(0.05, (double)pts[0].width * 0.5);
        BLPoint left0(pts[0].x + segNormals[0].x * r0, pts[0].y + segNormals[0].y * r0);
        BLPoint right0(pts[0].x - segNormals[0].x * r0, pts[0].y - segNormals[0].y * r0);

        path.move_to(left0.x, left0.y);

        // 1. Follow Left side forward
        for (size_t i = 1; i < N - 1; ++i) {
            const Joint& j = joints[i];
            if (j.turn == -1) {
                // Left is outer arc
                path.line_to(j.arcStart.x, j.arcStart.y);
                AppendArc(path, pts[i].x, pts[i].y, j.arcStart.x, j.arcStart.y, j.arcEnd.x, j.arcEnd.y, j.arcRadius, j.arcCCW);
            } else {
                path.line_to(j.leftPt.x, j.leftPt.y);
            }
        }

        size_t last = N - 1;
        double rLast = std::max(0.05, (double)pts[last].width * 0.5);
        BLPoint leftLast(pts[last].x + segNormals[last - 1].x * rLast, pts[last].y + segNormals[last - 1].y * rLast);
        BLPoint rightLast(pts[last].x - segNormals[last - 1].x * rLast, pts[last].y - segNormals[last - 1].y * rLast);

        path.line_to(leftLast.x, leftLast.y);

        // 2. End Cap: Semicircular round tip
        if (capType == CapType::Flat) {
            path.line_to(rightLast.x, rightLast.y);
        } else {
            AppendSemiCircleCap(path, pts[last].x, pts[last].y,
                                tangents[last - 1].x, tangents[last - 1].y,
                                rLast, leftLast, rightLast);
        }

        // 3. Follow Right side backward
        for (size_t i = N - 2; i >= 1; --i) {
            const Joint& j = joints[i];
            if (j.turn == 1) {
                // Right is outer arc (traveling backward from arcEnd to arcStart)
                path.line_to(j.arcEnd.x, j.arcEnd.y);
                AppendArc(path, pts[i].x, pts[i].y, j.arcEnd.x, j.arcEnd.y, j.arcStart.x, j.arcStart.y, j.arcRadius, !j.arcCCW);
            } else {
                path.line_to(j.rightPt.x, j.rightPt.y);
            }
        }

        path.line_to(right0.x, right0.y);

        // 4. Start Cap: Semicircular round tip connecting back to left0
        if (capType == CapType::Flat) {
            path.line_to(left0.x, left0.y);
        } else {
            AppendSemiCircleCap(path, pts[0].x, pts[0].y,
                                -tangents[0].x, -tangents[0].y,
                                r0, right0, left0);
        }

        path.close();
        return path;
    }

private:
    /**
     * @brief Appends a smooth circular cubic Bezier arc around (cx, cy) from (ax, ay) to (bx, by).
     */
    static void AppendArc(BLPath& path, double cx, double cy, double ax, double ay, double bx, double by, double r, bool ccw) {
        double vax = (ax - cx) / r;
        double vay = (ay - cy) / r;
        double vbx = (bx - cx) / r;
        double vby = (by - cy) / r;

        double dot = std::clamp(vax * vbx + vay * vby, -1.0, 1.0);
        double theta = std::acos(dot);
        if (theta < 1e-4) {
            path.line_to(bx, by);
            return;
        }

        double tax = ccw ? -vay : vay;
        double tay = ccw ? vax : -vax;
        double tbx = ccw ? -vby : vby;
        double tby = ccw ? vbx : -vbx;

        // Split large angles (> 100 deg) at midpoint for exceptional arc fidelity
        if (theta > 1.75) {
            double mx = (vax + vbx) * 0.5;
            double my = (vay + vby) * 0.5;
            double mlen = std::hypot(mx, my);
            if (mlen > 1e-6) {
                mx /= mlen;
                my /= mlen;
                double midX = cx + mx * r;
                double midY = cy + my * r;
                AppendArc(path, cx, cy, ax, ay, midX, midY, r, ccw);
                AppendArc(path, cx, cy, midX, midY, bx, by, r, ccw);
                return;
            }
        }

        double k = (4.0 / 3.0) * std::tan(theta * 0.25) * r;
        double cp1x = ax + tax * k;
        double cp1y = ay + tay * k;
        double cp2x = bx - tbx * k;
        double cp2y = by - tby * k;

        path.cubic_to(cp1x, cp1y, cp2x, cp2y, bx, by);
    }

    /**
     * @brief Appends a smooth semicircular cap in the forward direction.
     */
    static void AppendSemiCircleCap(BLPath& path, double cx, double cy,
                                    double forwardX, double forwardY, double r,
                                    const BLPoint& pStart, const BLPoint& pEnd) {
        double tipX = cx + forwardX * r;
        double tipY = cy + forwardY * r;
        double perpX = -forwardY;
        double perpY = forwardX;
        constexpr double k = 0.5522847498307935; // 4/3 * (sqrt(2) - 1)

        double cp1x = pStart.x + forwardX * (r * k);
        double cp1y = pStart.y + forwardY * (r * k);
        double cp2x = tipX + perpX * (r * k);
        double cp2y = tipY + perpY * (r * k);
        path.cubic_to(cp1x, cp1y, cp2x, cp2y, tipX, tipY);

        double cp3x = tipX - perpX * (r * k);
        double cp3y = tipY - perpY * (r * k);
        double cp4x = pEnd.x + forwardX * (r * k);
        double cp4y = pEnd.y + forwardY * (r * k);
        path.cubic_to(cp3x, cp3y, cp4x, cp4y, pEnd.x, pEnd.y);
    }

    /**
     * @brief Resamples a raw polyline at uniform arc-length intervals.
     *
     * Walks the raw point chain accumulating distance. Each time the accumulated
     * distance exceeds stepMm, a new point is emitted at the exact interpolated
     * position along the current segment, with width linearly blended between the
     * two bracketing raw samples.
     *
     * Always includes the first and last raw points so the stroke shape is preserved.
     *
     * @param raw   Raw input points from Google Ink Stroke Modeler
     * @param stepMm Desired arc-length spacing in mm (e.g. 0.35)
     * @return Evenly-spaced resampled points ready for ribbon geometry
     */
    static void ResampleArcLength(const std::vector<InputPoint>& raw, double stepMm, std::vector<InputPoint>& out) {
        out.clear();
        if (raw.empty()) return;
        if (raw.size() == 1) { out.push_back(raw[0]); return; }

        out.push_back(raw[0]);
        double accumulated = 0.0;

        for (size_t i = 1; i < raw.size(); ++i) {
            const InputPoint& a = raw[i - 1];
            const InputPoint& b = raw[i];

            double dx = b.x - a.x;
            double dy = b.y - a.y;
            double segLen = std::hypot(dx, dy);
            if (segLen < 1e-9) continue; // Duplicate — skip

            double remaining = segLen;
            double traveled  = 0.0;

            while (accumulated + remaining >= stepMm) {
                // How far along this segment do we need to travel to emit the next point?
                double advance = stepMm - accumulated;
                traveled += advance;
                remaining -= advance;
                accumulated = 0.0;

                double t = traveled / segLen;
                InputPoint p;
                p.x     = a.x + dx * t;
                p.y     = a.y + dy * t;
                p.width = a.width + (b.width - a.width) * static_cast<float>(t);
                out.push_back(p);
            }

            accumulated += remaining;
        }

        // Always include the final raw point to avoid a clipped stroke tail
        const InputPoint& last = raw.back();
        if (!out.empty()) {
            double ex = last.x - out.back().x;
            double ey = last.y - out.back().y;
            if (ex * ex + ey * ey > 1e-6) {
                out.push_back(last);
            }
        } else {
            out.push_back(last);
        }
    }
};