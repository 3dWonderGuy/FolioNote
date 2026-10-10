#pragma once
/**
 * =========================================================================================
 * @file core/canvas_engine/tools/ruler_tool.hpp
 * @brief Interactive Virtual Straightedge Ruler with Live Angle Tracking & Snapping
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & MATHEMATICAL PRINCIPLES:
 * ------------------------------------------------
 * The RulerTool provides an interactive drafting straightedge on the infinite canvas.
 * It fulfills three core capabilities:
 *
 * 1. Geometric State & Spatial Placement:
 *    - Positioned at world center C = (Cx, Cy) in physical canvas millimeters (mm).
 *    - Orientation defined by angle θ (in degrees and radians).
 *    - Physical dimensions: Length L (default 240mm) and Width W (default 52mm).
 *    - Unit systems: Metric (centimeters / millimeters) and Imperial (inches / fractions).
 *
 * 2. Vector Straightedge Snapping Calculus:
 *    - Direction unit vector: u = (cos θ, sin θ)
 *    - Normal unit vector:    v = (-sin θ, cos θ)
 *    - Top edge:    A_top = C - (L/2)*u + (W/2)*v,  B_top = C + (L/2)*u + (W/2)*v
 *    - Bottom edge: A_bot = C - (L/2)*u - (W/2)*v,  B_bot = C + (L/2)*u - (W/2)*v
 *    - For any incoming pen coordinate P:
 *        t = (P - C) · u
 *        d_v = (P - C) · v
 *        Δ_top = |d_v - W/2|,  Δ_bot = |d_v + W/2|
 *      If min(Δ_top, Δ_bot) <= snapThreshold, P is orthogonally projected onto the edge.
 *
 * 3. Interactive Manipulation & Presentation:
 *    - Center HUD angle dial showing digital orientation (e.g., "45.0°").
 *    - Dragging the ruler body translates C.
 *    - Dragging the rotation dial or circular end handles rotates θ.
 *    - Angle snapping: Automatically locks to 0°, 15°, 30°, 45°, 60°, 75°, 90° within ±2.0°.
 *    - Measurement tick marks:
 *        * Metric: 1cm major ticks with numerals, 5mm medium ticks, 1mm minor ticks.
 *        * Imperial: 1" major ticks with numerals, 1/2", 1/4", 1/8", 1/16" ticks.
 *    - Live stroke length gauge rendered along the active snapped edge during inking gestures.
 */

#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <imgui.h>

#include "core/ink_engine/stroke_smoother.hpp"
#include "core/canvas_engine/transform/canvas_transform.hpp"

namespace Folio {

/**
 * @enum RulerUnit
 * @brief Measurement calibration unit displayed on the ruler straightedges.
 */
enum class RulerUnit : uint8_t {
    Centimeters,  ///< Metric: millimeters and centimeters (default)
    Inches        ///< Imperial: inches and fractional 1/16th subdivisions
};

/**
 * @struct RulerSnapResult
 * @brief Telemetry result returned when projecting a stylus coordinate onto the ruler.
 */
struct RulerSnapResult {
    Point2D point;            ///< Snapped world coordinate (or original if not snapped)
    bool isSnapped = false;   ///< True if point was within snap radius of a straightedge
    bool isTopEdge = true;    ///< True if snapped to top edge; false if bottom edge
    double distanceAlongMm = 0.0; ///< Distance along edge from left end (mm)
};

/**
 * @class RulerTool
 * @brief Interactive digital straightedge guide for precision drafting and line drawing.
 */
class RulerTool {
public:
    RulerTool();
    ~RulerTool() = default;

    // --- State & Configuration ---
    [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }
    void SetEnabled(bool enabled) noexcept { m_enabled = enabled; }
    void ToggleEnabled() noexcept { m_enabled = !m_enabled; }

    [[nodiscard]] RulerUnit GetUnit() const noexcept { return m_unit; }
    void SetUnit(RulerUnit unit) noexcept { m_unit = unit; }
    void ToggleUnit() noexcept {
        m_unit = (m_unit == RulerUnit::Centimeters) ? RulerUnit::Inches : RulerUnit::Centimeters;
    }

    [[nodiscard]] Point2D GetCenter() const noexcept { return m_centerWorld; }
    void SetCenter(double worldXMm, double worldYMm) noexcept {
        m_centerWorld.x = worldXMm;
        m_centerWorld.y = worldYMm;
    }

