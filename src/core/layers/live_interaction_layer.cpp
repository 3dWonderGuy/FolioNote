/**
 * =========================================================================================
 * @file core/layers/live_interaction_layer.cpp
 * @brief Implementation of Layer 2 Real-Time Interaction (Inking, Lasso, Tool Feedback)
 * =========================================================================================
 */

#include "core/layers/live_interaction_layer.hpp"
#include <cmath>
#include <algorithm>

namespace Folio {

void LiveInteractionLayer::BeginStroke(const LivePoint& startPoint) {
    m_points.clear();
    m_points.reserve(256);
    m_points.push_back(startPoint);
    m_activeType = LiveInteractionType::Inking;
}

void LiveInteractionLayer::AppendPoint(const LivePoint& point) {
    if (m_activeType != LiveInteractionType::Inking) return;
    m_points.push_back(point);
}

std::vector<LivePoint> LiveInteractionLayer::FinalizeStroke() {
    m_activeType = LiveInteractionType::None;
    std::vector<LivePoint> committedPoints = std::move(m_points);
    m_points.clear();
    return committedPoints;
}

std::vector<Segment1D> LiveInteractionLayer::FinalizeStrokeAsSegments(float baseWidthMm) {
    m_activeType = LiveInteractionType::None;
    std::vector<LivePoint> pts = std::move(m_points);
    m_points.clear();

    std::vector<Segment1D> segments;
    if (pts.empty()) return segments;

    float w = baseWidthMm > 0.0f ? baseWidthMm : static_cast<float>(m_penWidthMm);
    if (w <= 0.0f) w = 0.5f;

    if (pts.size() == 1) {
        Segment1D seg;
        seg.p0 = Point2D{ pts[0].worldX, pts[0].worldY };
        seg.p1 = Point2D{ pts[0].worldX + 0.01, pts[0].worldY };
        seg.width = w * std::max(0.1f, pts[0].pressure);
        segments.push_back(seg);
        return segments;
    }

    segments.reserve(pts.size() - 1);
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        Segment1D seg;
        seg.p0 = Point2D{ pts[i].worldX, pts[i].worldY };
        seg.p1 = Point2D{ pts[i + 1].worldX, pts[i + 1].worldY };
        seg.width = w * std::max(0.1f, pts[i + 1].pressure);
        segments.push_back(seg);
    }
    return segments;
}

void LiveInteractionLayer::SetPenStyle(uint32_t argbColor, double widthMm) noexcept {
    m_penColor = argbColor;
    m_penWidthMm = widthMm;
}

void LiveInteractionLayer::BeginLasso(double worldXMm, double worldYMm) {
    m_activeType = LiveInteractionType::MarqueeSelection;
    m_lassoPoints.clear();
    m_lassoPoints.reserve(256);
    m_lassoPoints.push_back(Point2D{ worldXMm, worldYMm, 1.0f, 0.0 });
}

void LiveInteractionLayer::AddLassoPoint(double worldXMm, double worldYMm) {
    if (m_activeType != LiveInteractionType::MarqueeSelection || m_lassoPoints.empty()) return;

    Point2D pt{ worldXMm, worldYMm, 1.0f, 0.0 };
    // Filter micro-movements (< 0.5 mm) to bound polygon complexity
    if (std::hypot(pt.x - m_lassoPoints.back().x, pt.y - m_lassoPoints.back().y) < 0.5) return;
    m_lassoPoints.push_back(pt);
}

std::vector<Point2D> LiveInteractionLayer::FinishLasso() {
    if (m_activeType != LiveInteractionType::MarqueeSelection) return {};
    m_activeType = LiveInteractionType::None;
    std::vector<Point2D> finishedLasso = std::move(m_lassoPoints);
    m_lassoPoints.clear();
    return finishedLasso;
}

void LiveInteractionLayer::SetCustomFeedbackRenderer(std::function<void(BLContext&, const Viewport&)> renderer) {
    m_customRenderer = std::move(renderer);
    m_activeType = m_customRenderer ? LiveInteractionType::Custom : LiveInteractionType::None;
}

void LiveInteractionLayer::Clear() {
    m_activeType = LiveInteractionType::None;
    m_points.clear();
    m_lassoPoints.clear();
    m_customRenderer = nullptr;
}

void LiveInteractionLayer::Render(BLContext& ctx, const Viewport& viewport) {
    if (m_activeType == LiveInteractionType::None) return;

    // Render in-flight stylus path
    if (m_activeType == LiveInteractionType::Inking && m_points.size() >= 2) {
        ctx.save();
        ctx.set_transform(viewport.worldToScreenMatrix);

        BLPath path;
        path.move_to(m_points[0].worldX, m_points[0].worldY);

        for (size_t i = 1; i < m_points.size(); ++i) {
            path.line_to(m_points[i].worldX, m_points[i].worldY);
        }

        ctx.set_stroke_style(BLRgba32(m_penColor));
        ctx.set_stroke_width(m_penWidthMm);
        ctx.set_stroke_caps(BL_STROKE_CAP_ROUND);
        ctx.set_stroke_join(BL_STROKE_JOIN_ROUND);
        ctx.stroke_path(path);

        ctx.restore();
    }

    // Render in-flight lasso selection contour
    if (m_activeType == LiveInteractionType::MarqueeSelection && m_lassoPoints.size() >= 2) {
        ctx.save();
        ctx.set_transform(viewport.worldToScreenMatrix);

        BLPath path;
        path.move_to(m_lassoPoints[0].x, m_lassoPoints[0].y);
        for (size_t i = 1; i < m_lassoPoints.size(); ++i) {
            path.line_to(m_lassoPoints[i].x, m_lassoPoints[i].y);
        }

        ctx.set_stroke_style(BLRgba32(0x3B, 0x82, 0xF6, 0xD0)); // Accent blue
        ctx.set_stroke_width(0.5);
        ctx.stroke_path(path);

        ctx.restore();
    }

    // Render custom feedback (gizmo transform previews, snap lines)
    if (m_customRenderer) {
        m_customRenderer(ctx, viewport);
    }
}

} // namespace Folio