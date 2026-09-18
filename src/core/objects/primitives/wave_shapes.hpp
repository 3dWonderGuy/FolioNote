#pragma once
/**
 * @file wave_shapes.hpp
 * @brief Algorithmic path generation and mathematical modeling for wave shapes (Sine Wave, Square Wave).
 *
 * Mathematical Foundations:
 * -------------------------
 * 1. Sine Wave:
 *    A continuous periodic trigonometric waveform defined across the horizontal interval [x, x + W]
 *    with vertical bounding extent [y, y + H]:
 *      y_center = y + H / 2           (equilibrium baseline)
 *      Amplitude A = H / 2            (half of peak-to-peak height)
 *      Spatial Frequency f = N / W    (cycles per world unit, where N = cycle count)
 *      Wavelength λ = W / N           (horizontal length of one complete period)
 *
 *    The elevation at any horizontal position x' in [x, x + W] is given by:
 *      y(x') = y_center - A * sin(2π * N * (x' - x) / W)
 *    (Note: In canvas coordinate systems, +y points downwards. The minus sign ensures that the
 *    initial half-cycle ascends towards the top of the canvas, matching standard scientific conventions.)
 *
 * 2. Square Wave:
 *    A piecewise-constant alternating pulse waveform with sharp transitions between high and low rails:
 *      y_center = y + H / 2
 *      High rail: y_high = y          (top edge, y_center - A)
 *      Low rail:  y_low  = y + H      (bottom edge, y_center + A)
 *      Period T = W / N
 *
 *    For each cycle k ∈ [0, N):
 *      - Step from baseline y_center to y_high at x_k = x + k * T (or transition from previous cycle)
 *      - Horizontal pulse along y_high for duration T * dutyCycle (default dutyCycle = 0.5)
 *      - Vertical step down from y_high to y_low at x_k + T * dutyCycle
 *      - Horizontal pulse along y_low for duration T * (1 - dutyCycle)
 *      - Vertical step at cycle completion
 *      - Final closure returns to baseline y_center at x + W.
 *
 * Transformation & Dynamic Stretch Contract:
 * ------------------------------------------
 * - Horizontal stretching (scaling W) scales the period T, modifying the spatial frequency f = N / W.
 *   Live gizmo scale expands or contracts the cycles in real time.
 * - Vertical stretching (scaling H) scales the amplitude A = H / 2.
 * - The cycle count N is maintained via param1 (default = 3.0), adjustable via the ribbon UI.
 */

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <algorithm>
#include <cstdint>

#include <blend2d/blend2d.h>
#include <imgui.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Folio {

/**
 * @brief Algorithmic generator for wave vector paths and UI icons.
 */
class WaveShapeGenerator {
public:
    /**
     * @brief Procedurally builds a smooth Blend2D path for a continuous Sine Wave.
     *
     * Mathematical Process:
     * 1. Clamps width W and height H to minimum thresholds (0.01 mm) to avoid division by zero.
     * 2. Computes the baseline y_center = y + H * 0.5 and amplitude A = H * 0.5.
     * 3. Adapts sample resolution dynamically based on cycle count:
     *      samples = max(64, int(cycles * 32.0))
     *    This ensures crisp, curvature-accurate rendering without aliasing even at high frequencies.
     * 4. Evaluates y(t) = y_center - A * sin(2π * cycles * t) for t ∈ [0, 1].
     * 5. Appends line segments to the BLPath. Blend2D stroking with round joins creates a C∞-smooth curve.
     *
     * @param path    Output BLPath (cleared before generation).
     * @param x       Top-left X coordinate of the bounding box (world millimeters).
     * @param y       Top-left Y coordinate of the bounding box (world millimeters).
     * @param width   Horizontal span / length of the wave (world millimeters).
     * @param height  Total vertical peak-to-peak extent (world millimeters).
     * @param cycles  Total number of complete sine periods across the width (param1).
     */
    static void BuildSineWavePath(BLPath& path,
                                  double x, double y,
                                  double width, double height,
                                  double cycles);

    /**
     * @brief Procedurally builds a Blend2D path for an orthogonal Square Wave.
     *
     * Mathematical Process:
     * 1. Determines high rail (y_high = y), low rail (y_low = y + height), and baseline (y_center = y + height * 0.5).
     * 2. Partitions the horizontal span into N cycles of period T = width / cycles.
     * 3. Starts path at (x, y_center).
     * 4. For each cycle k from 0 up to floor(cycles):
     *    - Climbs vertically to (x_k, y_high).
     *    - Traces high plateau to (x_k + T * dutyCycle, y_high).
     *    - Drops vertically to (x_k + T * dutyCycle, y_low).
     *    - Traces low plateau to (x_{k+1}, y_low).
     * 5. Accommodates fractional remaining cycles if cycles is non-integer.
     * 6. Concludes with a vertical return to the baseline (x + width, y_center).
     *
     * @param path       Output BLPath (cleared before generation).
     * @param x          Top-left X coordinate of the bounding box (world millimeters).
     * @param y          Top-left Y coordinate of the bounding box (world millimeters).
     * @param width      Horizontal span / length of the wave (world millimeters).
     * @param height     Total vertical peak-to-peak extent (world millimeters).
     * @param cycles     Total number of periods across the width (param1).
     * @param dutyCycle  Ratio of high-rail duration to full period (default = 0.5, symmetric).
     */
    static void BuildSquareWavePath(BLPath& path,
                                    double x, double y,
                                    double width, double height,
                                    double cycles,
                                    double dutyCycle = 0.5);

