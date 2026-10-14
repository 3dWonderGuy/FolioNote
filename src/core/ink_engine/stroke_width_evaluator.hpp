#pragma once
/**
 * =========================================================================================
 * @file core/ink_engine/stroke_width_evaluator.hpp
 * @brief Dynamic Stylus Width, Pressure Curve, and Velocity Thinning Evaluator
 * =========================================================================================
 *
 * Encapsulates all non-linear stylus dynamics:
 * 1. Hardware pressure curve calibration (deadzone, saturation, power-law gamma).
 * 2. Pressure-to-thickness bounding and scaling.
 * 3. Velocity thinning (Fountain pen hydrodynamic thinning effect).
 * 4. Exponential Moving Average (EMA) width filter for flicker-free transitions.
 */

#include <cmath>
#include <algorithm>
#include "core/ink_engine/ink_config.hpp"
#include "core/ink_engine/ink_stroke_data.hpp"

namespace Folio {

class StrokeWidthEvaluator {
public:
    /**
     * @brief Evaluates hardware stylus pressure through deadzone, saturation, and gamma curves.
     * @param rawPressure Hardware stylus pressure reported by digitizer [0.0, 1.0].
     * @param config Active inking configuration.
     * @return Curved thickness multiplier in [config.minRelativeThicknessFraction, 1.0].
     */
    [[nodiscard]] static float EvaluatePressure(float rawPressure, const InkingConfig& config) noexcept {
        if (!config.enablePressureCurve) {
            return 1.0f;
        }
        if (rawPressure <= config.pressureDeadzoneMin) {
            return config.minRelativeThicknessFraction;
        }
        if (rawPressure >= config.pressureSaturationMax) {
            return 1.0f;
        }

        float norm = (rawPressure - config.pressureDeadzoneMin) /
                     (config.pressureSaturationMax - config.pressureDeadzoneMin);
        float curved = std::pow(norm, config.pressureGamma);
        return config.minRelativeThicknessFraction +
               (1.0f - config.minRelativeThicknessFraction) *
                   std::clamp(curved, 0.0f, 1.0f);
    }

    /**
     * @brief Computes pressure-modulated stroke thickness in world millimeters.
     * @param baseWidth Base tool diameter in world millimeters.
     * @param rawPressure Hardware stylus pressure [0.0, 1.0]. Negative values indicate non-pressure input.
     * @param config Active inking configuration.
     * @return Scaled stroke width in millimeters.
     */
    [[nodiscard]] static float CalculatePressureWidth(float baseWidth, float rawPressure,
                                                      const InkingConfig& config) noexcept {
        if (!config.google_use_pressure || rawPressure < 0.0f) {
            return baseWidth;
        }

        float pressureCurved = EvaluatePressure(rawPressure, config);
        float minFrac = config.google_min_width_multiplier;
        float maxFrac = config.google_max_width_multiplier;
        float widthMultiplier = minFrac + pressureCurved * (maxFrac - minFrac);
        return baseWidth * std::clamp(widthMultiplier, minFrac, maxFrac);
    }

    /**
     * @brief Computes tilt modulation factor (e.g. pencil shading / chisel flat angle).
     * @param tiltX Stylus tilt angle along X axis in radians.
     * @param tiltY Stylus tilt angle along Y axis in radians.
     * @param config Active inking configuration.
     * @return Multiplier factor for stroke width (1.0 = no tilt alteration).
     */
    [[nodiscard]] static float CalculateTiltWidthFactor(float tiltX, float tiltY,
                                                        const InkingConfig& config) noexcept {
        if (!config.google_use_tilt) {
            return 1.0f;
        }

        // Empty stub: ready for custom tilt math (pencil shading / chisel angle modulation)
        return 1.0f;
    }

    /**
     * @brief Overload computing pressure and tilt modulated stroke thickness in world millimeters.
     */
    [[nodiscard]] static float CalculatePressureWidth(float baseWidth, float rawPressure,
                                                      float tiltX, float tiltY,
                                                      const InkingConfig& config) noexcept {
        float pWidth = CalculatePressureWidth(baseWidth, rawPressure, config);
        float tiltFactor = CalculateTiltWidthFactor(tiltX, tiltY, config);
        return pWidth * tiltFactor;
    }

