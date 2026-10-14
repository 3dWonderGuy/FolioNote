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
     * @brief Evaluates complete dynamic stroke thickness from Point2D telemetry and applies EMA smoothing.
     * 
     * Pipeline Steps:
     * 1. ScaleWidthByPressure(): Modulates tool baseSize by hardware pressure curve.
     * 2. ComputeVelocityThinning(): Applies fountain pen speed thinning factor.
     * 3. ComputeTiltModulation(): Applies stylus tilt factor.
     * 4. Multiplies factors to establish instantaneous target thickness.
     * 5. ApplySmoothingEMA(): Dampens transitions against previous point to eliminate jitter.
     *
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
        float pressureWidth = ScaleWidthByPressure(baseWidth, pt.pressure, config);
        float velocityFactor = ComputeVelocityThinning(pt.speed, zoomScale, config);
        float tiltFactor = ComputeTiltModulation(pt.tiltX, pt.tiltY, config);
        float targetWidth = pressureWidth * velocityFactor * tiltFactor;
        return ApplySmoothingEMA(previousWidth, targetWidth, config.dynamicWidthSmoothing);
    }

private:
    /**
     * @brief Maps raw hardware stylus pressure through deadzone, saturation, and power-law gamma curve.
     * @param rawPressure Hardware stylus pressure reported by digitizer [0.0, 1.0].
     * @param config Active inking configuration.
     * @return Curved response multiplier in [config.minRelativeThicknessFraction, 1.0].
     */
    [[nodiscard]] static float ApplyPressureCurve(float rawPressure, const InkingConfig& config) noexcept {
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
     * @brief Scales base tool diameter by calibrated stylus pressure response.
     * @param baseWidth Base tool diameter in world millimeters.
     * @param rawPressure Hardware stylus pressure [0.0, 1.0]. Negative values indicate non-pressure input.
     * @param config Active inking configuration.
     * @return Pressure-scaled stroke width in millimeters.
     */
    [[nodiscard]] static float ScaleWidthByPressure(float baseWidth, float rawPressure,
                                                    const InkingConfig& config) noexcept {
        if (!config.usePressure || rawPressure < 0.0f) {
            return baseWidth;
        }

        float pressureCurved = ApplyPressureCurve(rawPressure, config);
        float minFrac = config.minWidthMultiplier;
        float maxFrac = config.maxWidthMultiplier;
        float widthMultiplier = minFrac + pressureCurved * (maxFrac - minFrac);
        return baseWidth * std::clamp(widthMultiplier, minFrac, maxFrac);
    }

    /**
     * @brief Computes velocity thinning multiplier (hydrodynamic fountain pen simulation).
     * @param speedMmPerSec Euclidean pen speed in millimeters per second.
     * @param zoomScale Active canvas viewport zoom scale.
     * @param config Active inking configuration.
     * @return Thinning factor in [config.minWidthMultiplier, 1.0].
     */
    [[nodiscard]] static float ComputeVelocityThinning(float speedMmPerSec, float zoomScale,
                                                       const InkingConfig& config) noexcept {
        if (!config.useVelocity || speedMmPerSec <= 0.0f) {
            return 1.0f;
        }

        float speed = speedMmPerSec * zoomScale;
        float normalizedSpeed = std::clamp(
            speed / config.velocityThinningMaxSpeed, 0.0f, 1.0f);
        float minThin = config.minWidthMultiplier;
        return 1.0f - normalizedSpeed * (1.0f - minThin);
    }

    /**
     * @brief Computes tilt modulation factor (e.g. pencil shading / chisel flat angle).
     * @param tiltX Stylus tilt angle along X axis in radians.
     * @param tiltY Stylus tilt angle along Y axis in radians.
     * @param config Active inking configuration.
     * @return Multiplier factor for stroke width (1.0 = no tilt alteration).
     */
    [[nodiscard]] static float ComputeTiltModulation(float tiltX, float tiltY,
                                                     const InkingConfig& config) noexcept {
        if (!config.useTilt) {
            return 1.0f;
        }

        // Empty stub: ready for custom tilt math (pencil shading / chisel angle modulation)
        return 1.0f;
    }

    /**
     * @brief Applies an Exponential Moving Average (EMA) filter to prevent abrupt thickness jumps.
     * @param currentSmoothed Current smoothed width in millimeters (negative if uninitialized).
     * @param targetWidth Instantaneous target width in millimeters.
     * @param smoothingFactor EMA weight factor in [0.0, 1.0] (0 = sluggish, 1 = instant).
     * @return Filtered stroke width in millimeters.
     */
    [[nodiscard]] static float ApplySmoothingEMA(float currentSmoothed, float targetWidth,
                                                 float smoothingFactor) noexcept {
        if (currentSmoothed < 0.0f) {
            return targetWidth;
        }
        return std::lerp(currentSmoothed, targetWidth, std::clamp(smoothingFactor, 0.0f, 1.0f));
    }
};

} // namespace Folio
