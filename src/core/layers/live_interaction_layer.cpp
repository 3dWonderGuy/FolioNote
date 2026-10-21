/**
 * =========================================================================================
 * @file core/layers/live_interaction_layer.cpp
 * @brief Implementation of Layer 2 Real-Time Ephemeral Interaction Overlay
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & COORDINATE DISCIPLINE:
 * 1. Zero-Copy Borrowing:
 *    - Uses C++20 `std::span` for zero-allocation access to active tool telemetry.
 *    - Never clones geometry or allocates heap storage during per-frame render ticks.
 * 2. Coordinate Transformations:
 *    - World Space (mm): In-flight strokes, laser paths, and text editing boundaries are
 *      transformed using `viewport.worldToScreenMatrix` to scale and pan with camera motion.
 *    - Screen Space (px): Gizmo frames, scale handles, and rotation stems are rendered in
 *      fixed-pixel metrics via `viewport.WorldToScreenRect` so that touch/mouse hit targets
 *      remain sharp and constant regardless of zoom level.
 * 3. State Isolation:
 *    - Every rendering pass wraps its transformation and raster state inside `ctx.save()`
 *      and `ctx.restore()`.
 */

#include "core/layers/live_interaction_layer.hpp"
#include "core/canvas_engine/gizmo/selection_gizmo.hpp"
#include "core/canvas_engine/transform/canvas_transform.hpp"
#include "core/gpu/gpu_ink_manager.hpp"
#include <cmath>
#include <algorithm>