    /**
     * @brief Computes velocity thinning multiplier (hydrodynamic fountain pen simulation).
     * @param speedMmPerSec Euclidean pen speed in millimeters per second.
     * @param zoomScale Active canvas viewport zoom scale.
     * @param config Active inking configuration.
     * @return Thinning factor in [config.google_min_width_multiplier, 1.0].
     */
    [[nodiscard]] static float CalculateVelocityThinFactor(float speedMmPerSec, float zoomScale,
                                                           const InkingConfig& config) noexcept {
        if (!config.google_use_velocity || speedMmPerSec <= 0.0f) {
            return 1.0f;
        }

        float speed = speedMmPerSec * zoomScale;
        float normalizedSpeed = std::clamp(
            speed / config.google_velocity_thinning_max_speed, 0.0f, 1.0f);
        float minThin = config.google_min_width_multiplier;
        return 1.0f - normalizedSpeed * (1.0f - minThin);
    }

    /**
     * @brief Unified dynamic width calculation combining pressure curves, velocity thinning, and tilt.
     */
    [[nodiscard]] static float CalculateDynamicWidth(float baseWidth, float rawPressure,
                                                     float speedMmPerSec, float zoomScale,
                                                     float tiltX, float tiltY,
                                                     const InkingConfig& config) noexcept {
        float pressureWidth = CalculatePressureWidth(baseWidth, rawPressure, config);
        float velocityFactor = CalculateVelocityThinFactor(speedMmPerSec, zoomScale, config);
        float tiltFactor = CalculateTiltWidthFactor(tiltX, tiltY, config);
        return pressureWidth * velocityFactor * tiltFactor;
    }

    /**
     * @brief Evaluates complete dynamic stroke thickness from Point2D telemetry and applies EMA smoothing.
     * @param baseWidth Chosen tool base size in millimeters.
     * @param pt Incoming stylus point containing pressure, speed, and tilt.
     * @param previousWidth Smoothed width from preceding point (-1.0f if starting a stroke).
     * @param zoomScale Current canvas zoom factor.
     * @param config Active inking configuration.
     * @return Final smoothed stroke thickness in millimeters.
     */
    [[nodiscard]] static float CalculateDynamicWidth(float baseWidth, const Point2D& pt,
                                                     float previousWidth, float zoomScale,
                                                     const InkingConfig& config) noexcept {
        float pressureWidth = CalculatePressureWidth(baseWidth, pt.pressure, config);
        float velocityFactor = CalculateVelocityThinFactor(pt.speed, zoomScale, config);
        float tiltFactor = CalculateTiltWidthFactor(pt.tiltX, pt.tiltY, config);
        float targetWidth = pressureWidth * velocityFactor * tiltFactor;
        return FilterEMA(previousWidth, targetWidth, config.google_dynamic_width_smoothing);
    }

    /**
     * @brief Unified dynamic width convenience overload without tilt parameters.
     */
    [[nodiscard]] static float CalculateDynamicWidth(float baseWidth, float rawPressure,
                                                     float speedMmPerSec, float zoomScale,
                                                     const InkingConfig& config) noexcept {
        return CalculateDynamicWidth(baseWidth, rawPressure, speedMmPerSec, zoomScale, 0.0f, 0.0f, config);
    }

    /**
     * @brief Applies an Exponential Moving Average (EMA) filter to prevent abrupt thickness jumps.
     * @param currentSmoothed Current smoothed width in millimeters (negative if uninitialized).
     * @param targetWidth Instantaneous target width in millimeters.
     * @param smoothingFactor EMA weight factor in [0.0, 1.0] (0 = sluggish, 1 = instant).
     * @return Filtered stroke width in millimeters.
     */
    [[nodiscard]] static float FilterEMA(float currentSmoothed, float targetWidth,
                                         float smoothingFactor) noexcept {
        if (currentSmoothed < 0.0f) {
            return targetWidth;
        }
        return std::lerp(currentSmoothed, targetWidth, std::clamp(smoothingFactor, 0.0f, 1.0f));
    }
};

} // namespace Folio

