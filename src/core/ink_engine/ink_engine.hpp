#pragma once

/**
 * @file ink_engine.hpp
 * @brief Universal inking physics, geometry synthesis, and presentation data engine.
 *
 * ARCHITECTURAL DESIGN:
 * ---------------------
 * InkEngine is the single public facade for all handwriting, dynamic width evaluation,
 * predictive stroke modeling, and outline generation.
 *
 * INTERNAL PIPELINE & HELPERS (ink_engine.cpp):
 * - MakeGoogleInput()       : Translates our Point2D into Google Ink's Input packet (kDown/kMove/kUp).
 * - ToPoint2D()             : Translates Google Ink's smoothed Result back into our Point2D with evaluated width.
 * - CalculateDynamicWidth() : Delegates pressure, velocity thinning, and tilt width math to StrokeWidthEvaluator.
 * - ProcessGoogleResults()  : Converts smoothed Google results into 1D collision segments and live 2D outline polygon.
 * - UpdateGooglePrediction(): Projects Kalman-predicted points ahead of the pen tip to hide display latency.
 */

#include <vector>
#include <memory>
#include <cstdint>
#include <blend2d/blend2d.h>

#include "input/pen_palette.hpp"
#include "core/ink_engine/ink_stroke_data.hpp"
#include "core/ink_engine/ink_config.hpp"
#include "core/ink_engine/stroke_outline_builder.hpp"
#include "core/objects/primitives/shape_types.hpp"

// Forward declarations for Google Ink Stroke Modeler
namespace ink::stroke_model {
    class StrokeModeler;
    struct Result;
}

namespace Folio {

/**
 * @class InkEngine
 * @brief Core data and execution engine for vector handwriting and live drawing.
 */
class InkEngine {
public:
    InkEngine();
    ~InkEngine();

    // Move-only semantics (owns unique Google Modeler instance)
    InkEngine(const InkEngine&) = delete;
    InkEngine& operator=(const InkEngine&) = delete;
    InkEngine(InkEngine&&) noexcept;
    InkEngine& operator=(InkEngine&&) noexcept;

    // -------------------------------------------------------------------------
    // 1. INKING LIFECYCLE (Coordinates in physical world millimeters)
    // -------------------------------------------------------------------------

    /**
     * @brief Initiates a new live vector stroke gesture.
     *
     * Working Process:
     * 1. Resets Google Ink modeler and transient point accumulation buffers.
     * 2. Configures spring-mass, damping drag, and wobble suppression parameters from active configuration.
     * 3. Seeds the first sample point and establishes the initial nominal stroke thickness.
     *
     * @param worldX Initial stylus X position in physical millimeters.
     * @param worldY Initial stylus Y position in physical millimeters.
     * @param pressure Initial contact pressure [0.0, 1.0].
     * @param timeSec Monotonic event timestamp in seconds.
     * @param tool Active pen styling, nib dimensions, cap geometry, and pigment.
     * @param zoomScale Current viewport zoom factor (normalizes velocity thinning to screen speed).
     * @param tiltX Stylus tilt angle along X axis in radians/degrees.
     * @param tiltY Stylus tilt angle along Y axis in radians/degrees.
     */
    void BeginStroke(double worldX, double worldY, float pressure, double timeSec,
                     const PenTool& tool, float zoomScale = 1.0f,
                     float tiltX = 0.0f, float tiltY = 0.0f);

    /**
     * @brief Ingests continuous motion telemetry from the stylus or mouse.
     *
     * Working Process:
     * 1. Submits raw input sample (pos, time, pressure) to Google Ink's position and wobble filters.
     * 2. Solves dynamic line width via pressure gamma mapping and velocity thinning equations.
     * 3. Applies Exponential Moving Average (EMA) smoothing to eliminate single-frame thickness jitter.
     * 4. Reconstructs confirmed 2D outline path via StrokeOutlineBuilder.
     * 5. Solves predictive continuation frames to minimize perceived display latency at the pen tip.
     *
     * @param worldX Current stylus X position in millimeters.
     * @param worldY Current stylus Y position in millimeters.
     * @param pressure Current contact pressure [0.0, 1.0].
     * @param timeSec Monotonic event timestamp in seconds.
     * @param tiltX Current stylus tilt X.
     * @param tiltY Current stylus tilt Y.
     */
    void AppendPoint(double worldX, double worldY, float pressure, double timeSec,
                     float tiltX = 0.0f, float tiltY = 0.0f);

