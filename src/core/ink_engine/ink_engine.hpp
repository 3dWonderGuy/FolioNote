#pragma once

/**
 * @file ink_engine.hpp
 * @brief Universal inking physics, geometry synthesis, and presentation data engine.
 *
 * ARCHITECTURAL DESIGN:
 * ---------------------
 * InkEngine is a centralized, modular data and execution engine responsible for all
 * handwriting, dynamic width evaluation, predictive stroke modeling, and presentation
 * laser trails.
 *
 * Decoupling Contract:
 *  - CanvasEngine, PdfEngine, or MarkdownEditor ingest pointer events and route them into InkEngine.
 *  - The Layer Compositor (e.g. LiveInteractionLayer) interacts with InkEngine to query live stroke
 *    geometry, render predictive nib tips, or execute dirty-rect redraws.
 *  - On pen lift (PointerUp), InkEngine finalizes the stroke and hands an immutable FinishedStrokeData
 *    payload to the consumer layer / document session for database and spatial index persistence.
 */

#include <vector>
#include <memory>
#include <cstdint>
#include <chrono>
#include <blend2d/blend2d.h>

#include "input/pen_palette.hpp"
#include "core/spatial/aabb.hpp"
#include "core/ink_engine/stroke_smoother.hpp"
#include "core/ink_engine/stroke_outline_builder.hpp"

// Forward declarations for Google Ink Stroke Modeler
namespace ink::stroke_model {
    class StrokeModeler;
    struct Result;
}

namespace Folio {

/**
 * @struct EphemeralStroke
 * @brief Transient presentation stroke (e.g. Laser Pointer trail) with temporal quadratic fade.
 *
 * Mathematical Decay Model:
 * -------------------------
 * Given stroke start timestamp t0 (ms) and total lifetime duration T (ms),
 * at current time t:
 *   elapsed = clamp(t - t0, 0, T)
 *   progress = elapsed / T           ∈ [0.0, 1.0]
 *   alphaFactor = (1.0 - progress)^2  (Quadratic ease-out fade)
 *
 * The stroke is automatically pruned once elapsed >= T.
 */
struct EphemeralStroke {
    BLPath outlinePath;
    BLRgba32 baseColor;
    uint64_t startTimeMs = 0;
    uint32_t durationMs = 2500;
};

/**
 * @struct LiveInkSnapshot
 * @brief Read-only snapshot of current in-flight inking state for presentation layers.
 */
struct LiveInkSnapshot {
    bool isStrokeActive = false;
    BLPath confirmedOutline;          ///< Closed 2D polygon ribbon up to latest confirmed physics step
    BLPath predictedOutline;          ///< Low-latency forward-extrapolated tip polygon
    BLRgba32 strokeColor;             ///< Active tool pigment
    AABB dirtyBounds;                 ///< Combined bounding envelope in physical world millimeters
};

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

    /**
     * @brief Retrieves the active pen tool configuration for the current stroke.
     */
    [[nodiscard]] const PenTool& GetActiveTool() const noexcept { return m_activeTool; }

    /**
     * @brief Generates an immutable snapshot of the active stroke geometry.
     * Used by layer classes for compositing and calculating dirty invalidate rectangles.
     */
    [[nodiscard]] LiveInkSnapshot GetLiveSnapshot() const;

    /**
     * @brief Renders the confirmed live stroke and the predictive tip onto a Blend2D context.
     *
     * @param ctx Target Blend2D drawing context.
     */
    void RenderLiveStroke(BLContext& ctx) const;

    // -------------------------------------------------------------------------
    // 3. EPHEMERAL PRESENTATION INK (Laser Pointer)
    // -------------------------------------------------------------------------

    /**
     * @brief Registers a transient laser stroke that decays over time.
     *
     * @param path Outline geometry of the stroke.
     * @param color Pigment color.
     * @param durationMs Lifetime in milliseconds (default: 2500ms).
     */
    void AddEphemeralStroke(BLPath path, BLRgba32 color, uint32_t durationMs = 2500);

    /**
     * @brief Advances quadratic decay on all ephemeral presentation strokes.
     *
     * @param currentTimeMs Current monotonic timestamp in milliseconds.
     * @return bool True if any stroke is still active or changed opacity (requires frame redraw).
     */
    bool UpdateEphemeralStrokes(uint64_t currentTimeMs);

    /**
     * @brief Instantly clears all ephemeral laser strokes.
     */
    void ClearEphemeralStrokes();

    /**
     * @brief Renders all active laser strokes onto a Blend2D context with quadratic opacity decay.
     *
     * @param ctx Target Blend2D drawing context.
     * @param currentTimeMs Current monotonic timestamp in milliseconds.
     */
    void RenderEphemeralStrokes(BLContext& ctx, uint64_t currentTimeMs) const;

    /**
     * @brief Returns read-only access to all active ephemeral strokes.
     */
    [[nodiscard]] const std::vector<EphemeralStroke>& GetEphemeralStrokes() const noexcept {
        return m_ephemeralStrokes;
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
    float CalculateDynamicWidth(const ink::stroke_model::Result& res) const;

    void ProcessGoogleResults(const std::vector<ink::stroke_model::Result>& results);
    void UpdateGooglePrediction();

    // Internal Inking State
    bool m_isStrokeActive = false;
    PenTool m_activeTool;
    float m_zoomScale = 1.0f;
    float m_tiltX = 0.0f;
    float m_tiltY = 0.0f;
    float m_smoothedWidth = -1.0f;

    // Point and Vector Geometry Buffers
    std::vector<Point2D> m_activeStrokePoints;
    std::vector<Segment1D> m_fullSmoothedSegments;
    std::vector<StrokeOutlineBuilder::InputPoint> m_liveModeledPoints;
    std::vector<Segment1D> m_googlePredictedSegments;

    BLPath m_liveStrokeOutline;
    BLPath m_predictedStrokeOutline;

    // Ephemeral Laser Trails
    std::vector<EphemeralStroke> m_ephemeralStrokes;

    // Google Ink Modeler Bridge
    std::unique_ptr<ink::stroke_model::StrokeModeler> m_googleModeler;
    bool m_hasPreviousGooglePoint = false;
    Point2D m_previousGooglePos{0.0, 0.0};
};

} // namespace Folio
