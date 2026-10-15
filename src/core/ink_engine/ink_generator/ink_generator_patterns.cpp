#include "core/ink_engine/ink_generator.hpp"

#include <vector>
#include <cmath>
#include <algorithm>

// =============================================================================
// DISCONTINUOUS PATTERN DISPATCHER
// =============================================================================

BLPath InkGenerator::ApplyDiscontinuousPattern(const std::vector<Point2D>& pts,
                                               CapType capType,
                                               StrokePattern pattern) {
    switch (pattern) {
        case StrokePattern::Dotted:
            return BuildDottedPattern(pts);
        case StrokePattern::Dashed:
            return BuildDashedPattern(pts, capType);
        case StrokePattern::DashDot:
            return BuildDashDotPattern(pts, capType);
        case StrokePattern::Solid:
        default:
            return BuildSolidRibbon(pts, capType);
    }
}

// =============================================================================
// 1. DOTTED PATTERN
// =============================================================================

BLPath InkGenerator::BuildDottedPattern(const std::vector<Point2D>& pts) {
    BLPath path;
    double nextDotDist = 0.0;
    double distAccum = 0.0;

    for (size_t i = 0; i < pts.size() - 1; ++i) {
        const auto& p0 = pts[i];
        const auto& p1 = pts[i + 1];
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
    if (path.is_empty() && !pts.empty()) {
        double r = std::max(0.05, (double)pts[0].width * 0.5);
        path.add_circle(BLCircle{pts[0].x, pts[0].y, r});
    }
    return path;
}

// =============================================================================
// 2. DASHED PATTERN
// =============================================================================

BLPath InkGenerator::BuildDashedPattern(const std::vector<Point2D>& pts, CapType capType) {
    BLPath path;
    const size_t N = pts.size();
    std::vector<double> cumDist(N, 0.0);
    for (size_t i = 1; i < N; ++i) {
        cumDist[i] = cumDist[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    }
    double totalLen = cumDist.back();
    if (totalLen < 1e-5) {
        double r = std::max(0.05, (double)pts[0].width * 0.5);
        path.add_circle(BLCircle{pts[0].x, pts[0].y, r});
        return path;
    }

    float avgWidth = 0.0f;
    for (const auto& pt : pts) avgWidth += pt.width;
    avgWidth /= static_cast<float>(N);

    double dashLen = std::max(1.8, (double)avgWidth * 4.0);
    double gapLen  = std::max(1.0, (double)avgWidth * 2.5);
    double cycle   = dashLen + gapLen;

    auto samplePointAt = [&](double d) -> Point2D {
        d = std::clamp(d, 0.0, totalLen);
        auto it = std::lower_bound(cumDist.begin(), cumDist.end(), d);
        if (it == cumDist.begin()) return pts.front();
        if (it == cumDist.end()) return pts.back();
        size_t idx = std::distance(cumDist.begin(), it);
        double d0 = cumDist[idx - 1];
        double d1 = cumDist[idx];
        double segL = d1 - d0;
        double t = (segL > 1e-7) ? (d - d0) / segL : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const auto& p0 = pts[idx - 1];
        const auto& p1 = pts[idx];
        Point2D res;
        res.x = p0.x + t * (p1.x - p0.x);
        res.y = p0.y + t * (p1.y - p0.y);
        res.width = p0.width + static_cast<float>(t) * (p1.width - p0.width);
        res.pressure = p0.pressure + static_cast<float>(t) * (p1.pressure - p0.pressure);
        return res;
    };

    for (double s0 = 0.0; s0 < totalLen; s0 += cycle) {
        double s1 = std::min(totalLen, s0 + dashLen);
        if (s1 <= s0) break;

        std::vector<Point2D> dashPts;
        dashPts.push_back(samplePointAt(s0));

        for (size_t i = 0; i < N; ++i) {
            if (cumDist[i] > s0 + 1e-5 && cumDist[i] < s1 - 1e-5) {
                dashPts.push_back(pts[i]);
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

// =============================================================================
// 3. DASH-DOT PATTERN
// =============================================================================

BLPath InkGenerator::BuildDashDotPattern(const std::vector<Point2D>& pts, CapType capType) {
    BLPath path;
    const size_t N = pts.size();
    std::vector<double> cumDist(N, 0.0);
    for (size_t i = 1; i < N; ++i) {
        cumDist[i] = cumDist[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    }
    double totalLen = cumDist.back();
    if (totalLen < 1e-5) {
        double r = std::max(0.05, (double)pts[0].width * 0.5);
        path.add_circle(BLCircle{pts[0].x, pts[0].y, r});
        return path;
    }

    float avgWidth = 0.0f;
    for (const auto& pt : pts) avgWidth += pt.width;
    avgWidth /= static_cast<float>(N);

    double dashLen = std::max(2.0, (double)avgWidth * 4.2);
    double gap1    = std::max(1.0, (double)avgWidth * 2.0);
    double dotLen  = std::max(0.5, (double)avgWidth * 1.0);
    double gap2    = std::max(1.0, (double)avgWidth * 2.0);
    double cycle   = dashLen + gap1 + dotLen + gap2;

    auto samplePointAt = [&](double d) -> Point2D {
        d = std::clamp(d, 0.0, totalLen);
        auto it = std::lower_bound(cumDist.begin(), cumDist.end(), d);
        if (it == cumDist.begin()) return pts.front();
        if (it == cumDist.end()) return pts.back();
        size_t idx = std::distance(cumDist.begin(), it);
        double d0 = cumDist[idx - 1];
        double d1 = cumDist[idx];
        double segL = d1 - d0;
        double t = (segL > 1e-7) ? (d - d0) / segL : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const auto& p0 = pts[idx - 1];
        const auto& p1 = pts[idx];
        Point2D res;
        res.x = p0.x + t * (p1.x - p0.x);
        res.y = p0.y + t * (p1.y - p0.y);
        res.width = p0.width + static_cast<float>(t) * (p1.width - p0.width);
        res.pressure = p0.pressure + static_cast<float>(t) * (p1.pressure - p0.pressure);
        return res;
    };

    for (double s0 = 0.0; s0 < totalLen; s0 += cycle) {
        // 1. Dash segment
        double s1 = std::min(totalLen, s0 + dashLen);
        if (s1 > s0) {
            std::vector<Point2D> dashPts;
            dashPts.push_back(samplePointAt(s0));
            for (size_t i = 0; i < N; ++i) {
                if (cumDist[i] > s0 + 1e-5 && cumDist[i] < s1 - 1e-5) {
                    dashPts.push_back(pts[i]);
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

        // 2. Dot segment
        double dotCenter = s0 + dashLen + gap1 + dotLen * 0.5;
        if (dotCenter <= totalLen) {
            Point2D dotPt = samplePointAt(dotCenter);
            double r = std::max(0.05, (double)dotPt.width * 0.55);
            path.add_circle(BLCircle{dotPt.x, dotPt.y, r});
        }
    }
    return path;
}