    /**
     * @brief Renders a vector Sine Wave preview icon in an ImGui draw list.
     *
     * @param drawList  ImGui draw list for ribbon button rendering.
     * @param pMin      Top-left corner of the icon bounding box in screen pixels.
     * @param pMax      Bottom-right corner of the icon bounding box in screen pixels.
     * @param strokeCol 32-bit packed RGBA color for the wave stroke.
     * @param thickness Stroke thickness in screen pixels (default 1.5f).
     */
    static void DrawSineWaveIconImGui(ImDrawList* drawList,
                                      ImVec2 pMin, ImVec2 pMax,
                                      ImU32 strokeCol,
                                      float thickness = 1.5f);

    /**
     * @brief Renders a vector Square Wave preview icon in an ImGui draw list.
     *
     * @param drawList  ImGui draw list for ribbon button rendering.
     * @param pMin      Top-left corner of the icon bounding box in screen pixels.
     * @param pMax      Bottom-right corner of the icon bounding box in screen pixels.
     * @param strokeCol 32-bit packed RGBA color for the wave stroke.
     * @param thickness Stroke thickness in screen pixels (default 1.5f).
     */
    static void DrawSquareWaveIconImGui(ImDrawList* drawList,
                                        ImVec2 pMin, ImVec2 pMax,
                                        ImU32 strokeCol,
                                        float thickness = 1.5f);

    /**
     * @brief Procedurally builds a Blend2D path for a symmetric Triangle Wave.
     *
     * Mathematical Process:
     * 1. Baseline y_center = y + height * 0.5, amplitude A = height * 0.5.
     * 2. Period T = width / cycles.
     * 3. For each cycle k from 0 to ceil(cycles):
     *    - Start at baseline: (x_k, y_center)
     *    - Ascend linearly to peak: (x_k + 0.25 * T, y)
     *    - Descend linearly through baseline to trough: (x_k + 0.75 * T, y + height)
     *    - Ascend linearly back to baseline: (x_k + T, y_center)
     * 4. Clamps x coordinates to [x, x + width] and concludes at (x + width, y_center).
     *
     * @param path    Output BLPath (cleared before generation).
     * @param x       Top-left X coordinate of the bounding box (world millimeters).
     * @param y       Top-left Y coordinate of the bounding box (world millimeters).
     * @param width   Horizontal span / length of the wave (world millimeters).
     * @param height  Total vertical peak-to-peak extent (world millimeters).
     * @param cycles  Total number of complete periods across the width (param1).
     */
    static void BuildTriangleWavePath(BLPath& path,
                                      double x, double y,
                                      double width, double height,
                                      double cycles);

    /**
     * @brief Procedurally builds a Blend2D path for a Right Triangle (Sawtooth / Ramp) Wave.
     *
     * Mathematical Process:
     * 1. Baseline y_center = y + height * 0.5, peak y_top = y, trough y_bottom = y + height.
     * 2. Period T = width / cycles.
     * 3. If rightAngleOnRight is true (default, Ramp Up):
     *    - In each cycle: linear ramp ascending from (x_k, y_bottom) to (x_k + T, y_top).
     *    - Vertical drop at x_k + T from (x_k + T, y_top) to (x_k + T, y_bottom), forming a right angle on the right.
     * 4. If rightAngleOnRight is false (Ramp Down):
     *    - In each cycle: vertical rise at x_k from (x_k, y_bottom) to (x_k, y_top), forming a right angle on the left.
     *    - Linear ramp descending from (x_k, y_top) to (x_k + T, y_bottom).
     * 5. Start and end points anchor smoothly to baseline (x, y_center) and (x + width, y_center).
     *
     * @param path              Output BLPath (cleared before generation).
     * @param x                 Top-left X coordinate of the bounding box (world millimeters).
     * @param y                 Top-left Y coordinate of the bounding box (world millimeters).
     * @param width             Horizontal span / length of the wave (world millimeters).
     * @param height            Total vertical peak-to-peak extent (world millimeters).
     * @param cycles            Total number of periods across the width (param1).
     * @param rightAngleOnRight True if the vertical edge (right angle) is on the right; false if on the left (param2).
     */
    static void BuildRightTriangleWavePath(BLPath& path,
                                           double x, double y,
                                           double width, double height,
                                           double cycles,
                                           bool rightAngleOnRight = true);

    /**
     * @brief Renders a vector Triangle Wave preview icon in an ImGui draw list.
     */
    static void DrawTriangleWaveIconImGui(ImDrawList* drawList,
                                          ImVec2 pMin, ImVec2 pMax,
                                          ImU32 strokeCol,
                                          float thickness = 1.5f);

    /**
     * @brief Renders a vector Right Triangle Wave preview icon in an ImGui draw list.
     */
    static void DrawRightTriangleWaveIconImGui(ImDrawList* drawList,
                                               ImVec2 pMin, ImVec2 pMax,
                                               ImU32 strokeCol,
                                               bool rightAngleOnRight = true,
                                               float thickness = 1.5f);
};

} // namespace Folio
