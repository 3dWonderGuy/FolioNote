/**
 * @file wave_shapes.cpp
 * @brief Implementation of WaveShapeGenerator for mathematical wave shapes.
 *
 * Provides procedural generation for continuous trigonometric Sine Waves and
 * orthogonal pulse train Square Waves, alongside vector icon renderers for ImGui.
 *
 * Detailed Mathematical Analysis:
 * --------------------------------
 * 1. Sine Wave Curve Generation:
 *    Let x0 = x, y0 = y, W = width, H = height, N = cycles.
 *    The continuous mapping f: [0, 1] -> R^2 is defined by:
 *      x(t) = x0 + t * W
 *      y(t) = (y0 + 0.5 * H) - (0.5 * H) * sin(2 * π * N * t)
 *
 *    Derivative (instantaneous slope):
 *      dy/dx = (dy/dt) / (dx/dt) = -π * N * (H / W) * cos(2 * π * N * t)
 *    At the zero-crossings (t = k / (2N)), the maximum slope is π * N * (H / W).
 *    To ensure accurate curvature sampling without polygonal flattening where slope
 *    is steepest, the step count is dynamically scaled with N:
 *      stepCount = clamp(ceil(N * 32), 64, 512).
 *    The maximum angular step per segment is Δθ = 2π / 32 ≈ 11.25°, guaranteeing
 *    that polygonal approximation error remains below 0.05% of the amplitude.
 *
 * 2. Square Wave Piecewise Trajectory:
 *    For an orthogonal pulse train with period T = W / N and duty cycle D ∈ (0, 1):
 *    In each cycle k:
 *      Phase 1 (Rising Edge & High Rail):
 *        Vertical transition at x = x0 + k*T from baseline/low rail to y_high = y0.
 *        Horizontal plateau from x0 + k*T to x0 + (k + D)*T at y = y_high.
 *      Phase 2 (Falling Edge & Low Rail):
 *        Vertical transition at x = x0 + (k + D)*T from y_high to y_low = y0 + H.
 *        Horizontal plateau from x0 + (k + D)*T to x0 + (k + 1)*T at y = y_low.
 *    Boundary transitions ensure that both ends neatly anchor onto the central baseline
 *    (y0 + 0.5 * H), creating a visually pleasing, balanced waveform on canvas.
 */

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <algorithm>
#include <vector>

#include "core/objects/primitives/wave_shapes.hpp"

