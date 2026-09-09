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
        float pressure = 1.0f;
        float speed = 0.0f;
        float tiltX = 0.0f;
        float tiltY = 0.0f;
    };

    /**
     * @brief Builds a 2D closed ribbon polygon or patterned outline (solid, dashed, dotted) from an ordered list of points.
     */
    static BLPath BuildOutline(const std::vector<InputPoint>& rawPoints, CapType capType = CapType::Round, StrokePattern pattern = StrokePattern::Solid) {
        BLPath path;
        if (rawPoints.empty()) return path;

        if (rawPoints.size() == 1) {
            double r = std::max(0.05, (double)rawPoints[0].width * 0.5);
            path.add_circle(BLCircle{rawPoints[0].x, rawPoints[0].y, r});
            return path;
        }

        if (pattern == StrokePattern::Dotted) {
            double nextDotDist = 0.0;
            double distAccum = 0.0;

            for (size_t i = 0; i < rawPoints.size() - 1; ++i) {
                const auto& p0 = rawPoints[i];
                const auto& p1 = rawPoints[i + 1];
                double dx = p1.x - p0.x;
                double dy = p1.y - p0.y;
                double segLen = std::hypot(dx, dy);
                if (segLen < 1e-7) continue;

                while (nextDotDist <= distAccum + segLen) {
                    double t = (nextDotDist - distAccum) / segLen;
                    t = std::clamp(t, 0.0, 1.0);
                    double dotX = p0.x + t * dx;
                    double dotY = p0.y + t * dy;
                    float dotW = p0.width + static_cast<float>(t) * (p1.width - p0.width);
                    double r = std::max(0.05, (double)dotW * 0.5);
                    path.add_circle(BLCircle{dotX, dotY, r});

                    double spacing = std::max(0.6, (double)dotW * 2.2);
                    nextDotDist += spacing;
                }
                distAccum += segLen;
            }
            if (path.is_empty() && !rawPoints.empty()) {
                double r = std::max(0.05, (double)rawPoints[0].width * 0.5);
                path.add_circle(BLCircle{rawPoints[0].x, rawPoints[0].y, r});
            }
            return path;
        }

        if (pattern == StrokePattern::Dashed) {
            const size_t N = rawPoints.size();
            std::vector<double> cumDist(N, 0.0);
            for (size_t i = 1; i < N; ++i) {
                cumDist[i] = cumDist[i - 1] + std::hypot(rawPoints[i].x - rawPoints[i - 1].x, rawPoints[i].y - rawPoints[i - 1].y);
            }
            double totalLen = cumDist.back();
            if (totalLen < 1e-5) {
                double r = std::max(0.05, (double)rawPoints[0].width * 0.5);
                path.add_circle(BLCircle{rawPoints[0].x, rawPoints[0].y, r});
                return path;
            }

            float avgWidth = 0.0f;
            for (const auto& pt : rawPoints) avgWidth += pt.width;
            avgWidth /= static_cast<float>(N);

            double dashLen = std::max(1.8, (double)avgWidth * 4.0);
            double gapLen  = std::max(1.0, (double)avgWidth * 2.5);
            double cycle   = dashLen + gapLen;

            auto samplePointAt = [&](double d) -> InputPoint {
                d = std::clamp(d, 0.0, totalLen);
                auto it = std::lower_bound(cumDist.begin(), cumDist.end(), d);
                if (it == cumDist.begin()) return rawPoints.front();
                if (it == cumDist.end()) return rawPoints.back();
                size_t idx = std::distance(cumDist.begin(), it);
                double d0 = cumDist[idx - 1];
                double d1 = cumDist[idx];
                double segL = d1 - d0;
                double t = (segL > 1e-7) ? (d - d0) / segL : 0.0;
                t = std::clamp(t, 0.0, 1.0);
                const auto& p0 = rawPoints[idx - 1];
                const auto& p1 = rawPoints[idx];
                InputPoint res;
                res.x = p0.x + t * (p1.x - p0.x);
                res.y = p0.y + t * (p1.y - p0.y);
                res.width = p0.width + static_cast<float>(t) * (p1.width - p0.width);
                res.pressure = p0.pressure + static_cast<float>(t) * (p1.pressure - p0.pressure);
                return res;
            };

            for (double s0 = 0.0; s0 < totalLen; s0 += cycle) {
                double s1 = std::min(totalLen, s0 + dashLen);
                if (s1 <= s0) break;

                std::vector<InputPoint> dashPts;
                dashPts.push_back(samplePointAt(s0));

                for (size_t i = 0; i < N; ++i) {
                    if (cumDist[i] > s0 + 1e-5 && cumDist[i] < s1 - 1e-5) {
                        dashPts.push_back(rawPoints[i]);
                    }
                }
                dashPts.push_back(samplePointAt(s1));

                if (dashPts.size() <= 1 || std::hypot(dashPts.front().x - dashPts.back().x, dashPts.front().y - dashPts.back().y) < 1e-5) {
                    double r = std::max(0.05, (double)dashPts[0].width * 0.5);
                    path.add_circle(BLCircle{dashPts[0].x, dashPts[0].y, r});
                } else {
                    BLPath subRibbon = BuildSolidRibbon(dashPts, capType);
                    path.add_path(subRibbon);
                }
            }
            return path;
        }

        return BuildSolidRibbon(rawPoints, capType);
    }

    /**
     * @brief Builds a continuous 2D closed ribbon polygon from an ordered list of points.
     */
    static BLPath BuildSolidRibbon(const std::vector<InputPoint>& pts, CapType capType = CapType::Round) {
        BLPath path;
        if (pts.empty()) return path;

        if (pts.size() == 1) {
            double r = std::max(0.05, (double)pts[0].width * 0.5);
            path.add_circle(BLCircle{pts[0].x, pts[0].y, r});
            return path;
        }

        const size_t N = pts.size();

        struct Vec2 { double x = 0; double y = 0; };
        std::vector<Vec2> left(N);
        std::vector<Vec2> right(N);

        // Basic Polygon Formation
        for (size_t i = 0; i < N; ++i) {
            double nx = 0.0, ny = 0.0;
            // Average normal from adjacent segments for smooth continuous ribbon
            if (i > 0) {
                double dx = pts[i].x - pts[i - 1].x;
                double dy = pts[i].y - pts[i - 1].y;
                double len = std::hypot(dx, dy);
                if (len > 1e-7) {
                    nx += -dy / len;
                    ny += dx / len;
                }
            }
            if (i < N - 1) {
                double dx = pts[i + 1].x - pts[i].x;
                double dy = pts[i + 1].y - pts[i].y;
                double len = std::hypot(dx, dy);
                if (len > 1e-7) {
                    nx += -dy / len;
                    ny += dx / len;
                }
            }
            double nLen = std::hypot(nx, ny);
            if (nLen > 1e-7) {
                nx /= nLen;
                ny /= nLen;
            } else {
                nx = 1.0;
                ny = 0.0;
            }

            double r = std::max(0.05, (double)pts[i].width * 0.5);
            left[i]  = { pts[i].x + nx * r, pts[i].y + ny * r };
            right[i] = { pts[i].x - nx * r, pts[i].y - ny * r };
        }

        path.move_to(left[0].x, left[0].y);

        // 1. Follow Left side forward
        for (size_t i = 1; i < N; ++i) {
            path.line_to(left[i].x, left[i].y);
        }

        // 2. End Cap: Simple flat line
        path.line_to(right[N - 1].x, right[N - 1].y);

        // 3. Follow Right side backward
        for (size_t i = N - 1; i > 0; --i) {
            path.line_to(right[i - 1].x, right[i - 1].y);
        }

        // 4. Start Cap: Simple flat line connecting back
        path.line_to(left[0].x, left[0].y);

        path.close();
        return path;
    }

private:
    /*
     * OLD LOGIC:
     * static void AppendSemiCircleCap(...)
     * - Generated complex cubic bezier semicircles for round start/end caps.
     * 
     * static void AppendArc(...)
     * - Generated circular fillets for outer joints on sharp turns.
     * 
     * static void ResampleArcLength(...)
     * - Linearly interpolated points based on arc-length distance.
     */
};