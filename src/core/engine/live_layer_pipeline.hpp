#pragma once

#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>
#include <blend2d/blend2d.h>
#include "input/pen_palette.hpp"
#include "core/engine/stroke_smoother.hpp"
#include "core/engine/stroke_outline_builder.hpp"
#include "ui/components/tuning_overlay.hpp"
#include <ink_stroke_modeler/stroke_modeler.h>
#include <ink_stroke_modeler/params.h>

// type of interaction with live layer
enum class LiveLayerType {
    None,
    Inking,
    Erasing,
    Lasso,
    Transforming,
    ImagePlacement,
    Video,
    PDF
};

struct LiveDrawSegment {
    Point2D p0;
    Point2D p1;
    float width; // Physical millimeters (mm)
    BLRgba32 color;
};

struct FinishedStrokeData {
    std::vector<Point2D> rawPoints;
    std::vector<Segment1D> liveSegments;
    std::vector<StrokeOutlineBuilder::InputPoint> modeledPoints;
    BLPath outlinePath;
};

class LiveLayerPipeline {
public:
    LiveLayerType activeType = LiveLayerType::None;

    // Accumulation buffers during active stroke
    std::vector<Point2D> activeStrokePoints;
    std::vector<Segment1D> fullSmoothedSegments;
    std::vector<StrokeOutlineBuilder::InputPoint> liveModeledPoints;
    BLPath liveStrokeOutline;
    BLPath predictedStrokeOutline;
    PenTool activePenTool;
    bool isStrokeActive = false;

    // Active lasso polygon state
    std::vector<Point2D> activeLassoPoints;
    bool isLassoActive = false;

    // Google Ink State
    std::unique_ptr<ink::stroke_model::StrokeModeler> googleModeler;
    bool hasPreviousGooglePoint = false;
    ink::stroke_model::Vec2 previousGoogleResultPos;
    std::vector<Segment1D> googlePredictedSegments;
    float currentSmoothedWidth = -1.0f;
    float currentZoomScale = 1.0f;
    float currentTiltX = 0.0f;
    float currentTiltY = 0.0f;

    LiveLayerPipeline() {
        googleModeler = std::make_unique<ink::stroke_model::StrokeModeler>();
    }

    // -------------------------------------------------------------
    // INKING LIFECYCLE (All coordinates in mm)
    // -------------------------------------------------------------
    void BeginStroke(double worldXMm, double worldYMm, float pressure, double timeSec, const PenTool& tool, float zoomScale = 1.0f, float tiltX = 0.0f, float tiltY = 0.0f) {
        activeType = LiveLayerType::Inking;
        isStrokeActive = true;
        activePenTool = tool;
        currentZoomScale = zoomScale;
        currentTiltX = tiltX;
        currentTiltY = tiltY;

        // proper reset
        activeStrokePoints.clear();
        fullSmoothedSegments.clear();
        liveModeledPoints.clear();
        liveStrokeOutline.clear();
        predictedStrokeOutline.clear();
        googlePredictedSegments.clear();
        hasPreviousGooglePoint = false;
        currentSmoothedWidth = -1.0f;

        Point2D startPt{ worldXMm, worldYMm, pressure, timeSec, tiltX, tiltY };
        activeStrokePoints.push_back(startPt);

        ink::stroke_model::StrokeModelParams params;
        params.position_modeler_params.spring_mass_constant = g_InkingConfig.google_spring_mass_constant;
        params.position_modeler_params.drag_constant = g_InkingConfig.google_drag_constant;
        params.wobble_smoother_params.is_enabled = g_InkingConfig.google_wobble_enable;
        params.wobble_smoother_params.timeout = ink::stroke_model::Duration(g_InkingConfig.google_wobble_timeout_s);
        params.wobble_smoother_params.speed_floor = g_InkingConfig.google_wobble_speed_floor;
        params.wobble_smoother_params.speed_ceiling = g_InkingConfig.google_wobble_speed_ceiling;
        params.sampling_params.min_output_rate = 120.0;
        params.sampling_params.end_of_stroke_stopping_distance = 0.01f;

        (void)googleModeler->Reset(params);

        ink::stroke_model::Input input;
        input.event_type = ink::stroke_model::Input::EventType::kDown;
        input.position = { static_cast<float>(worldXMm), static_cast<float>(worldYMm) };
        input.time = ink::stroke_model::Time(timeSec);
        input.pressure = pressure;

        std::vector<ink::stroke_model::Result> results;
        if (googleModeler->Update(input, results).ok()) {
            ProcessGoogleResults(results);
        }

        if (g_InkingConfig.google_enable_prediction) {
            UpdateGooglePrediction();
        }
    }

