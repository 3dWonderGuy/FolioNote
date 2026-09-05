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

    void AddStrokePoint(double worldXMm, double worldYMm, float pressure, double timeSec, float zoomScale = 1.0f, float tiltX = 0.0f, float tiltY = 0.0f) {
        if (!isStrokeActive || activeStrokePoints.empty()) return;
        currentZoomScale = zoomScale;
        currentTiltX = tiltX;
        currentTiltY = tiltY;

        Point2D currentPt{ worldXMm, worldYMm, pressure, timeSec, tiltX, tiltY };
        const Point2D& prevPt = activeStrokePoints.back();

        double dx = currentPt.x - prevPt.x;
        double dy = currentPt.y - prevPt.y;
        
        // 0.05 mm deadband filter to eliminate digitizer ADC jitter
        if ((dx * dx + dy * dy) < 0.0025) return;

        activeStrokePoints.push_back(currentPt);

        const size_t totalPts = activeStrokePoints.size();
        if (totalPts < 2) return;

        ink::stroke_model::Input input;
        input.event_type = ink::stroke_model::Input::EventType::kMove;
        input.position = { static_cast<float>(worldXMm), static_cast<float>(worldYMm) };
        input.time = ink::stroke_model::Time(timeSec);
        input.pressure = pressure;

        std::vector<ink::stroke_model::Result> results;
        if (googleModeler->Update(input, results).ok()) {
            ProcessGoogleResults(results);
        }

        if (g_InkingConfig.google_enable_prediction) {
            UpdateGooglePrediction();
        } else {
            googlePredictedSegments.clear();
            predictedStrokeOutline.clear();
        }
    }

    // Completes stroke and returns accumulated data for eventual saving
    FinishedStrokeData FinishStroke() {
        if (!isStrokeActive) return {};

        if (!activeStrokePoints.empty()) {
            ink::stroke_model::Input input;
            input.event_type = ink::stroke_model::Input::EventType::kUp;
            input.position = { static_cast<float>(activeStrokePoints.back().x), static_cast<float>(activeStrokePoints.back().y) };
            input.time = ink::stroke_model::Time(activeStrokePoints.back().timeSeconds);
            input.pressure = activeStrokePoints.back().pressure;

            std::vector<ink::stroke_model::Result> results;
            if (googleModeler->Update(input, results).ok()) {
                ProcessGoogleResults(results);
            }

            // If the user just tapped without moving, create a single dot
            if (liveModeledPoints.empty() && hasPreviousGooglePoint) {
                float w = static_cast<float>(activePenTool.baseSize);
                liveModeledPoints.push_back({ previousGoogleResultPos.x, previousGoogleResultPos.y, w });
                Segment1D dot;
                dot.p0 = Point2D{ previousGoogleResultPos.x, previousGoogleResultPos.y, 0, 0 };
                dot.p1 = dot.p0;
                dot.p1.x += 0.001;
                dot.width = w;
                fullSmoothedSegments.push_back(dot);
            }
        }

        FinishedStrokeData data;
        data.rawPoints = std::move(activeStrokePoints);
        data.liveSegments = std::move(fullSmoothedSegments);
        data.modeledPoints = liveModeledPoints;
        data.outlinePath = StrokeOutlineBuilder::BuildOutline(liveModeledPoints, activePenTool.capType);
        
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
    float CalculateDynamicWidth(const ink::stroke_model::Result& res) const {
        float baseWidth = static_cast<float>(activePenTool.baseSize);

        // ---------------------------------------------------------------
        // STAGE A: Pressure → target width
        // Use EvaluatePressure() for the proper gamma-curved, deadzone-
        // clamped mapping. The result is a fraction in [minThickness, 1.0]
        // which we remap to [min_width_multiplier, max_width_multiplier].
        // ---------------------------------------------------------------
        float pressureWidth = baseWidth; // default: full width if pressure disabled
        if (g_InkingConfig.google_use_pressure && res.pressure >= 0.0f) {
            // EvaluatePressure returns a value in [minRelativeThicknessFraction, 1.0]
            // We then scale that into the user's configured [min, max] width range.
            float pressureCurved = g_InkingConfig.EvaluatePressure(res.pressure);

            // pressureCurved is in [minRelativeThicknessFraction..1.0]
            // Remap to [min_width_multiplier..max_width_multiplier]
            float minFrac = g_InkingConfig.google_min_width_multiplier;
            float maxFrac = g_InkingConfig.google_max_width_multiplier;
            float widthMultiplier = minFrac + pressureCurved * (maxFrac - minFrac);
            pressureWidth = baseWidth * std::clamp(widthMultiplier, minFrac, maxFrac);
        }

        // ---------------------------------------------------------------
        // STAGE B: Velocity → thin DOWN from the pressure-determined width
        // The user intent: pressure selects the "intended" stroke weight.
        // Speed only thins from that — it never makes the line thicker.
        //   speed = 0      → thinFactor = 1.0  (no thinning, full pressure weight)
        //   speed = maxSpd → thinFactor = minThickness  (maximum thinning)
        // This models natural ink: slow deliberate strokes are fat, fast
        // flicks taper off. Pressure is always the ceiling.
        // ---------------------------------------------------------------
        float velocityThinFactor = 1.0f;
        if (g_InkingConfig.google_use_velocity) {
            float speed = std::hypot(res.velocity.x, res.velocity.y) * currentZoomScale;
            float normalizedSpeed = std::clamp(
                speed / g_InkingConfig.google_velocity_thinning_max_speed, 0.0f, 1.0f);
            // At speed=0 → factor=1.0 (full width). At speed=max → factor=minThickness fraction.
            float minThin = g_InkingConfig.google_min_width_multiplier;
            velocityThinFactor = 1.0f - normalizedSpeed * (1.0f - minThin);
        }

        return pressureWidth * velocityThinFactor;
    }

    void ProcessGoogleResults(const std::vector<ink::stroke_model::Result>& results) {
        for (const auto& res : results) {
            float targetWidth = CalculateDynamicWidth(res);
            if (currentSmoothedWidth < 0.0f) {
                currentSmoothedWidth = targetWidth; // initialize on first point
            } else {
                currentSmoothedWidth = std::lerp(currentSmoothedWidth, targetWidth, g_InkingConfig.google_dynamic_width_smoothing);
            }

            if (hasPreviousGooglePoint) {
                Segment1D seg;
                seg.p0 = Point2D{ previousGoogleResultPos.x, previousGoogleResultPos.y, 0, 0 };
                seg.p1 = Point2D{ res.position.x, res.position.y, 0, 0 };
                seg.width = currentSmoothedWidth;
                fullSmoothedSegments.push_back(seg);
            }

            float speed = std::hypot(res.velocity.x, res.velocity.y);
            liveModeledPoints.push_back({ res.position.x, res.position.y, currentSmoothedWidth, res.pressure, speed, currentTiltX, currentTiltY });

            previousGoogleResultPos = res.position;
            hasPreviousGooglePoint = true;
        }

        if (!liveModeledPoints.empty()) {
            liveStrokeOutline = StrokeOutlineBuilder::BuildOutline(liveModeledPoints, activePenTool.capType);
        }
    }

    void UpdateGooglePrediction() {
        googlePredictedSegments.clear();
        predictedStrokeOutline.clear();
        if (!hasPreviousGooglePoint) return;

        std::vector<ink::stroke_model::Result> predictions;
        if (googleModeler->Predict(predictions).ok() && !predictions.empty()) {
            Point2D prev = { previousGoogleResultPos.x, previousGoogleResultPos.y, 0, 0 };
            float predictionSmoothedWidth = currentSmoothedWidth;
            
            std::vector<StrokeOutlineBuilder::InputPoint> predPoints;
            if (!liveModeledPoints.empty()) {
                predPoints.push_back(liveModeledPoints.back());
            }

            for (const auto& res : predictions) {
                float targetWidth = CalculateDynamicWidth(res);
                if (predictionSmoothedWidth < 0.0f) {
                    predictionSmoothedWidth = targetWidth;
                } else {
                    predictionSmoothedWidth = std::lerp(predictionSmoothedWidth, targetWidth, g_InkingConfig.google_dynamic_width_smoothing);
                }

                Segment1D seg;
                seg.p0 = prev;
                seg.p1 = Point2D{ res.position.x, res.position.y, 0, 0 };
                seg.width = predictionSmoothedWidth;
                googlePredictedSegments.push_back(seg);
                prev = seg.p1;

                float speed = std::hypot(res.velocity.x, res.velocity.y);
                predPoints.push_back({ res.position.x, res.position.y, predictionSmoothedWidth, res.pressure, speed, currentTiltX, currentTiltY });
            }

            if (predPoints.size() >= 2) {
                predictedStrokeOutline = StrokeOutlineBuilder::BuildOutline(predPoints, activePenTool.capType);
            }
        }
    }
public:

    // -----------------------------------------------------------------
    // LASSO LIFECYCLE (All coordinates in mm)
    // -----------------------------------------------------------------
    void BeginLasso(double worldXMm, double worldYMm) {
        activeType = LiveLayerType::Lasso;
        isLassoActive = true;
        activeLassoPoints.clear();
        activeLassoPoints.push_back(Point2D{ worldXMm, worldYMm, 1.0f, 0.0 });
    }

    void AddLassoPoint(double worldXMm, double worldYMm) {
        if (!isLassoActive || activeLassoPoints.empty()) return;

        Point2D pt{ worldXMm, worldYMm, 1.0f, 0.0 };
        // 0.5 mm spacing between lasso polygon vertices
        if (std::hypot(pt.x - activeLassoPoints.back().x, pt.y - activeLassoPoints.back().y) < 0.5) return;
        activeLassoPoints.push_back(pt);
    }

    std::vector<Point2D> FinishLasso() {
        if (!isLassoActive) return {};
        std::vector<Point2D> finishedLasso = std::move(activeLassoPoints);
        activeLassoPoints.clear();
        isLassoActive = false;
        activeType = LiveLayerType::None;
        return finishedLasso;
    }

    [[nodiscard]] bool HasActiveData() const noexcept {
        return isStrokeActive || isLassoActive || (activeType != LiveLayerType::None);
    }
};