namespace Folio {

LiveInteractionLayer::LiveInteractionLayer() {
    // Pre-reserve capacity to guarantee zero heap reallocations during high-frequency gestures
    m_internalPoints.reserve(1024);
    m_internalLassoPoints.reserve(1024);
}

bool LiveInteractionLayer::HasActiveInteraction() const noexcept {
    return m_inkEngine.IsStrokeActive() ||
           m_hasBorrowedStroke ||
           m_hasActiveGizmo    ||
           (m_boundGizmo && m_boundGizmo->HasSelection()) ||
           m_hasActiveText     ||
           m_hasActiveMarquee  ||
           m_hasActiveEraser   ||
           m_isInking          ||
           m_isLassoing        ||
           (m_customRenderer != nullptr);
}

AABB LiveInteractionLayer::GetInteractionScreenBounds(const Viewport& viewport) const noexcept {
    if (!HasActiveInteraction()) {
        return AABB{};
    }

    if (m_customRenderer != nullptr) {
        return viewport.bounds;
    }

    const double effectiveScale = viewport.GetEffectiveScale();
    AABB bounds;

    // 1. In-flight ink stroke
    if (m_inkEngine.IsStrokeActive()) {
        const auto& livePts = m_inkEngine.GetLiveModeledPoints();
        for (const auto& pt : livePts) {
            Point2D sp = viewport.WorldToScreen(pt.x, pt.y);
            double rPx = pt.r * effectiveScale;
            bounds.Merge(sp.x - rPx, sp.y - rPx);
            bounds.Merge(sp.x + rPx, sp.y + rPx);
        }
        const auto& predPts = m_inkEngine.GetPredictedPoints();
        for (const auto& pt : predPts) {
            Point2D sp = viewport.WorldToScreen(pt.x, pt.y);
            double rPx = pt.r * effectiveScale;
            bounds.Merge(sp.x - rPx, sp.y - rPx);
            bounds.Merge(sp.x + rPx, sp.y + rPx);
        }
        bounds.Expand(12.0);
    } else if (m_hasBorrowedStroke && m_borrowedStroke.points.size() >= 2) {
        for (const auto& p : m_borrowedStroke.points) {
            Point2D sp = viewport.WorldToScreen(p.worldX, p.worldY);
            bounds.Merge(sp.x, sp.y);
        }
        double rPx = (m_borrowedStroke.baseWidthMm * 0.5) * effectiveScale + 8.0;
        bounds.Expand(rPx);
    } else if (m_isInking && m_internalPoints.size() >= 2) {
        for (const auto& p : m_internalPoints) {
            Point2D sp = viewport.WorldToScreen(p.worldX, p.worldY);
            bounds.Merge(sp.x, sp.y);
        }
        double rPx = (m_penWidthMm * 0.5) * effectiveScale + 8.0;
        bounds.Expand(rPx);
    }

    // 2. Selection / Transform Gizmo
    if (m_boundGizmo && m_boundGizmo->HasSelection()) {
        const AABB& wBox = m_boundGizmo->bounds;
        if (wBox.IsValid() && !wBox.IsEmpty()) {
            BLRect sRect = viewport.WorldToScreenRect(wBox);
            bounds.Merge(sRect.x, sRect.y);
            bounds.Merge(sRect.x + sRect.w, sRect.y + sRect.h);
            bounds.Expand(40.0);
        }
    } else if (m_hasActiveGizmo && !m_activeGizmo.targetBounds.IsEmpty()) {
        BLRect sRect = viewport.WorldToScreenRect(m_activeGizmo.targetBounds);
        bounds.Merge(sRect.x, sRect.y);
        bounds.Merge(sRect.x + sRect.w, sRect.y + sRect.h);
        bounds.Expand(40.0);
    }

    // 3. Eraser Cursor Reticle
    if (m_hasActiveEraser && m_activeEraser.isVisible) {
        double r = m_activeEraser.radiusPx + 8.0;
        bounds.Merge(m_activeEraser.screenX - r, m_activeEraser.screenY - r);
        bounds.Merge(m_activeEraser.screenX + r, m_activeEraser.screenY + r);
    }

    // 4. Marquee Box / Lasso
    if (m_hasActiveMarquee) {
        if (m_activeMarquee.isScreenRect) {
            bounds.Merge(m_activeMarquee.screenMinX - 2.0, m_activeMarquee.screenMinY - 2.0);
            bounds.Merge(m_activeMarquee.screenMaxX + 2.0, m_activeMarquee.screenMaxY + 2.0);
        } else if (!m_activeMarquee.rectBounds.IsEmpty()) {
            BLRect sRect = viewport.WorldToScreenRect(m_activeMarquee.rectBounds);
            bounds.Merge(sRect.x - 2.0, sRect.y - 2.0);
            bounds.Merge(sRect.x + sRect.w + 2.0, sRect.y + sRect.h + 2.0);
        } else if (!m_activeMarquee.polygonPoints.empty()) {
            for (const auto& pt : m_activeMarquee.polygonPoints) {
                Point2D sp = viewport.WorldToScreen(pt.x, pt.y);
                bounds.Merge(sp.x, sp.y);
            }
            bounds.Expand(4.0);
        }
    }

    // 5. Active Text Editor Caret
    if (m_hasActiveText) {
        Point2D sp = viewport.WorldToScreen(m_activeText.caretWorldX, m_activeText.caretWorldY);
        double hPx = m_activeText.caretHeightMm * effectiveScale;
        bounds.Merge(sp.x - 6.0, sp.y - 6.0);
        bounds.Merge(sp.x + 6.0, sp.y + hPx + 6.0);
        for (const auto& r : m_activeText.selectionRects) {
            BLRect sRect = viewport.WorldToScreenRect(r);
            bounds.Merge(sRect.x, sRect.y);
            bounds.Merge(sRect.x + sRect.w, sRect.y + sRect.h);
        }
    }

    return bounds;
}

void LiveInteractionLayer::Render(BLContext& ctx, const Viewport& viewport) {
    if (!HasActiveInteraction()) {
        return;
    }

    // Pass 1: In-Flight Vector Ink Stroke (World Coordinates)
    RenderInFlightInk(ctx, viewport);

    // Pass 2: Selection / Transform Gizmo & Handles (Screen Coordinates for crisp fixed-size handles)
    RenderGizmo(ctx, viewport);

    // Pass 4: Active Text Editor Caret & Selection Highlights (World Coordinates)
    RenderTextEditor(ctx, viewport);

    // Pass 5: Marquee Selection Box / Lasso Polygon
    RenderMarquee(ctx, viewport);

    // Pass 6: Live Eraser Circular Cursor Reticle (Screen Coordinates)
    RenderEraser(ctx);

    // Pass 7: Custom Tool Callbacks (Alignment snapping lines, custom preview gizmos)
    if (m_customRenderer) {
        ctx.save();
        m_customRenderer(ctx, viewport);
        ctx.restore();
    }
}

/**
 * @brief Renders the active in-flight pen stroke in world space.
 *
 * MATHEMATICAL PROCESS:
 * - Priority 1: High-Performance Modeled Ribbon Polygon (Google Ink spring-damper physics).
 *   Fills the 2D extruded polygon contour using non-zero winding rules.
 * - Priority 2: Fallback for borrowed spans / raw points with round caps & round joins.
 */
void LiveInteractionLayer::RenderInFlightInk(BLContext& ctx, const Viewport& viewport) {
    // 1. Primary Vector Inking Engine: SDL_GPU Hardware Triangle Mesh & Modeled Ribbon
    if (m_inkEngine.IsStrokeActive()) {
        const auto& activeStyle = m_inkEngine.GetActiveStyle();
        auto& gpu = GpuInkManager::Instance();

        if (activeStyle.pattern == StrokePattern::Solid && gpu.IsEnabled()) {
            const float effectiveScale = static_cast<float>(viewport.GetEffectiveScale());
            gpu.BeginBatch();

            const auto& liveMesh = m_inkEngine.GetLiveMesh();
            if (!liveMesh.empty()) {
                gpu.SubmitMesh(liveMesh, viewport.worldToScreenMatrix, effectiveScale, 1.0f, m_activeStrokeBlendMode, 1);
            }

            const auto& predMesh = m_inkEngine.GetPredictedMesh();
            if (!predMesh.empty()) {
                gpu.SubmitMesh(predMesh, viewport.worldToScreenMatrix, effectiveScale, 1.0f, m_activeStrokeBlendMode, 1);
            }

            if (gpu.HasPendingDraws()) {
                AABB liveBounds = GetInteractionScreenBounds(viewport);
                if (liveBounds.IsValid() && !liveBounds.IsEmpty()) {
                    int clipX = static_cast<int>(std::floor(liveBounds.minX));
                    int clipY = static_cast<int>(std::floor(liveBounds.minY));
                    int clipW = static_cast<int>(std::ceil(liveBounds.maxX)) - clipX;
                    int clipH = static_cast<int>(std::ceil(liveBounds.maxY)) - clipY;
                    gpu.FlushToContext(ctx, clipX, clipY, clipW, clipH);
                } else {
                    gpu.FlushToContext(ctx);
                }
                return;
            }
        }

        ctx.save();
        ctx.set_transform(viewport.worldToScreenMatrix);

        if (activeStyle.pattern == StrokePattern::TexturedPencil) {
            const auto& livePts = m_inkEngine.GetLiveModeledPoints();
            const auto& liveOutline = m_inkEngine.GetLiveOutlinePath();
            if (!livePts.empty()) {
                InkGenerator::RenderPencilStroke(ctx, livePts, activeStyle.baseWidth, activeStyle.color, &liveOutline);
            }
            const auto& predPts = m_inkEngine.GetPredictedPoints();
            const auto& predOutline = m_inkEngine.GetPredictedOutlinePath();
            if (predPts.size() >= 2) {
                InkGenerator::RenderPencilStroke(ctx, predPts, activeStyle.baseWidth, activeStyle.color, &predOutline);
            }
        } else {
            if (m_activeStrokeBlendMode == BlendMode::Multiply) {
                ctx.set_comp_op(BL_COMP_OP_MULTIPLY);
            }
            ctx.set_fill_style(BLRgba32(activeStyle.color.value));
            ctx.set_fill_rule(BL_FILL_RULE_EVEN_ODD);

            const auto& liveOutline = m_inkEngine.GetLiveOutlinePath();
            if (!liveOutline.is_empty()) {
                ctx.fill_path(liveOutline);
            }

            const auto& predOutline = m_inkEngine.GetPredictedOutlinePath();
            if (!predOutline.is_empty()) {
                ctx.fill_path(predOutline);
            } else {
                const auto& livePts = m_inkEngine.GetLiveModeledPoints();
                if (!livePts.empty() && activeStyle.capType == CapType::Round) {
                    ctx.fill_circle(BLCircle(livePts.back().x, livePts.back().y, livePts.back().r));
                }
            }
        }

        ctx.restore();
        return;
    }

    // 2. Fallback for Borrowed Tool Spans / Direct LivePoint Ingestion
    std::span<const LivePoint> pts;
    BLRgba32 strokeColor;
    double strokeWidthMm = 0.5;

    if (m_hasBorrowedStroke && m_borrowedStroke.points.size() >= 2) {
        pts = m_borrowedStroke.points;
        strokeColor = m_borrowedStroke.color;
        strokeWidthMm = m_borrowedStroke.baseWidthMm;
    } else if (m_isInking && m_internalPoints.size() >= 2) {
        pts = m_internalPoints;
        strokeColor = BLRgba32(m_penColor);
        strokeWidthMm = m_penWidthMm;
    } else {
        return;
    }

    ctx.save();
    ctx.set_transform(viewport.worldToScreenMatrix);

    BLPath path;
    path.move_to(pts[0].worldX, pts[0].worldY);
    for (size_t i = 1; i < pts.size(); ++i) {
        path.line_to(pts[i].worldX, pts[i].worldY);
    }

    ctx.set_stroke_style(strokeColor);
    ctx.set_stroke_width(strokeWidthMm);
    ctx.set_stroke_caps(BL_STROKE_CAP_ROUND);
    ctx.set_stroke_join(BL_STROKE_JOIN_ROUND);
    ctx.stroke_path(path);

    ctx.restore();
}



/**
 * @brief Renders the selection and transform gizmo in crisp SCREEN coordinates.
 *
 * GEOMETRIC LAYOUT (Screen Pixels):
 * - Bounding box frame: `screenBox = viewport.WorldToScreenRect(targetBounds)`.
 * - 8 Scale handles positioned at the 4 corners and 4 edge midpoints:
 *     Top-Left, Top-Mid, Top-Right, Right-Mid, Bottom-Right, Bottom-Mid, Bottom-Left, Left-Mid.
 * - Rotation handle: Positioned above Top-Mid by `rotationHandleOffsetPx` with connecting stem.
 */
void LiveInteractionLayer::RenderGizmo(BLContext& ctx, const Viewport& viewport) {
    // 1. Direct Bound Application Gizmo Pass (takes precedence)
    if (m_boundGizmo && m_boundTransform && m_boundGizmo->HasSelection()) {
        m_boundGizmo->Render(ctx, *m_boundTransform);
        return;
    }

    // 2. Fallback Lightweight Gizmo Pass
    if (!m_hasActiveGizmo || m_activeGizmo.targetBounds.IsEmpty()) {
        return;
    }

    BLRect box = viewport.WorldToScreenRect(m_activeGizmo.targetBounds);
    if (box.w <= 0.0 || box.h <= 0.0) {
        return;
    }

    ctx.save();
    // Context remains in identity screen space (pixels)

    // 1. Draw outer bounding rectangle
    ctx.set_stroke_style(m_activeGizmo.outlineColor);
    ctx.set_stroke_width(m_activeGizmo.borderWidthPx);
    ctx.stroke_rect(box);

    const double r = m_activeGizmo.handleRadiusPx;

    // 2. Draw 8 scale / resize handles
    if (m_activeGizmo.showScaleHandles) {
        const double x0 = box.x;
        const double x1 = box.x + box.w * 0.5;
        const double x2 = box.x + box.w;
        const double y0 = box.y;
        const double y1 = box.y + box.h * 0.5;
        const double y2 = box.y + box.h;

        const BLPoint handles[8] = {
            { x0, y0 }, { x1, y0 }, { x2, y0 }, // Top
            { x2, y1 },                          // Right
            { x2, y2 }, { x1, y2 }, { x0, y2 }, // Bottom
            { x0, y1 }                           // Left
        };

        for (const auto& pt : handles) {
            ctx.set_fill_style(m_activeGizmo.handleFillColor);
            ctx.fill_circle(pt.x, pt.y, r);
            ctx.set_stroke_style(m_activeGizmo.handleBorderColor);
            ctx.set_stroke_width(1.2);
            ctx.stroke_circle(pt.x, pt.y, r);
        }
    }

    // 3. Draw rotation handle & connecting stem
    if (m_activeGizmo.showRotateHandle) {
        const double midX = box.x + box.w * 0.5;
        const double topY = box.y;
        const double rotY = topY - m_activeGizmo.rotationHandleOffsetPx;

        // Stem line
        ctx.set_stroke_style(m_activeGizmo.outlineColor);
        ctx.set_stroke_width(1.2);
        ctx.stroke_line(midX, topY, midX, rotY);

        // Rotation circle handle
        ctx.set_fill_style(m_activeGizmo.handleFillColor);
        ctx.fill_circle(midX, rotY, r * 1.1);
        ctx.set_stroke_style(m_activeGizmo.handleBorderColor);
        ctx.set_stroke_width(1.5);
        ctx.stroke_circle(midX, rotY, r * 1.1);
    }

    ctx.restore();
}

/**
 * @brief Renders the active text editor caret and range selection highlights.
 */
void LiveInteractionLayer::RenderTextEditor(BLContext& ctx, const Viewport& viewport) {
    if (!m_hasActiveText) {
        return;
    }

    ctx.save();
    ctx.set_transform(viewport.worldToScreenMatrix);

    // 1. Draw text selection background highlight boxes
    if (!m_activeText.selectionRects.empty()) {
        ctx.set_fill_style(m_activeText.selectionFillColor);
        for (const auto& rect : m_activeText.selectionRects) {
            if (!rect.IsEmpty() && rect.IsValid()) {
                ctx.fill_rect(BLRect(rect.minX, rect.minY, rect.Width(), rect.Height()));
            }
        }
    }

    // 2. Draw blinking text insertion cursor (caret)
    if (m_activeText.isCaretVisible) {
        ctx.set_fill_style(m_activeText.caretColor);
        ctx.fill_rect(BLRect(
            m_activeText.caretWorldX,
            m_activeText.caretWorldY,
            m_activeText.caretWidthMm,
            m_activeText.caretHeightMm
        ));
    }

    ctx.restore();
}

/**
 * @brief Renders transient marquee selection rectangle or lasso contour.
 */
void LiveInteractionLayer::RenderMarquee(BLContext& ctx, const Viewport& viewport) {
    // Check borrowed marquee first
    if (m_hasActiveMarquee) {
        // Direct screen-space rectangular marquee (pixel coordinates)
        if (m_activeMarquee.isScreenRect) {
            float bx = (std::min)(m_activeMarquee.screenMinX, m_activeMarquee.screenMaxX);
            float by = (std::min)(m_activeMarquee.screenMinY, m_activeMarquee.screenMaxY);
            float bw = std::abs(m_activeMarquee.screenMaxX - m_activeMarquee.screenMinX);
            float bh = std::abs(m_activeMarquee.screenMaxY - m_activeMarquee.screenMinY);

            ctx.save();
            ctx.set_fill_style(m_activeMarquee.fillColor);
            ctx.fill_rect(bx, by, bw, bh);
            ctx.set_stroke_style(m_activeMarquee.strokeColor);
            ctx.set_stroke_width(m_activeMarquee.strokeWidthPx);
            ctx.stroke_rect(bx, by, bw, bh);
            ctx.restore();
            return;
        }

        if (m_activeMarquee.isLasso && m_activeMarquee.polygonPoints.size() >= 2) {
            ctx.save();
            ctx.set_transform(viewport.worldToScreenMatrix);

            BLPath path;
            const auto& pts = m_activeMarquee.polygonPoints;
            path.move_to(pts[0].x, pts[0].y);
            for (size_t i = 1; i < pts.size(); ++i) {
                path.line_to(pts[i].x, pts[i].y);
            }

            ctx.set_stroke_style(m_activeMarquee.strokeColor);
            ctx.set_stroke_width(0.5);
            ctx.stroke_path(path);
            ctx.restore();
            return;
        }

        if (!m_activeMarquee.isLasso && !m_activeMarquee.rectBounds.IsEmpty()) {
            BLRect sRect = viewport.WorldToScreenRect(m_activeMarquee.rectBounds);
            ctx.save();
            ctx.set_fill_style(m_activeMarquee.fillColor);
            ctx.fill_rect(sRect);
            ctx.set_stroke_style(m_activeMarquee.strokeColor);
            ctx.set_stroke_width(m_activeMarquee.strokeWidthPx);
            ctx.stroke_rect(sRect);
            ctx.restore();
            return;
        }
    }

    // Fallback: internal lasso buffer from direct ingestion
    if (m_isLassoing && m_internalLassoPoints.size() >= 2) {
        ctx.save();
        ctx.set_transform(viewport.worldToScreenMatrix);

        BLPath path;
        path.move_to(m_internalLassoPoints[0].x, m_internalLassoPoints[0].y);
        for (size_t i = 1; i < m_internalLassoPoints.size(); ++i) {
            path.line_to(m_internalLassoPoints[i].x, m_internalLassoPoints[i].y);
        }

        ctx.set_stroke_style(BLRgba32(0x3B, 0x82, 0xF6, 0xD0)); // Accent blue
        ctx.set_stroke_width(0.5);
        ctx.stroke_path(path);

        ctx.restore();
    }
}

/**
 * @brief Renders the live interactive eraser reticle in screen coordinates.
 *
 * Provides visual cues for both:
 * 1. Stroke Eraser: Crosshair reticle with pink/red accent indicator and center cross line.
 * 2. Area/Object Eraser: Soft blue circular wash with glowing rim and central precision dot.
 */
void LiveInteractionLayer::RenderEraser(BLContext& ctx) {
    if (!m_hasActiveEraser || !m_activeEraser.isVisible) {
        return;
    }

    ctx.save();
    const float cx = m_activeEraser.screenX;
    const float cy = m_activeEraser.screenY;
    const double radiusPx = m_activeEraser.radiusPx;

    if (m_activeEraser.isStrokeEraser) {
        const double r = (std::max)(4.0, radiusPx);
        ctx.set_stroke_style(BLRgba32(0xFF, 0x40, 0x81, 0xDD));
        ctx.set_stroke_width(1.5);
        ctx.stroke_circle(cx, cy, r);

        ctx.set_fill_style(m_activeEraser.isDown ? BLRgba32(0xFF, 0x40, 0x81, 0x2E) : BLRgba32(0xFF, 0x40, 0x81, 0x12));
        ctx.fill_circle(cx, cy, r);

        ctx.stroke_line(cx - 3.5, cy, cx + 3.5, cy);
        ctx.stroke_line(cx, cy - 3.5, cx, cy + 3.5);
    } else {
        ctx.set_stroke_style(BLRgba32(0x29, 0xB6, 0xF6, 0xEE));
        ctx.set_stroke_width(1.5);
        ctx.stroke_circle(cx, cy, radiusPx);

        ctx.set_fill_style(m_activeEraser.isDown ? BLRgba32(0x29, 0xB6, 0xF6, 0x3A) : BLRgba32(0x29, 0xB6, 0xF6, 0x14));
        ctx.fill_circle(cx, cy, radiusPx);

        ctx.fill_circle(cx, cy, 1.2, BLRgba32(0x29, 0xB6, 0xF6, 0xFF));
    }
    ctx.restore();
}

// -----------------------------------------------------------------------------
// Direct Ingestion API (Zero Allocation Runtime Buffer)
// -----------------------------------------------------------------------------
// High-Performance Physics Inking API (Google Ink Stroke Modeler)
// -----------------------------------------------------------------------------

void LiveInteractionLayer::BeginStroke(double worldXMm, double worldYMm, float pressure, double timeSec,
                                       const PenTool& tool, float zoomScale, float tiltX, float tiltY) {
    m_activeStrokeBlendMode = (tool.penType == PenType::Highlighter || tool.blendMode == BlendMode::Multiply)
                                  ? BlendMode::Multiply
                                  : tool.blendMode;
    m_inkEngine.BeginStroke(worldXMm, worldYMm, pressure, timeSec, tool, zoomScale, tiltX, tiltY);
    m_isInking = true;
}

void LiveInteractionLayer::AddStrokePoint(double worldXMm, double worldYMm, float pressure, double timeSec,
                                          float zoomScale, float tiltX, float tiltY) {
    m_inkEngine.AppendPoint(worldXMm, worldYMm, pressure, timeSec, tiltX, tiltY);
}

FinishedStrokeData LiveInteractionLayer::FinishStroke() {
    m_isInking = false;
    m_activeStrokeBlendMode = BlendMode::Normal;
    return m_inkEngine.FinishStroke();
}

void LiveInteractionLayer::CancelStroke() {
    m_isInking = false;
    m_activeStrokeBlendMode = BlendMode::Normal;
    m_inkEngine.CancelStroke();
}

// -----------------------------------------------------------------------------
// Direct Ingestion API (Zero Allocation Fallback / Telemetry Buffer)
// -----------------------------------------------------------------------------

void LiveInteractionLayer::BeginStroke(const LivePoint& startPoint) {
    m_activeStrokeBlendMode = m_borrowedStroke.isHighlighter ? BlendMode::Multiply : BlendMode::Normal;
    m_internalPoints.clear();
    m_internalPoints.push_back(startPoint);
    m_isInking = true;
}

void LiveInteractionLayer::AppendPoint(const LivePoint& point) {
    if (!m_isInking) return;
    m_internalPoints.push_back(point);
}

std::vector<LivePoint> LiveInteractionLayer::FinalizeStroke() {
    m_isInking = false;
    std::vector<LivePoint> committedPoints = std::move(m_internalPoints);
    m_internalPoints.clear();
    m_internalPoints.reserve(1024);
    return committedPoints;
}

std::vector<Segment1D> LiveInteractionLayer::FinalizeStrokeAsSegments(float baseWidthMm) {
    m_isInking = false;
    std::vector<LivePoint> pts = std::move(m_internalPoints);
    m_internalPoints.clear();
    m_internalPoints.reserve(1024);

    std::vector<Segment1D> segments;
    if (pts.empty()) return segments;

    float w = baseWidthMm > 0.0f ? baseWidthMm : static_cast<float>(m_penWidthMm);
    if (w <= 0.0f) w = 0.5f;

    if (pts.size() == 1) {
        Segment1D seg;
        seg.p0 = Point2D{ pts[0].worldX, pts[0].worldY };
        seg.p1 = Point2D{ pts[0].worldX + 0.01, pts[0].worldY };
        seg.width = w * (std::max)(0.1f, pts[0].pressure);
        segments.push_back(seg);
        return segments;
    }

    segments.reserve(pts.size() - 1);
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        Segment1D seg;
        seg.p0 = Point2D{ pts[i].worldX, pts[i].worldY };
        seg.p1 = Point2D{ pts[i + 1].worldX, pts[i + 1].worldY };
        seg.width = w * (std::max)(0.1f, pts[i + 1].pressure);
        segments.push_back(seg);
    }
    return segments;
}