    /**
     * @brief Finalizes the active stroke gesture upon pointer release.
     *
     * Working Process:
     * 1. Submits a final kUp event to flush lingering spring inertia in Google Modeler.
     * 2. Handles single-click / tap edge cases (synthesizes a dot if no lateral movement occurred).
     * 3. Builds the final 2D closed polygon ribbon with end-caps.
     * 4. Clears transient live buffers and returns the immutable FinishedStrokeData snapshot.
     *
     * @return FinishedStrokeData The completed stroke package ready for layer / document commitment.
     */
    FinishedStrokeData FinishStroke();

    /**
     * @brief Discards the active in-flight stroke without generating output.
     * Called on palm rejection triggers, modal interruptions, or window blur.
     */
    void CancelStroke();

    // -------------------------------------------------------------------------
    // 2. LAYER INTERACTION & STATE QUERIES
    // -------------------------------------------------------------------------

    /**
     * @brief Queries whether an inking gesture is currently in progress.
     */
    [[nodiscard]] bool IsStrokeActive() const noexcept { return m_isStrokeActive; }
    [[nodiscard]] const BLPath& GetLiveOutlinePath() const noexcept { return m_liveStrokeOutline; }
    [[nodiscard]] const BLPath& GetPredictedOutlinePath() const noexcept { return m_predictedStrokeOutline; }
    [[nodiscard]] const PenTool& GetActiveTool() const noexcept { return m_activeTool; }

    // -------------------------------------------------------------------------
    // 3. CONFIGURATION & OUTLINE COMPUTATION AUTHORITY
    // -------------------------------------------------------------------------

    /**
     * @brief Returns a reference to the active inking configuration for reading or modifying settings.
     */
    [[nodiscard]] InkingConfig& GetConfig() noexcept;

    /**
     * @brief Centralized stroke outline computing authority.
     */
    static BLPath BuildStrokeOutline(const std::vector<Point2D>& points,
                                     CapType capType = CapType::Round,
                                     StrokePattern pattern = StrokePattern::Solid,
                                     bool isClosed = false) {
        return StrokeOutlineBuilder::FeedStrokeBatch(points, capType, pattern, isClosed);
    }

    /**
     * @brief Maps ShapeOutlineType into StrokePattern for unified shape/connector outline generation.
     */
    static StrokePattern MapShapeOutlineType(ShapeOutlineType outlineType) noexcept {
        return StrokeOutlineBuilder::MapShapeOutlineType(outlineType);
    }

private:
    /**
     * @brief Evaluates target stroke thickness combining pressure response and velocity thinning.
     *
     * Mathematical Working Process:
     * 1. Stage A (Pressure Ceiling):
     *    p_norm = clamp((pressure - deadzone) / (saturation - deadzone), 0, 1)
     *    p_curve = p_norm^gamma
     *    w_pressure = baseSize * (min_mult + p_curve * (max_mult - min_mult))
     *
     * 2. Stage B (Velocity Thinning):
     *    speed = sqrt(vx^2 + vy^2) * zoomScale
     *    v_norm = clamp(speed / max_speed, 0, 1)
     *    thin_factor = 1.0 - v_norm * (1.0 - min_mult)
     *
     * 3. Result:
     *    width = w_pressure * thin_factor
     */
    float CalculateDynamicWidth(const Point2D& pt, float& smoothedWidth) const;

    // Converts Google Ink smoothed output (Result) back into our unified Point2D struct (with EMA width evaluation)
    Point2D ToPoint2D(const ink::stroke_model::Result& res, float& smoothedWidth) const;

    // Processes confirmed Google Ink results into 1D segments and 2D live outline
    void ProcessGoogleResults(const std::vector<ink::stroke_model::Result>& results);

    // Builds temporary forward-predicted tip outline to reduce perceived latency
    void UpdateGooglePrediction();

    // Internal Inking State
    bool m_isStrokeActive = false;
    PenTool m_activeTool;
    InkingConfig m_config;
    float m_zoomScale = 1.0f;
    float m_smoothedWidth = -1.0f;

    // Point and Vector Geometry Buffers
    std::vector<Point2D> m_activeStrokePoints;
    std::vector<Point2D> m_liveModeledPoints;
    std::vector<ink::stroke_model::Result> m_googleResultsBuffer;
    std::vector<ink::stroke_model::Result> m_googlePredictionsBuffer;
    std::vector<Point2D> m_predictedPointsBuffer;

    BLPath m_liveStrokeOutline;
    BLPath m_predictedStrokeOutline;

    // Google Ink Modeler Bridge
    std::unique_ptr<ink::stroke_model::StrokeModeler> m_googleModeler;
};

} // namespace Folio
