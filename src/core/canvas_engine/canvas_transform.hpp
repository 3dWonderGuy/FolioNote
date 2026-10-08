#pragma once

#include <algorithm>
#include <cmath>
#include <blend2d/blend2d.h>
#include "core/canvas_engine/stroke_smoother.hpp"
#include "core/spatial/aabb.hpp"

enum class PaperStyle { Grid, Lined, Blank, Dotted, Cornell };
enum class PageBorderType { Automatic, Fixed };
enum class PageBorderStyle { Continuous, Dashed, Corners };
enum class PageSizeFormat { Letter, A4, A3, A5, Custom };

enum class CanvasInfinityMode {
    SemiInfinity,      // OneNote style: origin (0, 0), extends right and down
    FullInfinity,      // Unbounded 2D infinity in all directions
    VerticalScroll,    // Bounded page width, infinite vertical scroll
    HorizontalScroll   // Bounded page height, infinite horizontal scroll
};

/**
 * @brief Represents the visible camera viewport on canvas.
 * Defaults to a standard 1080p canvas window surface rather than an empty box.
 */
struct Viewport {
    AABB bounds{ 0.0, 0.0, 1920.0, 1080.0 };
    AABB visibleWorldBounds{ 0.0, 0.0, 1920.0, 1080.0 };
    double zoom = 1.0;
    double pixelsPerMm = 3.779527559; // Standard 96 DPI screen density (96.0 / 25.4)
    double cameraX = 0.0;
    double cameraY = 0.0;
    BLMatrix2D worldToScreenMatrix{ 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };

    [[nodiscard]] constexpr double GetEffectiveScale() const noexcept {
        return pixelsPerMm * zoom;
    }

    [[nodiscard]] Point2D WorldToScreen(double worldXMm, double worldYMm) const noexcept {
        const double scale = GetEffectiveScale();
        return {
            (worldXMm + cameraX) * scale,
            (worldYMm + cameraY) * scale,
            1.0f,
            0.0
        };
    }

    /**
     * @brief Transforms an axis-aligned bounding box from world space (mm) to screen coordinates (px).
     *
     * MATHEMATICAL TRANSFORMATION:
     * - Uses the 2D affine transformation matrix `worldToScreenMatrix`:
     *     [ x_s ]   [ m00 m10 m20 ] [ x_w ]
     *     [ y_s ] = [ m01 m11 m21 ] [ y_w ]
     *     [  1  ]   [  0   0   1  ] [  1  ]
     * - Projects all 4 corner vertices of `worldBox` to accurately enclose rotated/scaled geometry.
     * - Evaluates extrema:
     *     x_min = min(x_0, x_1, x_2, x_3),  x_max = max(x_0, x_1, x_2, x_3)
     *     y_min = min(y_0, y_1, y_2, y_3),  y_max = max(y_0, y_1, y_2, y_3)
     * - Returns screen rectangle: BLRect(x_min, y_min, x_max - x_min, y_max - y_min).
     *
     * @param worldBox Axis-aligned bounding box in world millimeters.
     * @return BLRect Projected axis-aligned rectangle in screen pixels.
     */
    [[nodiscard]] BLRect WorldToScreenRect(const AABB& worldBox) const noexcept {
        if (worldBox.IsEmpty() || !worldBox.IsFinite()) {
            return BLRect(0.0, 0.0, 0.0, 0.0);
        }

        // Project all 4 corners through the affine matrix
        const double px[4] = {
            worldBox.minX * worldToScreenMatrix.m00 + worldBox.minY * worldToScreenMatrix.m10 + worldToScreenMatrix.m20,
            worldBox.maxX * worldToScreenMatrix.m00 + worldBox.minY * worldToScreenMatrix.m10 + worldToScreenMatrix.m20,
            worldBox.maxX * worldToScreenMatrix.m00 + worldBox.maxY * worldToScreenMatrix.m10 + worldToScreenMatrix.m20,
            worldBox.minX * worldToScreenMatrix.m00 + worldBox.maxY * worldToScreenMatrix.m10 + worldToScreenMatrix.m20
        };
        const double py[4] = {
            worldBox.minX * worldToScreenMatrix.m01 + worldBox.minY * worldToScreenMatrix.m11 + worldToScreenMatrix.m21,
            worldBox.maxX * worldToScreenMatrix.m01 + worldBox.minY * worldToScreenMatrix.m11 + worldToScreenMatrix.m21,
            worldBox.maxX * worldToScreenMatrix.m01 + worldBox.maxY * worldToScreenMatrix.m11 + worldToScreenMatrix.m21,
            worldBox.minX * worldToScreenMatrix.m01 + worldBox.maxY * worldToScreenMatrix.m11 + worldToScreenMatrix.m21
        };

        const double minX = (std::min)({ px[0], px[1], px[2], px[3] });
        const double maxX = (std::max)({ px[0], px[1], px[2], px[3] });
        const double minY = (std::min)({ py[0], py[1], py[2], py[3] });
        const double maxY = (std::max)({ py[0], py[1], py[2], py[3] });

        return BLRect(minX, minY, (std::max)(0.0, maxX - minX), (std::max)(0.0, maxY - minY));
    }
};

class CanvasTransform {
public:
    // Physical state in millimeters (mm)
    double panXMm = 0.0;
    double panYMm = 0.0;
    double zoom = 1.0;
    CanvasInfinityMode infinityMode = CanvasInfinityMode::SemiInfinity;

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

    [[nodiscard]] BLRect WorldToScreenRect(const AABB& worldBox) const noexcept {
        if (worldBox.IsEmpty() || !worldBox.IsFinite()) {
            return BLRect(0.0, 0.0, 0.0, 0.0);
        }
        Point2D s0 = WorldToScreen(worldBox.minX, worldBox.minY);
        Point2D s1 = WorldToScreen(worldBox.maxX, worldBox.maxY);
        double minX = (std::min)(s0.x, s1.x);
        double minY = (std::min)(s0.y, s1.y);
        double maxX = (std::max)(s0.x, s1.x);
        double maxY = (std::max)(s0.y, s1.y);
        return BLRect(minX, minY, (std::max)(0.0, maxX - minX), (std::max)(0.0, maxY - minY));
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
        if (infinityMode == CanvasInfinityMode::FullInfinity) {
            // Unbounded pan in all directions
            return;
        }
        if (infinityMode == CanvasInfinityMode::SemiInfinity) {
            if (panXMm > 0.0) panXMm = 0.0;
            if (panYMm > 0.0) panYMm = 0.0;
            return;
        }
        if (infinityMode == CanvasInfinityMode::VerticalScroll) {
            if (panYMm > 0.0) panYMm = 0.0;
            return;
        }
        if (infinityMode == CanvasInfinityMode::HorizontalScroll) {
            if (panXMm > 0.0) panXMm = 0.0;
            return;
        }
    }

    [[nodiscard]] Viewport GetVisibleViewportMm(int viewportPixelW, int viewportPixelH) const noexcept {
        Point2D minWorld = ScreenToWorld(0.0, 0.0);
        Point2D maxWorld = ScreenToWorld(viewportPixelW, viewportPixelH);
        Viewport vp;
        vp.bounds = AABB{ minWorld.x, minWorld.y, maxWorld.x, maxWorld.y };
        vp.visibleWorldBounds = vp.bounds;
        vp.zoom = zoom;
        vp.pixelsPerMm = pixelsPerMm;
        vp.cameraX = panXMm;
        vp.cameraY = panYMm;
        vp.worldToScreenMatrix = GetBlend2DTransformMatrix();
        return vp;
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