void LiveInteractionLayer::SetPenStyle(uint32_t argbColor, double widthMm) noexcept {
    m_penColor = argbColor;
    m_penWidthMm = widthMm;
}

void LiveInteractionLayer::BeginLasso(double worldXMm, double worldYMm) {
    m_internalLassoPoints.clear();
    m_internalLassoPoints.push_back(Point2D{ worldXMm, worldYMm });
    m_isLassoing = true;
}

void LiveInteractionLayer::AddLassoPoint(double worldXMm, double worldYMm) {
    if (!m_isLassoing || m_internalLassoPoints.empty()) return;

    Point2D pt{ worldXMm, worldYMm };
    // Filter micro-movements (< 0.5 mm) to bound polygon complexity
    if (std::hypot(pt.x - m_internalLassoPoints.back().x, pt.y - m_internalLassoPoints.back().y) < 0.5) {
        return;
    }
    m_internalLassoPoints.push_back(pt);
}

std::vector<Point2D> LiveInteractionLayer::FinishLasso() {
    m_isLassoing = false;
    std::vector<Point2D> finishedLasso = std::move(m_internalLassoPoints);
    m_internalLassoPoints.clear();
    m_internalLassoPoints.reserve(1024);
    return finishedLasso;
}

void LiveInteractionLayer::Clear() noexcept {
    m_hasBorrowedStroke = false;
    m_hasActiveGizmo    = false;
    m_boundGizmo        = nullptr;
    m_boundTransform    = nullptr;
    m_hasActiveText     = false;
    m_hasActiveMarquee  = false;
    m_hasActiveEraser   = false;
    m_activeEraser.isVisible = false;
    m_isInking          = false;
    m_isLassoing        = false;
    m_internalPoints.clear();
    m_internalLassoPoints.clear();
    m_customRenderer    = nullptr;
}

} // namespace Folio