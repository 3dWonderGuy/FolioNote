#include "core/ink_engine/ink_engine.hpp"
#include "core/ink_engine/stroke_width_evaluator.hpp"
#include "core/ink_engine/stroke_outline_builder.hpp"

#include <ink_stroke_modeler/stroke_modeler.h>
#include <ink_stroke_modeler/params.h>
#include <algorithm>
#include <cmath>

namespace Folio {

namespace sm = ink::stroke_model;

namespace {

// translates our Point2D into Google Ink's Input packet
sm::Input MakeGoogleInput(sm::Input::EventType type, const Point2D& pt) {
    
    sm::Input input;                                                                   // empty google ink input packet
    input.event_type = type;                                                           // pen state (kDown, kMove, or kUp)
    input.position = { static_cast<float>(pt.x), static_cast<float>(pt.y) };           // convert double world mm to google float Vec2
    input.time = sm::Time(pt.timeSeconds);                                             // wrap timestamp into google Time struct
    input.pressure = pt.pressure;                                                      // stylus pressure [0.0, 1.0]
    input.tilt = std::hypot(pt.tiltX, pt.tiltY);                                       // polar tilt magnitude (>= 0)
    float angle = std::atan2(pt.tiltY, pt.tiltX);                                      // polar azimuth angle [-pi, pi]
    input.orientation = angle < 0.0f ? angle + 2.0f * static_cast<float>(M_PI) : angle;// wrap orientation into [0, 2*pi)
    return input;
}

} // namespace

InkEngine::InkEngine() {
    m_googleModeler = std::make_unique<sm::StrokeModeler>();

    // pre-allocate buffers once to prevent mid-stroke std::vector reallocations
    m_activeStrokePoints.reserve(4096);
    m_liveModeledPoints.reserve(8192);
    m_googleResultsBuffer.reserve(64);
    m_googlePredictionsBuffer.reserve(64);
    m_predictedPointsBuffer.reserve(64);

    UpdatePhysicsParams();
}

InkEngine::~InkEngine() = default;

InkEngine::InkEngine(InkEngine&&) noexcept = default;
InkEngine& InkEngine::operator=(InkEngine&&) noexcept = default;

// -----------------------------------------------------------------------------
// 1. INKING LIFECYCLE
// -----------------------------------------------------------------------------

void InkEngine::BeginStroke(double worldX, double worldY, float pressure, double timeSec,
                            const PenTool& tool, float zoomScale, float tiltX, float tiltY) {
    // Reset transient stroke accumulation buffers
    CancelStroke();

    m_isStrokeActive = true;
    m_activeTool = tool;
    m_config.useTilt = tool.isTiltEnabled;
    m_zoomScale = zoomScale > 0.001f ? zoomScale : 1.0f;

    Point2D startPt;
    startPt.x = worldX;
    startPt.y = worldY;
    startPt.pressure = pressure;
    startPt.timeSeconds = timeSec;
    startPt.tiltX = tiltX;
    startPt.tiltY = tiltY;
    m_activeStrokePoints.push_back(startPt);

    // Reset physics engine state for the new stroke (preserves configured physics parameters)
    (void)m_googleModeler->Reset();

    // Seed the first sample into the physics pipeline (inside the google ink)
    m_googleResultsBuffer.clear();
    if (m_googleModeler->Update(MakeGoogleInput(sm::Input::EventType::kDown, startPt), m_googleResultsBuffer).ok()) {
        ProcessGoogleResults(m_googleResultsBuffer);
    }

    // If Google's tremor filter buffered the initial touch, render an immediate visual dot
    // so the user sees instant feedback under the pen tip with zero display lag (SPECIAL CASE FOR ONEPOINT SITUATION)
    if (m_liveStrokeOutline.is_empty()) {
        Point2D dotPt = startPt;
        dotPt.width = StrokeWidthEvaluator::CalculateDynamicWidth(
            static_cast<float>(m_activeTool.baseSize), dotPt, -1.0f, m_zoomScale, m_config);

        m_liveStrokeOutline = StrokeOutlineBuilder::FeedStrokeBatch(
            { dotPt }, m_activeTool.capType, m_activeTool.strokePattern);
    }

    if (m_config.google_enable_prediction) {
        UpdateGooglePrediction();                                                      // predicts next points to reduce latency
    }
}

void InkEngine::AppendPoint(double worldX, double worldY, float pressure, double timeSec,
                            float tiltX, float tiltY) {
    // small check if the stroke is still even active
    if (!m_isStrokeActive) return;

    // pen position tracker
    Point2D pt;
    pt.x = worldX;
    pt.y = worldY;
    pt.pressure = pressure;
    pt.timeSeconds = timeSec;
    pt.tiltX = tiltX;
    pt.tiltY = tiltY;
    m_activeStrokePoints.push_back(pt);                                                // track raw movement history

    // feed moving stylus event into physics solver
    m_googleResultsBuffer.clear();
    if (m_googleModeler->Update(MakeGoogleInput(sm::Input::EventType::kMove, pt), m_googleResultsBuffer).ok()) {
        ProcessGoogleResults(m_googleResultsBuffer);                                   // convert physics outputs to renderable vector points
    }

    if (m_config.google_enable_prediction) {
        UpdateGooglePrediction();                                                      // refresh forward Kalman projection
    }
}

FinishedStrokeData InkEngine::FinishStroke() {
    
    // safety check if stroke was active
    if (!m_isStrokeActive) return {};

    // safety check if there is data in the stroke
    if (!m_activeStrokePoints.empty()) {

        // Stage 1: Pen-Up event to flush lingering spring inertia in physics modeler
        m_googleResultsBuffer.clear();
        // we explicitly tell google that we lifted the pen so it can finish the stroke
        if (m_googleModeler->Update(MakeGoogleInput(sm::Input::EventType::kUp, m_activeStrokePoints.back()), m_googleResultsBuffer).ok()) {
            ProcessGoogleResults(m_googleResultsBuffer);                               // let spring finish catching up to pen lift position
        }

        // Stage 2: (SPECIAL CASE) Single-tap dot handling (user tapped without lateral dragging)
        if (m_liveModeledPoints.empty()) {
            Point2D pt = m_activeStrokePoints.front();                                 // tap origin
            pt.width = StrokeWidthEvaluator::CalculateDynamicWidth(
                static_cast<float>(m_activeTool.baseSize), pt, -1.0f, m_zoomScale, m_config);
            m_liveModeledPoints.push_back(pt);
        }
    }

    // Stage 3: Build collision segments and final vector stroke data once at the end (used for lasso math and eraser detection)
    FinishedStrokeData data;
    if (m_liveModeledPoints.size() == 1) {

        // Single-point tap: create a micro-segment because zero-length lines have undefined normals and fail collision checks
        Point2D p0 = m_liveModeledPoints[0];
        Point2D p1 = p0;
        p1.x += 0.001;                                                                 // tiny 1-micron offset gives a valid bounding box and direction for R-Tree & eraser

        Segment1D seg;
        seg.p0 = p0;
        seg.p1 = p1;
        seg.width = p0.width;
        data.liveSegments.push_back(seg);

    } else if (m_liveModeledPoints.size() >= 2) {

        // Multi-point stroke: link each consecutive pair of modeled points into a linear collision capsule
        data.liveSegments.reserve(m_liveModeledPoints.size() - 1);                     // pre-allocate exact capacity (N points produce N-1 segments)
        for (size_t i = 1; i < m_liveModeledPoints.size(); ++i) {
            Segment1D seg;
            seg.p0 = m_liveModeledPoints[i - 1];
            seg.p1 = m_liveModeledPoints[i];
            seg.width = m_liveModeledPoints[i].width;
            data.liveSegments.push_back(seg);
        }
    }

    data.modeledPoints = m_liveModeledPoints;
    data.outlinePath = StrokeOutlineBuilder::FeedStrokeBatch(
        m_liveModeledPoints, m_activeTool.capType, m_activeTool.strokePattern);        // builds closed polygon ribbon for blend2d

    // Stage 4: Reset live state
    CancelStroke();                                                                    // wipe live inking buffers for next stroke

    return data;
}

void InkEngine::CancelStroke() {
    m_activeStrokePoints.clear();
    m_liveModeledPoints.clear();
    m_googleResultsBuffer.clear();
    m_googlePredictionsBuffer.clear();
    m_predictedPointsBuffer.clear();
    m_liveStrokeOutline.clear();
    m_predictedStrokeOutline.clear();
    m_smoothedWidth = -1.0f;
    m_isStrokeActive = false;
}

// -----------------------------------------------------------------------------
// 2. MATHEMATICAL THICKNESS & MODELING
// -----------------------------------------------------------------------------

// translates Google Ink's smoothed Result output back into our Point2D struct with dynamic thickness
Point2D InkEngine::ToPoint2D(const sm::Result& res, float previousWidth) const {

    Point2D pt;
    pt.x = res.position.x;                                                             // smoothed X from google physics
    pt.y = res.position.y;                                                             // smoothed Y from google physics
    pt.pressure = res.pressure;                                                        // smoothed stylus pressure
    pt.timeSeconds = static_cast<double>(res.time.Value());                            // unwrap google Time back to double seconds
    pt.speed = std::hypot(res.velocity.x, res.velocity.y);                             // 2D velocity vector -> scalar speed
    pt.tiltX = (res.tilt >= 0.0f && res.orientation >= 0.0f) ? res.tilt * std::cos(res.orientation) : 0.0f;
    pt.tiltY = (res.tilt >= 0.0f && res.orientation >= 0.0f) ? res.tilt * std::sin(res.orientation) : 0.0f;

    // calculate dynamic thickness with physics and internal EMA smoothing
    pt.width = StrokeWidthEvaluator::CalculateDynamicWidth(
        static_cast<float>(m_activeTool.baseSize), pt, previousWidth, m_zoomScale, m_config);

    return pt;
}

void InkEngine::ProcessGoogleResults(const std::vector<sm::Result>& results) {

    // convert incoming google batch directly into complete Point2Ds and append to live stroke array
    for (const auto& res : results) {
        Point2D pt = ToPoint2D(res, m_smoothedWidth);
        m_smoothedWidth = pt.width;
        m_liveModeledPoints.push_back(pt);
    }

    // rebuild the live 2D outline polygon from the entire stroke's points with safety check
    if (!m_liveModeledPoints.empty()) {
        m_liveStrokeOutline = StrokeOutlineBuilder::FeedStrokeBatch(m_liveModeledPoints, m_activeTool.capType, m_activeTool.strokePattern);
    }
}

void InkEngine::UpdateGooglePrediction() {
    
    // make sure it is cleary and valid (old prediction mustbe cleared)
    m_predictedStrokeOutline.clear();
    if (m_liveModeledPoints.empty()) return;                                           // need at least one confirmed point to predict from

    // reset google buffer another precheck
    m_googlePredictionsBuffer.clear();
    if (!m_googleModeler->Predict(m_googlePredictionsBuffer).ok() || m_googlePredictionsBuffer.empty()) return;

    float predWidth = m_smoothedWidth;                                                 // local copy so prediction doesn't mutate real EMA state

    // start prediction polygon from last confirmed point to avoid gap
    m_predictedPointsBuffer.clear();
    m_predictedPointsBuffer.push_back(m_liveModeledPoints.back());

    // converts google predicted points directly into complete Point2Ds with thickness
    for (const auto& res : m_googlePredictionsBuffer) {
        Point2D pt = ToPoint2D(res, predWidth);
        predWidth = pt.width;
        m_predictedPointsBuffer.push_back(pt);
    }

    // build predictive tip polygon (rendered ahead of stylus for zero latency feel)
    if (m_predictedPointsBuffer.size() >= 2) {
        m_predictedStrokeOutline = StrokeOutlineBuilder::FeedStrokeBatch(
            m_predictedPointsBuffer, m_activeTool.capType, m_activeTool.strokePattern);
    }
}

// -----------------------------------------------------------------------------
// 3. CONFIGURATION (Public Facade)
// -----------------------------------------------------------------------------

InkingConfig& InkEngine::GetConfig() noexcept {
    return m_config;
}

void InkEngine::SetConfig(const InkingConfig& config) {
    m_config = config;
    UpdatePhysicsParams();
}

void InkEngine::UpdatePhysicsParams() {
    if (!m_googleModeler) {
        return;
    }

    // Configure Google Ink Stroke Modeler physics parameters
    sm::StrokeModelParams params;
    params.position_modeler_params.spring_mass_constant = m_config.google_spring_mass_constant;
    params.position_modeler_params.drag_constant = m_config.google_drag_constant;

    // Speed-based loop contraction mitigation (prevents fast curves/loops from shrinking)
    auto& loopParams = params.position_modeler_params.loop_contraction_mitigation_params;
    loopParams.is_enabled = m_config.google_loop_mitigation_enable;
    if (loopParams.is_enabled) {
        float lowSpeed = std::max(0.0f, m_config.google_loop_speed_lower_bound);
        float highSpeed = std::max(lowSpeed, m_config.google_loop_speed_upper_bound);
        float lowStrength = std::clamp(m_config.google_loop_strength_at_lower_speed, 0.0f, 1.0f);
        float highStrength = std::clamp(m_config.google_loop_strength_at_upper_speed, 0.0f, lowStrength);
        loopParams.speed_lower_bound = lowSpeed;
        loopParams.speed_upper_bound = highSpeed;
        loopParams.interpolation_strength_at_speed_lower_bound = lowStrength;
        loopParams.interpolation_strength_at_speed_upper_bound = highStrength;
        loopParams.min_speed_sampling_window = sm::Duration(std::max(0.0f, m_config.google_loop_min_speed_window_s));
    }

    // Stylus normal projection (required when loop mitigation is enabled)
    params.stylus_state_modeler_params.use_stroke_normal_projection =
        m_config.google_use_stroke_normal_projection || m_config.google_loop_mitigation_enable;

    // Wobble smoother (hand tremor filter)
    params.wobble_smoother_params.is_enabled = m_config.google_wobble_enable;
    params.wobble_smoother_params.timeout = sm::Duration(std::max(0.0f, m_config.google_wobble_timeout_s));
    params.wobble_smoother_params.speed_floor = std::max(0.0f, m_config.google_wobble_speed_floor);
    params.wobble_smoother_params.speed_ceiling =
        std::max(params.wobble_smoother_params.speed_floor, m_config.google_wobble_speed_ceiling);

    // Sampling & rotational velocity upsampling
    params.sampling_params.min_output_rate = std::max(10.0, static_cast<double>(m_config.google_min_output_rate));
    params.sampling_params.end_of_stroke_stopping_distance =
        std::max(0.0001f, m_config.google_end_of_stroke_stopping_distance);
    params.sampling_params.end_of_stroke_max_iterations =
        std::clamp(m_config.google_end_of_stroke_max_iterations, 1, 1000);
    params.sampling_params.max_estimated_angle_to_traverse_per_input =
        m_config.google_enable_angle_upsampling
            ? std::clamp(static_cast<double>(m_config.google_max_estimated_angle_rad), 0.005, 3.10)
            : -1.0;

    // Initialize/re-initialize physics parameters
    (void)m_googleModeler->Reset(params);
}

} // namespace Folio
