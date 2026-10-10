#include "core/ink_engine/ink_engine.hpp"
#include "ui/overlays/tuning_overlay.hpp"

#include <ink_stroke_modeler/stroke_modeler.h>
#include <ink_stroke_modeler/params.h>
#include <algorithm>
#include <cmath>

namespace Folio {

InkEngine::InkEngine() {
    m_googleModeler = std::make_unique<ink::stroke_model::StrokeModeler>();
}

InkEngine::~InkEngine() = default;

InkEngine::InkEngine(InkEngine&&) noexcept = default;
InkEngine& InkEngine::operator=(InkEngine&&) noexcept = default;

// -----------------------------------------------------------------------------
// 1. INKING LIFECYCLE
// -----------------------------------------------------------------------------

void InkEngine::BeginStroke(double worldX, double worldY, float pressure, double timeSec,
                            const PenTool& tool, float zoomScale, float tiltX, float tiltY) {
    m_isStrokeActive = true;
    m_activeTool = tool;
    m_zoomScale = zoomScale > 0.001f ? zoomScale : 1.0f;
    m_tiltX = tiltX;
    m_tiltY = tiltY;

    // Reset transient stroke accumulation buffers
    m_activeStrokePoints.clear();
    m_fullSmoothedSegments.clear();
    m_liveModeledPoints.clear();
    m_liveStrokeOutline.clear();
    m_predictedStrokeOutline.clear();
    m_googlePredictedSegments.clear();
    m_hasPreviousGooglePoint = false;
    m_smoothedWidth = -1.0f;

    Point2D startPt{ worldX, worldY, pressure, timeSec, tiltX, tiltY };
    m_activeStrokePoints.push_back(startPt);

    // Configure Google Ink Stroke Modeler physics parameters
    ink::stroke_model::StrokeModelParams params;
    params.position_modeler_params.spring_mass_constant = g_InkingConfig.google_spring_mass_constant;
    params.position_modeler_params.drag_constant = g_InkingConfig.google_drag_constant;
    params.wobble_smoother_params.is_enabled = g_InkingConfig.google_wobble_enable;
    params.wobble_smoother_params.timeout = ink::stroke_model::Duration(g_InkingConfig.google_wobble_timeout_s);
    params.wobble_smoother_params.speed_floor = g_InkingConfig.google_wobble_speed_floor;
    params.wobble_smoother_params.speed_ceiling = g_InkingConfig.google_wobble_speed_ceiling;
    params.sampling_params.min_output_rate = 120.0;
    params.sampling_params.end_of_stroke_stopping_distance = 0.01f;

    (void)m_googleModeler->Reset(params);

    // Seed the first sample into the physics pipeline
    ink::stroke_model::Input input;
    input.event_type = ink::stroke_model::Input::EventType::kDown;
    input.position = { static_cast<float>(worldX), static_cast<float>(worldY) };
    input.time = ink::stroke_model::Time(timeSec);
    input.pressure = pressure;

    std::vector<ink::stroke_model::Result> results;
    if (m_googleModeler->Update(input, results).ok()) {
        ProcessGoogleResults(results);
    }

    if (g_InkingConfig.google_enable_prediction) {
        UpdateGooglePrediction();
    }
}

void InkEngine::AppendPoint(double worldX, double worldY, float pressure, double timeSec,
                            float tiltX, float tiltY) {
    if (!m_isStrokeActive) return;

    m_tiltX = tiltX;
    m_tiltY = tiltY;

    Point2D pt{ worldX, worldY, pressure, timeSec, tiltX, tiltY };
    m_activeStrokePoints.push_back(pt);

    ink::stroke_model::Input input;
    input.event_type = ink::stroke_model::Input::EventType::kMove;
    input.position = { static_cast<float>(worldX), static_cast<float>(worldY) };
    input.time = ink::stroke_model::Time(timeSec);
    input.pressure = pressure;

    std::vector<ink::stroke_model::Result> results;
    if (m_googleModeler->Update(input, results).ok()) {
        ProcessGoogleResults(results);
    }

    if (g_InkingConfig.google_enable_prediction) {
        UpdateGooglePrediction();
    }
}

FinishedStrokeData InkEngine::FinishStroke() {
    if (!m_isStrokeActive) return {};

    if (!m_activeStrokePoints.empty()) {
        // Stage 1: Pen-Up event to flush lingering spring inertia in physics modeler
        ink::stroke_model::Input input;
        input.event_type = ink::stroke_model::Input::EventType::kUp;
        input.position = {
            static_cast<float>(m_activeStrokePoints.back().x),
            static_cast<float>(m_activeStrokePoints.back().y)
        };
        input.time = ink::stroke_model::Time(m_activeStrokePoints.back().timeSeconds);
        input.pressure = m_activeStrokePoints.back().pressure;

        std::vector<ink::stroke_model::Result> results;
        if (m_googleModeler->Update(input, results).ok()) {
            ProcessGoogleResults(results);
        }

        // Stage 2: Single-tap dot handling (user tapped without lateral dragging)
        if (m_liveModeledPoints.empty() && m_hasPreviousGooglePoint) {
            float baseW = static_cast<float>(m_activeTool.baseSize);
            m_liveModeledPoints.push_back({
                static_cast<float>(m_previousGooglePos.x),
                static_cast<float>(m_previousGooglePos.y),
                baseW,
                1.0f,
                0.0f,
                m_tiltX,
                m_tiltY
            });

            Segment1D dot;
            dot.p0 = Point2D{ m_previousGooglePos.x, m_previousGooglePos.y, 0, 0 };
            dot.p1 = dot.p0;
            dot.p1.x += 0.001; // Epsilon offset to establish valid vector direction
            dot.width = baseW;
            m_fullSmoothedSegments.push_back(dot);
        }
    }

    // Stage 3: Package final vector stroke data
    FinishedStrokeData data;
    data.rawPoints = std::move(m_activeStrokePoints);
    data.liveSegments = std::move(m_fullSmoothedSegments);
    data.modeledPoints = m_liveModeledPoints;
    data.outlinePath = StrokeOutlineBuilder::BuildOutline(
        m_liveModeledPoints, m_activeTool.capType, m_activeTool.strokePattern);

    // Stage 4: Reset live state
    CancelStroke();

    return data;
}

void InkEngine::CancelStroke() {
    m_activeStrokePoints.clear();
    m_fullSmoothedSegments.clear();
    m_liveModeledPoints.clear();
    m_liveStrokeOutline.clear();
    m_predictedStrokeOutline.clear();
    m_googlePredictedSegments.clear();
    m_hasPreviousGooglePoint = false;
    m_smoothedWidth = -1.0f;
    m_tiltX = 0.0f;
    m_tiltY = 0.0f;
    m_isStrokeActive = false;
}

// -----------------------------------------------------------------------------
// 2. MATHEMATICAL THICKNESS & MODELING
// -----------------------------------------------------------------------------

float InkEngine::CalculateDynamicWidth(const ink::stroke_model::Result& res) const {
    float baseWidth = static_cast<float>(m_activeTool.baseSize);

    // STAGE A: PRESSURE CEILING
    float pressureWidth = baseWidth;
    if (g_InkingConfig.google_use_pressure && res.pressure >= 0.0f) {
        float pressureCurved = g_InkingConfig.EvaluatePressure(res.pressure);
        float minFrac = g_InkingConfig.google_min_width_multiplier;
        float maxFrac = g_InkingConfig.google_max_width_multiplier;
        float widthMultiplier = minFrac + pressureCurved * (maxFrac - minFrac);
        pressureWidth = baseWidth * std::clamp(widthMultiplier, minFrac, maxFrac);
    }

    // STAGE B: VELOCITY THINNING
    float velocityThinFactor = 1.0f;
    if (g_InkingConfig.google_use_velocity) {
        float speed = std::hypot(res.velocity.x, res.velocity.y) * m_zoomScale;
        float normalizedSpeed = std::clamp(
            speed / g_InkingConfig.google_velocity_thinning_max_speed, 0.0f, 1.0f);

        float minThin = g_InkingConfig.google_min_width_multiplier;
        velocityThinFactor = 1.0f - normalizedSpeed * (1.0f - minThin);
    }

    return pressureWidth * velocityThinFactor;
}

void InkEngine::ProcessGoogleResults(const std::vector<ink::stroke_model::Result>& results) {
    for (const auto& res : results) {
        float targetWidth = CalculateDynamicWidth(res);

        // Exponential Moving Average (EMA) width filter: removes single-frame pop
        if (m_smoothedWidth < 0.0f) {
            m_smoothedWidth = targetWidth;
        } else {
            m_smoothedWidth = std::lerp(
                m_smoothedWidth, targetWidth, g_InkingConfig.google_dynamic_width_smoothing);
        }

        // Build 1D line segments for collision and spatial testing
        if (m_hasPreviousGooglePoint) {
            Segment1D seg;
            seg.p0 = Point2D{ m_previousGooglePos.x, m_previousGooglePos.y, 0, 0 };
            seg.p1 = Point2D{ res.position.x, res.position.y, 0, 0 };
            seg.width = m_smoothedWidth;
            m_fullSmoothedSegments.push_back(seg);
        }

        float speed = std::hypot(res.velocity.x, res.velocity.y);
        m_liveModeledPoints.push_back({
            res.position.x,
            res.position.y,
            m_smoothedWidth,
            res.pressure,
            speed,
            m_tiltX,
            m_tiltY
        });

        m_previousGooglePos = Point2D{ res.position.x, res.position.y, 0, 0 };
        m_hasPreviousGooglePoint = true;
    }

    // Reconstruct the 2D polygon outline for live preview
    if (!m_liveModeledPoints.empty()) {
        m_liveStrokeOutline = StrokeOutlineBuilder::BuildOutline(
            m_liveModeledPoints, m_activeTool.capType, m_activeTool.strokePattern);
    }
}

void InkEngine::UpdateGooglePrediction() {
    m_googlePredictedSegments.clear();
    m_predictedStrokeOutline.clear();
    if (!m_hasPreviousGooglePoint) return;

    std::vector<ink::stroke_model::Result> predictions;
    if (m_googleModeler->Predict(predictions).ok() && !predictions.empty()) {
        Point2D prev = m_previousGooglePos;
        float predWidth = m_smoothedWidth;

        std::vector<StrokeOutlineBuilder::InputPoint> predPoints;
        if (!m_liveModeledPoints.empty()) {
            predPoints.push_back(m_liveModeledPoints.back());
        }

        for (const auto& res : predictions) {
            float targetWidth = CalculateDynamicWidth(res);
            if (predWidth < 0.0f) {
                predWidth = targetWidth;
            } else {
                predWidth = std::lerp(
                    predWidth, targetWidth, g_InkingConfig.google_dynamic_width_smoothing);
            }

            Segment1D seg;
            seg.p0 = prev;
            seg.p1 = Point2D{ res.position.x, res.position.y, 0, 0 };
            seg.width = predWidth;
            m_googlePredictedSegments.push_back(seg);
            prev = seg.p1;

            float speed = std::hypot(res.velocity.x, res.velocity.y);
            predPoints.push_back({
                res.position.x,
                res.position.y,
                predWidth,
                res.pressure,
                speed,
                m_tiltX,
                m_tiltY
            });
        }

        if (predPoints.size() >= 2) {
            m_predictedStrokeOutline = StrokeOutlineBuilder::BuildOutline(
                predPoints, m_activeTool.capType, m_activeTool.strokePattern);
        }
    }
}

// -----------------------------------------------------------------------------
// 3. LAYER PRESENTATION & DATA QUERIES
// -----------------------------------------------------------------------------

LiveInkSnapshot InkEngine::GetLiveSnapshot() const {
    LiveInkSnapshot snapshot;
    snapshot.isStrokeActive = m_isStrokeActive;
    snapshot.confirmedOutline = m_liveStrokeOutline;
    snapshot.predictedOutline = m_predictedStrokeOutline;
    snapshot.strokeColor = BLRgba32(m_activeTool.color.value);

    if (m_isStrokeActive) {
        BLBox confirmedBox;
        if (m_liveStrokeOutline.get_bounding_box(&confirmedBox) == BL_SUCCESS) {
            snapshot.dirtyBounds = AABB(confirmedBox.x0, confirmedBox.y0, confirmedBox.x1, confirmedBox.y1);
        }
        BLBox predictedBox;
        if (m_predictedStrokeOutline.get_bounding_box(&predictedBox) == BL_SUCCESS) {
            AABB predAABB(predictedBox.x0, predictedBox.y0, predictedBox.x1, predictedBox.y1);
            snapshot.dirtyBounds.minX = std::min(snapshot.dirtyBounds.minX, predAABB.minX);
            snapshot.dirtyBounds.minY = std::min(snapshot.dirtyBounds.minY, predAABB.minY);
            snapshot.dirtyBounds.maxX = std::max(snapshot.dirtyBounds.maxX, predAABB.maxX);
            snapshot.dirtyBounds.maxY = std::max(snapshot.dirtyBounds.maxY, predAABB.maxY);
        }
    }

    return snapshot;
}

void InkEngine::RenderLiveStroke(BLContext& ctx) const {
    if (!m_isStrokeActive) return;

    ctx.save();
    ctx.set_fill_style(BLRgba32(m_activeTool.color.value));

    // 1. Render confirmed live ribbon polygon
    if (!m_liveStrokeOutline.is_empty()) {
        ctx.fill_path(m_liveStrokeOutline);
    }

    // 2. Render low-latency forward-predicted tip extension
    if (!m_predictedStrokeOutline.is_empty()) {
        ctx.fill_path(m_predictedStrokeOutline);
    }

    ctx.restore();
}

// -----------------------------------------------------------------------------
// 4. EPHEMERAL PRESENTATION INK (Laser Pointer)
// -----------------------------------------------------------------------------

void InkEngine::AddEphemeralStroke(BLPath path, BLRgba32 color, uint32_t durationMs) {
    auto nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    m_ephemeralStrokes.push_back(EphemeralStroke{ std::move(path), color, nowMs, durationMs });
}

bool InkEngine::UpdateEphemeralStrokes(uint64_t currentTimeMs) {
    if (m_ephemeralStrokes.empty()) return false;

    size_t prevCount = m_ephemeralStrokes.size();
    std::erase_if(m_ephemeralStrokes, [currentTimeMs](const EphemeralStroke& stroke) {
        return (currentTimeMs - stroke.startTimeMs) >= stroke.durationMs;
    });

    // Redraw if any strokes are active (fading each frame) or any expired
    return !m_ephemeralStrokes.empty() || (m_ephemeralStrokes.size() != prevCount);
}

void InkEngine::ClearEphemeralStrokes() {
    m_ephemeralStrokes.clear();
}

void InkEngine::RenderEphemeralStrokes(BLContext& ctx, uint64_t currentTimeMs) const {
    if (m_ephemeralStrokes.empty()) return;

    for (const auto& stroke : m_ephemeralStrokes) {
        if (stroke.outlinePath.is_empty()) continue;

        uint64_t elapsed = (currentTimeMs >= stroke.startTimeMs) ? (currentTimeMs - stroke.startTimeMs) : 0;
        if (elapsed >= stroke.durationMs) continue;

        // Quadratic fade calculation: alpha = (1.0 - progress)^2
        float progress = static_cast<float>(elapsed) / static_cast<float>(stroke.durationMs);
        float alphaFactor = (1.0f - progress) * (1.0f - progress);

        BLRgba32 decayedColor = stroke.baseColor;
        uint32_t baseA = (decayedColor.value >> 24) & 0xFF;
        uint32_t currentA = static_cast<uint32_t>(baseA * alphaFactor);
        decayedColor.value = (decayedColor.value & 0x00FFFFFF) | (currentA << 24);

        ctx.save();
        ctx.set_fill_style(decayedColor);
        ctx.fill_path(stroke.outlinePath);
        ctx.restore();
    }
}

} // namespace Folio
