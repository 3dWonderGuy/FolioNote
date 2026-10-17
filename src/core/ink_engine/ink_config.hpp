#pragma once
/**
 * =========================================================================================
 * @file core/ink_engine/ink_config.hpp
 * @brief Self-Sustained Inking Physics, Smoothing, and Pressure Calibration Configuration
 * =========================================================================================
 *
 * Defines runtime tuning parameters for:
 * 1. Google Ink Stroke Modeler spring-mass-damper ODE solving.
 * 2. Wobble suppression / noise filtering.
 * 3. Dynamic thickness calculation (pressure gamma and velocity thinning).
 * 4. Forward Kalman prediction.
 */

#include <cmath>
#include <algorithm>

namespace Folio {

enum class SplineMode {
    QuadraticMidpoint,
    CentripetalCatmullRom,
    LinearPolylines
};

struct InkingConfig {
    // -------------------------------------------------------------
    // 1. PRESSURE CALIBRATION
    // -------------------------------------------------------------
    bool enablePressureCurve = true;
    float pressureGamma = 1.0f;
    float pressureDeadzoneMin = 0.02f;
    float pressureSaturationMax = 0.95f;
    float minRelativeThicknessFraction = 0.15f;

    // -------------------------------------------------------------
    // 2. GOOGLE INK STROKE MODELER PARAMS (Spring Physics)
    // -------------------------------------------------------------
    float google_spring_mass_constant = 11.0f / 32400.0f; // ~0.000339
    float google_drag_constant = 72.0f;
    bool google_enable_prediction = true;
    bool google_use_stroke_normal_projection = true;

    // -------------------------------------------------------------
    // 3. ADAPTIVE SAMPLING & ROTATIONAL VELOCITY
    // -------------------------------------------------------------
    float google_min_output_rate = 180.0f;                // Baseline output rate (Hz)
    float google_end_of_stroke_stopping_distance = 0.01f; // End-of-stroke stopping distance (mm)
    int google_end_of_stroke_max_iterations = 20;         // Max end-of-stroke catch-up steps
    bool google_enable_angle_upsampling = true;           // Upsample on fast turns (rotational velocity)
    float google_max_estimated_angle_rad = 0.06f;         // Max turn angle per step in radians (~3.4 deg)

    // -------------------------------------------------------------
    // 4. LOOP CONTRACTION MITIGATION (Speed-Based Curve Accuracy)
    // -------------------------------------------------------------
    bool google_loop_mitigation_enable = true;
    float google_loop_speed_lower_bound = 20.0f;          // Slow speed threshold (mm/s)
    float google_loop_speed_upper_bound = 400.0f;         // Fast speed threshold (mm/s)
    float google_loop_strength_at_lower_speed = 1.0f;     // 1.0 = pure spring smoothing at slow speed
    float google_loop_strength_at_upper_speed = 0.5f;     // 0.0 = raw input, 1.0 = spring at high speed
    float google_loop_min_speed_window_s = 0.04f;         // Moving average window for speed (s)

    // -------------------------------------------------------------
    // 5. WOBBLE SMOOTHER (Tremor / Noise Filter)
    // -------------------------------------------------------------
    bool google_wobble_enable = true;
    float google_wobble_timeout_s = 0.04f;
    float google_wobble_speed_floor = 1.31f;
    float google_wobble_speed_ceiling = 1.44f;

    // -------------------------------------------------------------
    // 6. DYNAMIC WIDTH MAPPING
    // -------------------------------------------------------------
    bool usePressure = true;
    bool useVelocity = false;
    bool useTilt = false;
    float minWidthMultiplier = 0.2f;
    float maxWidthMultiplier = 1.5f;
    float maxTiltWidthMultiplier = 2.8f;     // Shading width expansion at maximum stylus tilt
    float tiltDeadzoneRad = 0.22f;          // ~12.6 deg from vertical: upright grip stays thin, tilting widens smoothly
    float tiltMaxAngleRad = 1.05f;          // ~60 deg from vertical: full side-shading (typical digitizer max)
    float velocityThinningMaxSpeed = 1500.0f; // mm/s
    float dynamicWidthSmoothing = 0.6f;        // EMA factor (0.0 to 1.0)

    // -------------------------------------------------------------
    // 7. POST-PROCESSING & SIMPLIFICATION (Pen-Up Commit)
    // -------------------------------------------------------------
    bool enablePostProcessSimplification = true;
    float simplificationToleranceMm = 0.015f; // Ramer-Douglas-Peucker distance threshold (mm)
    float simplificationMaxTurnDeg = 4.0f;    // Max direction deviation a dropped span may have (keeps curves round)
    float simplificationRadiusTolerance = 0.08f; // Max relative radius error when dropping points (keeps width profile)
    float minTurnAngleForJointCapDeg = 3.0f;  // Minimum turn angle (degrees) to emit a joint cap disk
};

using InkingTuningConfig = InkingConfig;

} // namespace Folio

// Global namespace alias for backwards compatibility
using InkingConfig = Folio::InkingConfig;
using InkingTuningConfig = Folio::InkingTuningConfig;
using SplineMode = Folio::SplineMode;