    [[nodiscard]] double GetAngleDeg() const noexcept { return m_angleDeg; }
    [[nodiscard]] double GetAngleRad() const noexcept { return m_angleDeg * (3.14159265358979323846 / 180.0); }
    void SetAngleDeg(double degrees) noexcept;

    [[nodiscard]] double GetLengthMm() const noexcept { return m_lengthMm; }
    void SetLengthMm(double lengthMm) noexcept { m_lengthMm = (std::max)(50.0, lengthMm); }

    [[nodiscard]] double GetWidthMm() const noexcept { return m_widthMm; }
    void SetWidthMm(double widthMm) noexcept { m_widthMm = (std::max)(20.0, widthMm); }

    [[nodiscard]] double GetSnapThresholdMm() const noexcept { return m_snapThresholdMm; }
    void SetSnapThresholdMm(double threshMm) noexcept { m_snapThresholdMm = threshMm; }

    // --- Inking Integration: Vector Snapping ---
    /**
     * @brief Evaluates an incoming stylus or mouse world coordinate and snaps it to the ruler
     *        straightedge if it falls within the activation threshold.
     *
     * Mathematical Process:
     * 1. Evaluates direction vectors u = (cos θ, sin θ) and v = (-sin θ, cos θ).
     * 2. Projects vector (P - C) into longitudinal offset t and transverse offset d_v.
     * 3. Calculates distance to top straightedge (+W/2) and bottom straightedge (-W/2).
     * 4. If within threshold and longitudinally bounded (with generous 15mm run-off),
     *    clamps transverse offset to the nearest edge and returns the snapped coordinate.
     *
     * @param worldPoint Input stylus coordinate in physical world millimeters.
     * @return RulerSnapResult Snapped coordinates and edge telemetry.
     */
    [[nodiscard]] RulerSnapResult SnapPoint(const Point2D& worldPoint) const;

    /**
     * @brief Convenience overload that directly returns the snapped Point2D.
     */
    [[nodiscard]] Point2D SnapWorldPoint(const Point2D& worldPoint) const {
        return SnapPoint(worldPoint).point;
    }

    /**
     * @brief Notifies the ruler of active inking along an edge to display live length measurement.
     * @param isDrawing True while pen is pressed and drawing.
     * @param currentLengthMm Distance drawn along the ruler in millimeters.
     */
    void SetActiveInkingState(bool isDrawing, double currentLengthMm = 0.0) noexcept {
        m_isDrawingAlongEdge = isDrawing;
        m_activeDrawnLengthMm = currentLengthMm;
    }

    // --- Interactive ImGui Rendering & Manipulation ---
    /**
     * @brief Renders the visual ruler, measurement tick marks, angle dial, and interaction
     *        grips onto the ImGui window draw list.
     *
     * @param transform Active canvas coordinate transformation (pan, zoom, DPI).
     * @param canvasOrigin Screen pixel coordinate of the canvas viewport top-left.
     * @param canvasSize Dimensions of the canvas viewport in screen pixels.
     * @param isDarkMode Whether dark theme is currently active.
     */
    void RenderImGui(const CanvasTransform& transform,
                     const ImVec2& canvasOrigin,
                     const ImVec2& canvasSize,
                     bool isDarkMode);

private:
    bool       m_enabled           = false;
    RulerUnit  m_unit              = RulerUnit::Centimeters;
    Point2D    m_centerWorld       { 150.0, 150.0 };  ///< World position in physical mm
    double     m_angleDeg          = 0.0;            ///< Angle in degrees [-180, 180)
    double     m_lengthMm          = 240.0;          ///< Total physical length (e.g. 24 cm)
    double     m_widthMm           = 52.0;           ///< Physical width (e.g. 5.2 cm)
    double     m_snapThresholdMm   = 6.5;            ///< Snapping proximity threshold in mm

    // Live gesture telemetry
    bool       m_isDrawingAlongEdge  = false;
    double     m_activeDrawnLengthMm = 0.0;

    // Interactive Drag State
    enum class DragMode {
        None,
        MoveBody,
        RotateDial,
        RotateHandleLeft,
        RotateHandleRight
    };
    DragMode   m_dragMode          = DragMode::None;
    ImVec2     m_dragStartMouse    { 0.0f, 0.0f };
    Point2D    m_dragStartCenter   { 0.0, 0.0 };
    double     m_dragStartAngleDeg = 0.0;
    double     m_dragInitialMouseAngle = 0.0;

    // Helper functions
    static double NormalizeAngleDeg(double deg) noexcept;
    static double SnapAngleToIncrements(double deg, double increment = 15.0, double threshold = 2.0) noexcept;
};

} // namespace Folio
