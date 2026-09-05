#pragma once

#include <algorithm>
#include <cmath>
#include <blend2d/blend2d.h>
#include "core/engine/stroke_smoother.hpp"
#include "core/spatial/aabb.hpp"

class CanvasTransform {
public:
    // Physical state in millimeters (mm)
    double panXMm = 0.0;
    double panYMm = 0.0;
    double zoom = 1.0;

    // Display DPI metrics
    // Standard desktop fallback: 96 DPI -> 96.0 / 25.4 ~= 3.779527559 pixels/mm
    double pixelsPerMm = 3.779527559;

    void SetDPI(float displayDpi) noexcept {
        if (displayDpi > 10.0f) {
            pixelsPerMm = static_cast<double>(displayDpi) / 25.4;
        }
    }

    [[nodiscard]] constexpr double GetEffectiveScale() const noexcept {
        return pixelsPerMm * zoom;
    }

    // --- Coordinate Transformations ---

    [[nodiscard]] Point2D ScreenToWorld(double screenX, double screenY) const noexcept {
        const double scale = GetEffectiveScale();
        return {
            (screenX / scale) - panXMm,
            (screenY / scale) - panYMm,
            1.0f,
            0.0
        };
    }

    [[nodiscard]] Point2D WorldToScreen(double worldXMm, double worldYMm) const noexcept {
        const double scale = GetEffectiveScale();
        return {
            (worldXMm + panXMm) * scale,
            (worldYMm + panYMm) * scale,
            1.0f,
            0.0
        };
    }

    // Convert pixel vectors (e.g., mouse delta) into world millimeter vectors
    [[nodiscard]] Point2D ScreenDeltaToWorldDelta(double screenDx, double screenDy) const noexcept {
        const double scale = GetEffectiveScale();
        return { screenDx / scale, screenDy / scale, 1.0f, 0.0 };
    }

    // --- Navigation & Viewport Control ---

    void PanByScreenPixels(double screenDx, double screenDy) noexcept {
        const double scale = GetEffectiveScale();
        panXMm += screenDx / scale;
        panYMm += screenDy / scale;
        ClampPan();
    }

    void PanByWorldMm(double deltaXMm, double deltaYMm) noexcept {
        panXMm += deltaXMm;
        panYMm += deltaYMm;
        ClampPan();
    }

    void ZoomAtScreenPoint(double cursorScreenX, double cursorScreenY, double factor) noexcept {
        const double oldZoom = zoom;
        const double newZoom = std::clamp(oldZoom * factor, 0.25, 8.0);
        if (std::abs(newZoom - oldZoom) < 0.0001) return;

        // Anchor world point under the cursor during zoom
        const double oldScale = pixelsPerMm * oldZoom;
        const double newScale = pixelsPerMm * newZoom;

        const double worldAnchorX = (cursorScreenX / oldScale) - panXMm;
        const double worldAnchorY = (cursorScreenY / oldScale) - panYMm;

        zoom = newZoom;
        panXMm = (cursorScreenX / newScale) - worldAnchorX;
        panYMm = (cursorScreenY / newScale) - worldAnchorY;

        ClampPan();
    }

    void ClampPan() noexcept {
        if (panXMm > 0.0) panXMm = 0.0;
        if (panYMm > 0.0) panYMm = 0.0;
    }

    [[nodiscard]] Viewport GetVisibleViewportMm(int viewportPixelW, int viewportPixelH) const noexcept {
        Point2D minWorld = ScreenToWorld(0.0, 0.0);
        Point2D maxWorld = ScreenToWorld(viewportPixelW, viewportPixelH);
        return {
            AABB{ minWorld.x, minWorld.y, maxWorld.x, maxWorld.y },
            zoom
        };
    }

    // Generates the 2D affine transformation matrix for Blend2D rendering passes
    [[nodiscard]] BLMatrix2D GetBlend2DTransformMatrix() const noexcept {
        const double scale = GetEffectiveScale();
        BLMatrix2D mat;
        mat.reset();
        mat.translate(panXMm * scale, panYMm * scale);
        mat.scale(scale, scale);
        return mat;
    }
};