#include "core/ink_engine/ink_generator.hpp"

#include <vector>
#include <cmath>

// =============================================================================
// 1. TOP-LEVEL DISPATCHER
// =============================================================================

BLPath InkGenerator::GenerateStroke(const std::vector<Point2D>& centerline,
                                    CapType capType,
                                    StrokePattern pattern,
                                    bool isStandardShape) {
    if (centerline.empty()) return BLPath{};

    if (isStandardShape && centerline.size() >= 3) {
        return BuildPolygon(centerline, pattern);
    } else {
        return BuildStroke(centerline, capType, pattern);
    }
}

// =============================================================================
// 2. CLOSED POLYGON OUTLINE BUILDER
// =============================================================================

BLPath InkGenerator::BuildPolygon(const std::vector<Point2D>& centerline,
                                  StrokePattern pattern) {
    if (centerline.empty()) return BLPath{};

    const std::vector<Point2D>* ptsPtr = &centerline;
    std::vector<Point2D> closed;
    double dx = centerline.back().x - centerline.front().x;
    double dy = centerline.back().y - centerline.front().y;

    if (std::hypot(dx, dy) > 1e-5) {
        closed = centerline;
        closed.push_back(centerline.front());
        ptsPtr = &closed;
    }

    const auto& pts = *ptsPtr;
    if (pts.size() == 1) return BuildSinglePointCap(pts[0], CapType::Round);

    // Continuous line: fast-path directly to ribbon (polygons always Round joins/caps)
    if (pattern == StrokePattern::Solid) {
        return BuildSolidRibbon(pts, CapType::Round);
    }

    // Discontinuous patterns: only executed when pattern is non-solid
    return ApplyDiscontinuousPattern(pts, CapType::Round, pattern);
}
