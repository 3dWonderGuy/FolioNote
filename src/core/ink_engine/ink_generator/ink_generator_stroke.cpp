#include "core/ink_engine/ink_generator.hpp"

#include <vector>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =============================================================================
// 1. SINGLE-POINT ENDCAP (Taps / Dots)
// =============================================================================

BLPath InkGenerator::BuildSinglePointCap(const Point2D& pt, CapType capType) {
    BLPath path;
    double r = std::max(0.05, (double)pt.width * 0.5);
    if (capType == CapType::Square) {
        path.add_box(BLBox{pt.x - r, pt.y - r, pt.x + r, pt.y + r});
    } else {
        path.add_circle(BLCircle{pt.x, pt.y, r});
    }
    return path;
}

// =============================================================================
// 2. OPEN STROKE BUILDER
// =============================================================================

BLPath InkGenerator::BuildStroke(const std::vector<Point2D>& centerline,
                                CapType capType,
                                StrokePattern pattern) {
    if (centerline.empty()) return BLPath{};
    if (centerline.size() == 1) return BuildSinglePointCap(centerline[0], capType);

    // Continuous line (standard pen/brush): fast-path directly to ribbon
    if (pattern == StrokePattern::Solid) {
        return BuildSolidRibbon(centerline, capType);
    }

    // Discontinuous patterns: only executed when pattern is non-solid
    return ApplyDiscontinuousPattern(centerline, capType, pattern);
}

// =============================================================================
// 3. CONTINUOUS SOLID RIBBON TESSELLATION
// =============================================================================

BLPath InkGenerator::BuildSolidRibbon(const std::vector<Point2D>& pts, CapType capType) {
    (void)capType;
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

    for (size_t i = 0; i < N; ++i) {
        double nx = 0.0, ny = 0.0;
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

    for (size_t i = 1; i < N; ++i) {
        path.line_to(left[i].x, left[i].y);
    }

    path.line_to(right[N - 1].x, right[N - 1].y);

    for (size_t i = N - 1; i > 0; --i) {
        path.line_to(right[i - 1].x, right[i - 1].y);
    }

    path.line_to(left[0].x, left[0].y);
    path.close();
    return path;
}