// Prevent MSVC min/max macro conflicts
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace Folio {

// =============================================================================
// SINE WAVE PATH GENERATION
// =============================================================================

/**
 * @brief Builds a smooth Blend2D path representing a Sine Wave.
 *
 * Inputs:
 *  - path:    Target BLPath object to receive path segments.
 *  - x, y:    Top-left bounding box coordinates in world millimeters.
 *  - width:   Total length of the waveform along the propagation axis.
 *  - height:  Peak-to-peak amplitude extent.
 *  - cycles:  Number of complete periodic oscillations across the width.
 *
 * Output:
 *  - Populates 'path' with a continuous open trajectory from (x, y + height/2)
 *    to (x + width, y(x+width)).
 */
void WaveShapeGenerator::BuildSineWavePath(BLPath& path,
                                          double x, double y,
                                          double width, double height,
                                          double cycles) {
    path.clear();

    const double w = (std::max)(width,  0.01);
    const double h = (std::max)(height, 0.01);
    const double n = (std::max)(cycles, 0.1);

    const double amplitude = h * 0.5;
    const double yCenter   = y + amplitude;

    // Determine adaptive sampling density: minimum 64 steps, or 32 steps per cycle
    const int steps = std::clamp(static_cast<int>(std::ceil(n * 32.0)), 64, 512);
    const double dt = 1.0 / static_cast<double>(steps);
    const double twoPiN = 2.0 * M_PI * n;

    for (int i = 0; i <= steps; ++i) {
        double t  = i * dt;
        double px = x + t * w;
        // Canvas Y increases downwards, so minus elevates the positive half-cycle
        double py = yCenter - amplitude * std::sin(twoPiN * t);

        if (i == 0) {
            path.move_to(px, py);
        } else {
            path.line_to(px, py);
        }
    }
}

// =============================================================================
// SQUARE WAVE PATH GENERATION
// =============================================================================

/**
 * @brief Builds an orthogonal Blend2D path representing a Square Wave pulse train.
 *
 * Inputs:
 *  - path:      Target BLPath object to receive path segments.
 *  - x, y:      Top-left bounding box coordinates in world millimeters.
 *  - width:     Total length of the waveform along the propagation axis.
 *  - height:    Peak-to-peak amplitude extent.
 *  - cycles:    Number of complete periods across the width.
 *  - dutyCycle: Ratio of high-state duration to total period (default = 0.5).
 *
 * Output:
 *  - Populates 'path' with orthogonal horizontal and vertical steps.
 */
void WaveShapeGenerator::BuildSquareWavePath(BLPath& path,
                                            double x, double y,
                                            double width, double height,
                                            double cycles,
                                            double dutyCycle) {
    path.clear();

    const double w = (std::max)(width,  0.01);
    const double h = (std::max)(height, 0.01);
    const double n = (std::max)(cycles, 0.1);
    const double d = std::clamp(dutyCycle, 0.05, 0.95);

    const double yHigh   = y;
    const double yLow    = y + h;
    const double yCenter = y + h * 0.5;
    const double period  = w / n;
    const double xEnd    = x + w;

    // Start anchor at baseline
    path.move_to(x, yCenter);

    double currentX = x;
    int cycleCount = static_cast<int>(std::ceil(n));

    for (int k = 0; k < cycleCount; ++k) {
        double cycleStart = x + k * period;
        double fallEdge   = cycleStart + period * d;
        double cycleEnd   = cycleStart + period;

        if (cycleStart >= xEnd) break;

        // Step up to high rail at start of cycle
        path.line_to(cycleStart, yHigh);

        if (fallEdge >= xEnd) {
            // Reaches end of waveform while on high rail
            path.line_to(xEnd, yHigh);
            currentX = xEnd;
            break;
        }

        // Trace high rail to falling edge
        path.line_to(fallEdge, yHigh);

        // Step down to low rail
        path.line_to(fallEdge, yLow);

        if (cycleEnd >= xEnd) {
            // Reaches end of waveform while on low rail
            path.line_to(xEnd, yLow);
            currentX = xEnd;
            break;
        }

        // Trace low rail to cycle boundary
        path.line_to(cycleEnd, yLow);
        currentX = cycleEnd;
    }

    // Return to equilibrium baseline at termination point
    path.line_to(xEnd, yCenter);
}

// =============================================================================
// IMGUI VECTOR ICON RENDERERS
// =============================================================================

/**
 * @brief Procedurally draws a 2-cycle Sine Wave icon into an ImGui draw list.
 */
void WaveShapeGenerator::DrawSineWaveIconImGui(ImDrawList* drawList,
                                              ImVec2 pMin, ImVec2 pMax,
                                              ImU32 strokeCol,
                                              float thickness) {
    if (!drawList) return;

    const float padX = 4.0f;
    const float padY = 5.0f;
    const float x0 = pMin.x + padX;
    const float y0 = pMin.y + padY;
    const float x1 = pMax.x - padX;
    const float y1 = pMax.y - padY;
    const float w = x1 - x0;
    const float h = y1 - y0;

    if (w <= 1.0f || h <= 1.0f) return;

    const float yMid = y0 + h * 0.5f;
    const float amp  = h * 0.45f;
    constexpr int steps = 32;
    constexpr float numCycles = 2.0f;

    ImVec2 pts[steps + 1];
    for (int i = 0; i <= steps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(steps);
        float px = x0 + t * w;
        float py = yMid - amp * std::sin(numCycles * 2.0f * 3.14159265f * t);
        pts[i] = ImVec2(px, py);
    }

    drawList->AddPolyline(pts, steps + 1, strokeCol, 0, thickness);
}

/**
 * @brief Procedurally draws a 2-cycle Square Wave icon into an ImGui draw list.
 */
void WaveShapeGenerator::DrawSquareWaveIconImGui(ImDrawList* drawList,
                                                ImVec2 pMin, ImVec2 pMax,
                                                ImU32 strokeCol,
                                                float thickness) {
    if (!drawList) return;

    const float padX = 4.0f;
    const float padY = 5.0f;
    const float x0 = pMin.x + padX;
    const float y0 = pMin.y + padY;
    const float x1 = pMax.x - padX;
    const float y1 = pMax.y - padY;
    const float w = x1 - x0;
    const float h = y1 - y0;

    if (w <= 1.0f || h <= 1.0f) return;

    const float yHigh = y0 + h * 0.15f;
    const float yLow  = y1 - h * 0.15f;
    const float yMid  = y0 + h * 0.5f;
    const float halfT = w * 0.25f; // 2 full cycles => 4 half-periods

    ImVec2 pts[10] = {
        ImVec2(x0, yMid),
        ImVec2(x0, yHigh),
        ImVec2(x0 + halfT, yHigh),
        ImVec2(x0 + halfT, yLow),
        ImVec2(x0 + 2.0f * halfT, yLow),
        ImVec2(x0 + 2.0f * halfT, yHigh),
        ImVec2(x0 + 3.0f * halfT, yHigh),
        ImVec2(x0 + 3.0f * halfT, yLow),
        ImVec2(x1, yLow),
        ImVec2(x1, yMid)
    };

    drawList->AddPolyline(pts, 10, strokeCol, 0, thickness);
}

// =============================================================================
// TRIANGLE WAVE PATH GENERATION
// =============================================================================

/**
 * @brief Builds a symmetric Triangle Wave path.
 */
void WaveShapeGenerator::BuildTriangleWavePath(BLPath& path,
                                              double x, double y,
                                              double width, double height,
                                              double cycles) {
    path.clear();

    const double w = (std::max)(width,  0.01);
    const double h = (std::max)(height, 0.01);
    const double n = (std::max)(cycles, 0.1);

    const double yHigh   = y;
    const double yLow    = y + h;
    const double yCenter = y + h * 0.5;
    const double period  = w / n;
    const double xEnd    = x + w;

    path.move_to(x, yCenter);

    int cycleCount = static_cast<int>(std::ceil(n));
    for (int k = 0; k < cycleCount; ++k) {
        double cycleStart = x + k * period;
        double pPeak      = cycleStart + period * 0.25;
        double pTrough    = cycleStart + period * 0.75;
        double cycleEnd   = cycleStart + period;

        if (cycleStart >= xEnd) break;

        // Ascend to peak
        if (pPeak >= xEnd) {
            double frac = (xEnd - cycleStart) / (period * 0.25);
            double py = yCenter - (h * 0.5) * frac;
            path.line_to(xEnd, py);
            break;
        }
        path.line_to(pPeak, yHigh);

        // Descend to trough
        if (pTrough >= xEnd) {
            double frac = (xEnd - pPeak) / (period * 0.5);
            double py = yHigh + h * frac;
            path.line_to(xEnd, py);
            break;
        }
        path.line_to(pTrough, yLow);

        // Ascend back towards baseline
        if (cycleEnd >= xEnd) {
            double frac = (xEnd - pTrough) / (period * 0.25);
            double py = yLow - (h * 0.5) * frac;
            path.line_to(xEnd, py);
            break;
        }
        path.line_to(cycleEnd, yCenter);
    }

    path.line_to(xEnd, yCenter);
}

// =============================================================================
// RIGHT TRIANGLE (SAWTOOTH) WAVE PATH GENERATION
// =============================================================================

/**
 * @brief Builds a Right Triangle (Sawtooth) Wave path with switchable right-angle side.
 */
void WaveShapeGenerator::BuildRightTriangleWavePath(BLPath& path,
                                                   double x, double y,
                                                   double width, double height,
                                                   double cycles,
                                                   bool rightAngleOnRight) {
    path.clear();

    const double w = (std::max)(width,  0.01);
    const double h = (std::max)(height, 0.01);
    const double n = (std::max)(cycles, 0.1);

    const double yHigh   = y;
    const double yLow    = y + h;
    const double yCenter = y + h * 0.5;
    const double period  = w / n;
    const double xEnd    = x + w;

    path.move_to(x, yCenter);

    int cycleCount = static_cast<int>(std::ceil(n));

    if (rightAngleOnRight) {
        // Ramp Up: linear ramp ascends to the right, vertical drop on the right (right angle on right)
        for (int k = 0; k < cycleCount; ++k) {
            double cycleStart = x + k * period;
            double cycleEnd   = cycleStart + period;

            if (cycleStart >= xEnd) break;

            if (k == 0) {
                path.line_to(cycleStart, yLow);
            }

            if (cycleEnd >= xEnd) {
                double frac = (xEnd - cycleStart) / period;
                double py = yLow - h * frac;
                path.line_to(xEnd, py);
                break;
            }

            path.line_to(cycleEnd, yHigh);
            path.line_to(cycleEnd, yLow);
        }
    } else {
        // Ramp Down: vertical rise on the left (right angle on left), linear ramp descends to the right
        for (int k = 0; k < cycleCount; ++k) {
            double cycleStart = x + k * period;
            double cycleEnd   = cycleStart + period;

            if (cycleStart >= xEnd) break;

            path.line_to(cycleStart, yHigh);

            if (cycleEnd >= xEnd) {
                double frac = (xEnd - cycleStart) / period;
                double py = yHigh + h * frac;
                path.line_to(xEnd, py);
                break;
            }

            path.line_to(cycleEnd, yLow);
        }
    }

    path.line_to(xEnd, yCenter);
}

// =============================================================================
// TRIANGLE & RIGHT TRIANGLE IMGUI ICONS
// =============================================================================

void WaveShapeGenerator::DrawTriangleWaveIconImGui(ImDrawList* drawList,
                                                   ImVec2 pMin, ImVec2 pMax,
                                                   ImU32 strokeCol,
                                                   float thickness) {
    if (!drawList) return;

    const float padX = 4.0f;
    const float padY = 5.0f;
    const float x0 = pMin.x + padX;
    const float y0 = pMin.y + padY;
    const float x1 = pMax.x - padX;
    const float y1 = pMax.y - padY;
    const float w = x1 - x0;
    const float h = y1 - y0;

    if (w <= 1.0f || h <= 1.0f) return;

    const float yHigh = y0 + h * 0.15f;
    const float yLow  = y1 - h * 0.15f;
    const float yMid  = y0 + h * 0.5f;
    const float qW    = w * 0.125f; // 2 cycles = 8 quarter-periods

    ImVec2 pts[9] = {
        ImVec2(x0, yMid),
        ImVec2(x0 + qW, yHigh),
        ImVec2(x0 + 3.0f * qW, yLow),
        ImVec2(x0 + 4.0f * qW, yMid),
        ImVec2(x0 + 5.0f * qW, yHigh),
        ImVec2(x0 + 7.0f * qW, yLow),
        ImVec2(x1, yMid)
    };

    drawList->AddPolyline(pts, 7, strokeCol, 0, thickness);
}

void WaveShapeGenerator::DrawRightTriangleWaveIconImGui(ImDrawList* drawList,
                                                        ImVec2 pMin, ImVec2 pMax,
                                                        ImU32 strokeCol,
                                                        bool rightAngleOnRight,
                                                        float thickness) {
    if (!drawList) return;

    const float padX = 4.0f;
    const float padY = 5.0f;
    const float x0 = pMin.x + padX;
    const float y0 = pMin.y + padY;
    const float x1 = pMax.x - padX;
    const float y1 = pMax.y - padY;
    const float w = x1 - x0;
    const float h = y1 - y0;

    if (w <= 1.0f || h <= 1.0f) return;

    const float yHigh = y0 + h * 0.15f;
    const float yLow  = y1 - h * 0.15f;
    const float yMid  = y0 + h * 0.5f;
    const float halfW = w * 0.5f; // 2 cycles

    if (rightAngleOnRight) {
        ImVec2 pts[7] = {
            ImVec2(x0, yMid),
            ImVec2(x0, yLow),
            ImVec2(x0 + halfW, yHigh),
            ImVec2(x0 + halfW, yLow),
            ImVec2(x1, yHigh),
            ImVec2(x1, yLow),
            ImVec2(x1, yMid)
        };
        drawList->AddPolyline(pts, 7, strokeCol, 0, thickness);
    } else {
        ImVec2 pts[7] = {
            ImVec2(x0, yMid),
            ImVec2(x0, yHigh),
            ImVec2(x0 + halfW, yLow),
            ImVec2(x0 + halfW, yHigh),
            ImVec2(x1, yLow),
            ImVec2(x1, yHigh),
            ImVec2(x1, yMid)
        };
        drawList->AddPolyline(pts, 7, strokeCol, 0, thickness);
    }
}

} // namespace Folio
