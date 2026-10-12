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

// creates a segment using our struct
Segment1D MakeSegment(const Point2D& p0, const Point2D& p1, float width) {
    Segment1D seg;
    seg.p0 = p0;
    seg.p1 = p1;
    seg.width = width;
    return seg;
}

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
    m_config.google_use_tilt = tool.isTiltEnabled;
    m_zoomScale = zoomScale > 0.001f ? zoomScale : 1.0f;

    Point2D startPt;
    startPt.x = worldX;
    startPt.y = worldY;
    startPt.pressure = pressure;
    startPt.timeSeconds = timeSec;
    startPt.tiltX = tiltX;
    startPt.tiltY = tiltY;
    m_activeStrokePoints.push_back(startPt);

    // Configure Google Ink Stroke Modeler physics parameters
    sm::StrokeModelParams params;                                                      // creates pipeline
    params.position_modeler_params.spring_mass_constant = m_config.google_spring_mass_constant; // sets up spring/mass constant
    params.position_modeler_params.drag_constant = m_config.google_drag_constant;      // friction constant

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
    params.wobble_smoother_params.is_enabled = m_config.google_wobble_enable;          // wobble compensation
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

    // physics engine reset (void) we don't want anything returned and guaranteed to succeed
    (void)m_googleModeler->Reset(params);

    // Seed the first sample into the physics pipeline (inside the google ink)
    m_googleResultsBuffer.clear();
    if (m_googleModeler->Update(MakeGoogleInput(sm::Input::EventType::kDown, startPt), m_googleResultsBuffer).ok()) {
        ProcessGoogleResults(m_googleResultsBuffer);
    }

    // Seed live initial touch dot so user sees immediate visual feedback under pen tip
    if (m_liveStrokeOutline.is_empty() && !m_activeStrokePoints.empty()) {
        Point2D pt = m_activeStrokePoints.front();                                     // first touchdown point from digitizer
        float baseW = static_cast<float>(m_activeTool.baseSize);                       // finds base width from active tool
        pt.width = StrokeWidthEvaluator::CalculatePressureWidth(
            baseW, pt.pressure, pt.tiltX, pt.tiltY, m_config);                         // pressure & tilt sized width

        // delegate shape generation to StrokeOutlineBuilder (respects tool capType: round, square, chisel)
        m_liveStrokeOutline = StrokeOutlineBuilder::FeedStrokeBatch(
            { pt }, m_activeTool.capType, m_activeTool.strokePattern);
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
        if (m_googleModeler->Update(MakeGoogleInput(sm::Input::EventType::kUp, m_activeStrokePoints.back()), m_googleResultsBuffer).ok()) {
            ProcessGoogleResults(m_googleResultsBuffer);                               // let spring finish catching up to pen lift position
        }

        // Stage 2: Single-tap dot handling (user tapped without lateral dragging)
        if (m_liveModeledPoints.empty()) {
            Point2D pt = m_activeStrokePoints.front();                                 // tap origin
            float baseW = static_cast<float>(m_activeTool.baseSize);
            pt.width = StrokeWidthEvaluator::CalculatePressureWidth(
                baseW, pt.pressure, pt.tiltX, pt.tiltY, m_config);                     // evaluate tap pressure & tilt

            m_liveModeledPoints.push_back(pt);
        }
    }

    // Stage 3: Build collision segments and final vector stroke data once at the end
    FinishedStrokeData data;
    if (m_liveModeledPoints.size() == 1) {
        Point2D p0 = m_liveModeledPoints[0];
        Point2D p1 = p0;
        p1.x += 0.001;                                                                 // tiny epsilon offset to give a valid segment for eraser/bounds
        data.liveSegments.push_back(MakeSegment(p0, p1, p0.width));
    } else if (m_liveModeledPoints.size() >= 2) {
        data.liveSegments.reserve(m_liveModeledPoints.size() - 1);
        for (size_t i = 1; i < m_liveModeledPoints.size(); ++i) {
            data.liveSegments.push_back(
                MakeSegment(m_liveModeledPoints[i - 1], m_liveModeledPoints[i], m_liveModeledPoints[i].width));
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

float InkEngine::CalculateDynamicWidth(const Point2D& pt, float& smoothedWidth) const {
    float baseWidth = static_cast<float>(m_activeTool.baseSize);
    float targetWidth = StrokeWidthEvaluator::CalculateDynamicWidth(
        baseWidth, pt.pressure, pt.speed, m_zoomScale, pt.tiltX, pt.tiltY, m_config);
    smoothedWidth = StrokeWidthEvaluator::FilterEMA(
        smoothedWidth, targetWidth, m_config.google_dynamic_width_smoothing);          // EMA smooth width transitions
    return smoothedWidth;
}

// translates Google Ink's smoothed Result output back into our Point2D struct and evaluates width
Point2D InkEngine::ToPoint2D(const sm::Result& res, float& smoothedWidth) const {
    Point2D pt;
    pt.x = res.position.x;                                                             // smoothed X from google physics
    pt.y = res.position.y;                                                             // smoothed Y from google physics
    pt.pressure = res.pressure;                                                        // smoothed stylus pressure
    pt.timeSeconds = static_cast<double>(res.time.Value());                            // unwrap google Time back to double seconds
    pt.speed = std::hypot(res.velocity.x, res.velocity.y);                             // 2D velocity vector -> scalar speed
    pt.tiltX = (res.tilt >= 0.0f && res.orientation >= 0.0f) ? res.tilt * std::cos(res.orientation) : 0.0f;
    pt.tiltY = (res.tilt >= 0.0f && res.orientation >= 0.0f) ? res.tilt * std::sin(res.orientation) : 0.0f;
    pt.width = CalculateDynamicWidth(pt, smoothedWidth);                               // evaluate & EMA-smooth thickness
    return pt;
}

void InkEngine::ProcessGoogleResults(const std::vector<sm::Result>& results) {

    // convert incoming google batch into our Point2D (with smoothed thickness) and append to live stroke array
    for (const auto& res : results) {

        m_liveModeledPoints.push_back(ToPoint2D(res, m_smoothedWidth));
    }

    // rebuild the live 2D outline polygon from the entire stroke's points with safety check
    if (!m_liveModeledPoints.empty()) {

        m_liveStrokeOutline = StrokeOutlineBuilder::FeedStrokeBatch(m_liveModeledPoints, m_activeTool.capType, m_activeTool.strokePattern);
    }
}

void InkEngine::UpdateGooglePrediction() {
    
    m_predictedStrokeOutline.clear();
    if (m_liveModeledPoints.empty()) return;                                           // need at least one confirmed point to predict from

    m_googlePredictionsBuffer.clear();
    if (!m_googleModeler->Predict(m_googlePredictionsBuffer).ok() || m_googlePredictionsBuffer.empty()) return;

    float predWidth = m_smoothedWidth;                                                 // local copy so prediction doesn't mutate real EMA state

    // start prediction polygon from last confirmed point to avoid gap
    m_predictedPointsBuffer.clear();
    m_predictedPointsBuffer.push_back(m_liveModeledPoints.back());

    for (const auto& res : m_googlePredictionsBuffer) {
        m_predictedPointsBuffer.push_back(ToPoint2D(res, predWidth));
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

} // namespace Folio
