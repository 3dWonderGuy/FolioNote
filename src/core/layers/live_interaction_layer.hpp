#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <blend2d/blend2d.h>

#include "core/engine/viewport.hpp"
#include "core/engine/stroke_smoother.hpp"

namespace Folio {

/**
 * @struct LivePoint
 * @brief Telemetry sample captured during an active in-flight pen gesture.
 */
struct LivePoint {
    double   worldX    = 0.0; ///< Pen tip X position in physical canvas millimeters (mm)
    double   worldY    = 0.0; ///< Pen tip Y position in physical canvas millimeters (mm)
    float    pressure  = 0.5f; ///< Stylus tip normalized pressure [0.0, 1.0]
    uint64_t timestamp = 0;   ///< Monotonic millisecond timestamp
};

/**
 * @enum LiveInteractionType
 * @brief Mode of transient interaction active on Layer 2.
 */
enum class LiveInteractionType : uint8_t {
    None,
    Inking,
    MarqueeSelection,
    GizmoTransform,
    ConnectorSnap,
    Custom
};

/**
 * @class LiveInteractionLayer
 * @brief Layer 2 Host: Handles high-Hz real-time tool feedback (inking, marquee, gizmo drag, snap lines)
 * before committing to the document.
 */
class LiveInteractionLayer {
public:
    LiveInteractionLayer() = default;
    ~LiveInteractionLayer() = default;

    LiveInteractionLayer(const LiveInteractionLayer&) = delete;
    LiveInteractionLayer& operator=(const LiveInteractionLayer&) = delete;

    // --- Inking & Vector Feedback ---
    void BeginStroke(const LivePoint& startPoint);
    void AppendPoint(const LivePoint& point);
    [[nodiscard]] std::vector<LivePoint> FinalizeStroke();
    [[nodiscard]] std::vector<Segment1D> FinalizeStrokeAsSegments(float baseWidthMm = 0.5f);
    void SetPenStyle(uint32_t argbColor, double widthMm) noexcept;
    [[nodiscard]] const std::vector<LivePoint>& GetPoints() const noexcept { return m_points; }

    // --- Lasso / Marquee Selection ---
    void BeginLasso(double worldXMm, double worldYMm);
    void AddLassoPoint(double worldXMm, double worldYMm);
    [[nodiscard]] std::vector<Point2D> FinishLasso();
    [[nodiscard]] const std::vector<Point2D>& GetLassoPoints() const noexcept { return m_lassoPoints; }

    // --- State & Custom Tool Render Callbacks ---
    void SetInteractionType(LiveInteractionType type) noexcept { m_activeType = type; }
    [[nodiscard]] LiveInteractionType GetInteractionType() const noexcept { return m_activeType; }
    [[nodiscard]] bool HasActiveInteraction() const noexcept { return m_activeType != LiveInteractionType::None; }

    void SetCustomFeedbackRenderer(std::function<void(BLContext&, const Viewport&)> renderer);
    void Clear();

    /**
     * @brief Direct-draw pass for active tool feedback.
     */
    void Render(BLContext& ctx, const Viewport& viewport);

private:
    LiveInteractionType m_activeType = LiveInteractionType::None;

    // Live inking state
    std::vector<LivePoint> m_points;
    uint32_t               m_penColor   = 0xFF000000;
    double                 m_penWidthMm = 0.5;

    // Live lasso / marquee selection state
    std::vector<Point2D>   m_lassoPoints;

    // Custom tool hook (selection marquee, gizmo previews, alignment lines)
    std::function<void(BLContext&, const Viewport&)> m_customRenderer;
};

} // namespace Folio