    /**
 * @brief Ingests continuous stylus motion telemetry during an active drawing stroke.
 * 
 * Flow & Responsibilities:
 * 1. State Validation: Ensures an active stroke lifecycle has begun.
 * 2. Telemetry Ingestion: Appends raw world-coordinate samples (including pressure and tilt).
 * 3. Modeler Integration: Packages the event into an `ink::stroke_model::Input` packet
 *    and triggers `googleModeler->Update()` to compute spring-mass-damper physics.
 * 4. Result Processing: Dispatches modeled output positions and dynamic widths to the active buffer.
 * 5. Latency Prediction: Optionally generates predictive trajectory points to reduce screen-to-pen lag.
 * 
 * @param worldXMm    Pen tip X coordinate in physical world millimeters (mm).
 * @param worldYMm    Pen tip Y coordinate in physical world millimeters (mm).
 * @param pressure    Normalized stylus tip force in range [0.0, 1.0].
 * @param timeSec     Monotonic event timestamp in seconds.
 * @param zoomScale   Active viewport scale factor (used for speed-thinning calculations).
 * @param tiltX       Stylus X tilt angle (telemetry reserved for brush footprint rotation).
 * @param tiltY       Stylus Y tilt angle (telemetry reserved for brush footprint rotation).
 */
void AddStrokePoint(double worldXMm, double worldYMm, float pressure, double timeSec, 
                    float zoomScale = 1.0f, float tiltX = 0.0f, float tiltY = 0.0f) {
    // -------------------------------------------------------------------------
    // STAGE 1: ACTIVE STROKE VALIDATION
    // -------------------------------------------------------------------------
    // Abort if no stroke was initialized via BeginStroke(), or if the point buffer
    // is uninitialized. This defends against spurious OS move events without a prior down event.
    if (!isStrokeActive || activeStrokePoints.empty()) return;
    
    // Cache the active rendering state for downstream width and tilt evaluators
    currentZoomScale = zoomScale;
    currentTiltX = tiltX;
    currentTiltY = tiltY;

    // -------------------------------------------------------------------------
    // STAGE 2: RAW TELEMETRY CAPTURE
    // -------------------------------------------------------------------------
    // Store the unaltered world-space coordinate in the raw point buffer.
    // Note: Manual deadband distance filtering is omitted here because Google Ink's
    // internal WobbleSmoother handles quantization noise and micro-tremors natively.
    Point2D currentPt{ worldXMm, worldYMm, pressure, timeSec, tiltX, tiltY };
    activeStrokePoints.push_back(currentPt);

    // -------------------------------------------------------------------------
    // STAGE 3: GOOGLE INK STROKE MODELER DISPATCH
    // -------------------------------------------------------------------------
    // Translate the raw telemetry into a Google Ink input event.
    // Coordinates are passed in physical millimeters (mm) so physics constants
    // remain invariant across different screen DPIs and zoom factors.
    ink::stroke_model::Input input;
    input.event_type = ink::stroke_model::Input::EventType::kMove;
    input.position = { static_cast<float>(worldXMm), static_cast<float>(worldYMm) };
    input.time = ink::stroke_model::Time(timeSec);
    input.pressure = pressure;

    // Update the spring-mass-damper physics simulation.
    // This solves the 2nd-order ODE and samples intermediate smoothed vertices.
    std::vector<ink::stroke_model::Result> results;
    if (googleModeler->Update(input, results).ok()) {
        // Compute dynamic widths (pressure + velocity) and append modeled vertices
        ProcessGoogleResults(results);
    }

    // -------------------------------------------------------------------------
    // STAGE 4: LATENCY PREDICTION PASS
    // -------------------------------------------------------------------------
    // If prediction is enabled, extrapolate 1-3 frames into the future along
    // the modeled velocity vector to visually extend ink directly beneath the physical pen nib.
    if (g_InkingConfig.google_enable_prediction) {
        UpdateGooglePrediction();
    } else {
        googlePredictedSegments.clear();
        predictedStrokeOutline.clear();
    }
}
/**
 * =========================================================================================
 * @file live_layer_pipeline.hpp
 * @brief Live Ingestion, Modeler Integration, and In-Flight Interaction Pipeline
 * =========================================================================================
 */

/**
 * @brief Completes the active drawing stroke, flushes remaining physics samples,
 *        and packages vector data for persistent storage handoff.
 * 
 * Flow & Responsibilities:
 * 1. Flushes the Google Modeler with an `kUp` event to solve remaining spring inertia.
 * 2. Handles tap/dot edge cases where no movement occurred between down and up.
 * 3. Packages raw points, modeled samples, and the final 2D closed polygon outline.
 * 4. Resets all transient accumulation buffers to prevent state leakage into the next stroke.
 * 
 * @return FinishedStrokeData Container holding all geometric and telemetry records.
 */
FinishedStrokeData FinishStroke() {
    // Abort if no stroke was active (defends against spurious up events)
    if (!isStrokeActive) return {};

    if (!activeStrokePoints.empty()) {
        // ---------------------------------------------------------------------
        // STAGE 1: PHYSICS FLUSH (PEN-UP EVENT)
        // ---------------------------------------------------------------------
        // Feed the final Up event to Google Ink. The physics modeler will solve
        // remaining velocity and bring the virtual spring tip to a complete rest.
        ink::stroke_model::Input input;
        input.event_type = ink::stroke_model::Input::EventType::kUp;
        input.position = { 
            static_cast<float>(activeStrokePoints.back().x), 
            static_cast<float>(activeStrokePoints.back().y) 
        };
        input.time = ink::stroke_model::Time(activeStrokePoints.back().timeSeconds);
        input.pressure = activeStrokePoints.back().pressure;

        std::vector<ink::stroke_model::Result> results;
        if (googleModeler->Update(input, results).ok()) {
            ProcessGoogleResults(results);
        }

        // ---------------------------------------------------------------------
        // STAGE 2: SINGLE-TAP / DOT HANDLING
        // ---------------------------------------------------------------------
        // If the user tapped the screen without moving, the position modeler may
        // not emit intermediate segment steps. We construct an explicit micro-segment
        // so the stroke registers as a circular dot.
        if (liveModeledPoints.empty() && hasPreviousGooglePoint) {
            float w = static_cast<float>(activePenTool.baseSize);
            liveModeledPoints.push_back({ 
                previousGoogleResultPos.x, 
                previousGoogleResultPos.y, 
                w, 
                1.0f, 
                0.0f, 
                currentTiltX, 
                currentTiltY 
            });

            Segment1D dot;
            dot.p0 = Point2D{ previousGoogleResultPos.x, previousGoogleResultPos.y, 0, 0 };
            dot.p1 = dot.p0;
            dot.p1.x += 0.001; // Minimal length delta to establish a direction vector
            dot.width = w;
            fullSmoothedSegments.push_back(dot);
        }
    }

    // -------------------------------------------------------------------------
    // STAGE 3: VECTOR DATA PACKAGING
    // -------------------------------------------------------------------------
    // Move transient buffers into the immutable FinishedStrokeData snapshot.
    // The final outline polygon is generated once using the chosen cap geometry.
    FinishedStrokeData data;
    data.rawPoints = std::move(activeStrokePoints);
    data.liveSegments = std::move(fullSmoothedSegments);
    data.modeledPoints = liveModeledPoints;
    data.outlinePath = StrokeOutlineBuilder::BuildOutline(liveModeledPoints, activePenTool.capType);
    
    // -------------------------------------------------------------------------
    // STAGE 4: STATE RESET & CLEANUP
    // -------------------------------------------------------------------------
    // Clear all stroke buffers and reset tracking flags for the next gesture.
    activeStrokePoints.clear();
    fullSmoothedSegments.clear();
    liveModeledPoints.clear();
    liveStrokeOutline.clear();
    predictedStrokeOutline.clear();
    googlePredictedSegments.clear();
    hasPreviousGooglePoint = false;
    currentSmoothedWidth = -1.0f;
    currentTiltX = 0.0f;
    currentTiltY = 0.0f;
    isStrokeActive = false;
    activeType = LiveLayerType::None;

    return data;
}

/**
 * @brief Aborts the active stroke immediately and discards all pending points without saving.
 *        Used when input is canceled by the OS (e.g. window focus loss, palm rejection gesture).
 */
void CancelStroke() {
    activeStrokePoints.clear();
    fullSmoothedSegments.clear();
    liveModeledPoints.clear();
    liveStrokeOutline.clear();
    predictedStrokeOutline.clear();
    googlePredictedSegments.clear();
    hasPreviousGooglePoint = false;
    currentSmoothedWidth = -1.0f;
    currentTiltX = 0.0f;
    currentTiltY = 0.0f;
    isStrokeActive = false;
    activeType = LiveLayerType::None;
}

private:
/**
 * @brief Evaluates the target stroke width in millimeters based on pressure curves and velocity thinning.
 * 
 * Width Evaluation Architecture:
 * 1. Base Size: Initial nominal line width configured on the active PenTool (mm).
 * 2. Pressure Evaluation (Stage A): Maps normalized pressure [0.0, 1.0] through the user's
 *    configured gamma curve to determine the baseline stroke thickness ceiling.
 * 3. Velocity Thinning (Stage B): High-speed flicks reduce the stroke thickness down toward
 *    the minimum multiplier, simulating natural fluid ink depletion on quick gestures.
 * 
 * @param res Output result packet from Google Ink containing modeled position, velocity, and pressure.
 * @return float Final calculated width in physical world millimeters (mm).
 */
float CalculateDynamicWidth(const ink::stroke_model::Result& res) const {
    float baseWidth = static_cast<float>(activePenTool.baseSize);

    // -------------------------------------------------------------------------
    // STAGE A: PRESSURE → TARGET THICKNESS CEILING
    // -------------------------------------------------------------------------
    float pressureWidth = baseWidth; // Fallback: full nominal width if pressure is disabled
    if (g_InkingConfig.google_use_pressure && res.pressure >= 0.0f) {
        // EvaluatePressure applies user gamma curves and deadband cutoffs
        float pressureCurved = g_InkingConfig.EvaluatePressure(res.pressure);

        // Remap the curved [0.0, 1.0] response into the configured multiplier range [min, max]
        float minFrac = g_InkingConfig.google_min_width_multiplier;
        float maxFrac = g_InkingConfig.google_max_width_multiplier;
        float widthMultiplier = minFrac + pressureCurved * (maxFrac - minFrac);
        pressureWidth = baseWidth * std::clamp(widthMultiplier, minFrac, maxFrac);
    }

    // -------------------------------------------------------------------------
    // STAGE B: VELOCITY THINNING
    // -------------------------------------------------------------------------
    // Speed acts strictly as a thinning factor applied against the pressure ceiling.
    // Speed = 0        → thinFactor = 1.0 (Full width dictated by pressure)
    // Speed = MaxSpeed → thinFactor = min_width_multiplier (Maximal tapering)
    float velocityThinFactor = 1.0f;
    if (g_InkingConfig.google_use_velocity) {
        // Velocity magnitude scaled by screen zoom to normalize physical drawing speed
        float speed = std::hypot(res.velocity.x, res.velocity.y) * currentZoomScale;
        float normalizedSpeed = std::clamp(
            speed / g_InkingConfig.google_velocity_thinning_max_speed, 0.0f, 1.0f);

        float minThin = g_InkingConfig.google_min_width_multiplier;
        velocityThinFactor = 1.0f - normalizedSpeed * (1.0f - minThin);
    }

    return pressureWidth * velocityThinFactor;
}

/**
 * @brief Ingests modeled vertices from Google Ink, smooths dynamic thickness transitions,
 *        and updates the active 2D ribbon polygon outline.
 * 
 * @param results Collection of solved sample points emitted by Google Ink.
 */
void ProcessGoogleResults(const std::vector<ink::stroke_model::Result>& results) {
    for (const auto& res : results) {
        float targetWidth = CalculateDynamicWidth(res);

        // Exponential Moving Average (EMA) filter on width to eliminate single-frame thickness popping
        if (currentSmoothedWidth < 0.0f) {
            currentSmoothedWidth = targetWidth; // Seed filter on first sample
        } else {
            currentSmoothedWidth = std::lerp(
                currentSmoothedWidth, targetWidth, g_InkingConfig.google_dynamic_width_smoothing);
        }

        // Build continuous 1D line segments for hit-testing and legacy serialization
        if (hasPreviousGooglePoint) {
            Segment1D seg;
            seg.p0 = Point2D{ previousGoogleResultPos.x, previousGoogleResultPos.y, 0, 0 };
            seg.p1 = Point2D{ res.position.x, res.position.y, 0, 0 };
            seg.width = currentSmoothedWidth;
            fullSmoothedSegments.push_back(seg);
        }

        // Store modeled point with velocity and orientation metadata
        float speed = std::hypot(res.velocity.x, res.velocity.y);
        liveModeledPoints.push_back({ 
            res.position.x, 
            res.position.y, 
            currentSmoothedWidth, 
            res.pressure, 
            speed, 
            currentTiltX, 
            currentTiltY 
        });

        previousGoogleResultPos = res.position;
        hasPreviousGooglePoint = true;
    }

    // Rebuild the unified 2D vector ribbon path for live frame rendering
    if (!liveModeledPoints.empty()) {
        liveStrokeOutline = StrokeOutlineBuilder::BuildOutline(liveModeledPoints, activePenTool.capType);
    }
}

/**
 * @brief Extrapolates future pointer positions to render a low-latency predictive tip.
 * 
 * Uses Google Ink's internal velocity and acceleration state to synthesize 1-3 future frames,
 * rendering a seamless extension under the stylus nib that updates each frame.
 */
void UpdateGooglePrediction() {
    googlePredictedSegments.clear();
    predictedStrokeOutline.clear();
    if (!hasPreviousGooglePoint) return;

    std::vector<ink::stroke_model::Result> predictions;
    if (googleModeler->Predict(predictions).ok() && !predictions.empty()) {
        Point2D prev = { previousGoogleResultPos.x, previousGoogleResultPos.y, 0, 0 };
        float predictionSmoothedWidth = currentSmoothedWidth;
        
        std::vector<StrokeOutlineBuilder::InputPoint> predPoints;
        // Stitch the predicted path seamlessly to the last confirmed modeled vertex
        if (!liveModeledPoints.empty()) {
            predPoints.push_back(liveModeledPoints.back());
        }

        for (const auto& res : predictions) {
            float targetWidth = CalculateDynamicWidth(res);
            if (predictionSmoothedWidth < 0.0f) {
                predictionSmoothedWidth = targetWidth;
            } else {
                predictionSmoothedWidth = std::lerp(
                    predictionSmoothedWidth, targetWidth, g_InkingConfig.google_dynamic_width_smoothing);
            }

            Segment1D seg;
            seg.p0 = prev;
            seg.p1 = Point2D{ res.position.x, res.position.y, 0, 0 };
            seg.width = predictionSmoothedWidth;
            googlePredictedSegments.push_back(seg);
            prev = seg.p1;

            float speed = std::hypot(res.velocity.x, res.velocity.y);
            predPoints.push_back({ 
                res.position.x, 
                res.position.y, 
                predictionSmoothedWidth, 
                res.pressure, 
                speed, 
                currentTiltX, 
                currentTiltY 
            });
        }

        // Build the transient predicted polygon contour
        if (predPoints.size() >= 2) {
            predictedStrokeOutline = StrokeOutlineBuilder::BuildOutline(predPoints, activePenTool.capType);
        }
    }
}

public:
// -----------------------------------------------------------------------------
// LASSO INTERACTION LIFECYCLE (All coordinates in mm)
// -----------------------------------------------------------------------------

/**
 * @brief Initializes a freeform lasso selection contour.
 */
void BeginLasso(double worldXMm, double worldYMm) {
    activeType = LiveLayerType::Lasso;
    isLassoActive = true;
    activeLassoPoints.clear();
    activeLassoPoints.push_back(Point2D{ worldXMm, worldYMm, 1.0f, 0.0 });
}

/**
 * @brief Appends vertices to the active lasso boundary, filtering micro-movements (< 0.5 mm).
 */
void AddLassoPoint(double worldXMm, double worldYMm) {
    if (!isLassoActive || activeLassoPoints.empty()) return;

    Point2D pt{ worldXMm, worldYMm, 1.0f, 0.0 };
    // Maintain a 0.5 mm minimum distance between vertices to bound polygon complexity
    if (std::hypot(pt.x - activeLassoPoints.back().x, pt.y - activeLassoPoints.back().y) < 0.5) return;
    activeLassoPoints.push_back(pt);
}

/**
 * @brief Finalizes the lasso gesture and returns the boundary polygon for spatial intersection testing.
 */
std::vector<Point2D> FinishLasso() {
    if (!isLassoActive) return {};
    std::vector<Point2D> finishedLasso = std::move(activeLassoPoints);
    activeLassoPoints.clear();
    isLassoActive = false;
    activeType = LiveLayerType::None;
    return finishedLasso;
}

/**
 * @brief Queries whether any live interaction (stroke or lasso) is currently capturing input.
 */
[[nodiscard]] bool HasActiveData() const noexcept {
    return isStrokeActive || isLassoActive || (activeType != LiveLayerType::None);
}
};