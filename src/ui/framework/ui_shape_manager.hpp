#pragma once
/**
 * =========================================================================================
 * @file ui/framework/ui_shape_manager.hpp
 * @brief High-End Surface Drawing & Geometric Primitives for FolioNote UI
 * =========================================================================================
 *
 * GENERAL ARCHITECTURE & DESIGN:
 * ------------------------------
 * While Dear ImGui provides raw AddRect/AddRectFilled primitives, premium modern interfaces
 * require layered visual depth:
 * 1. Multi-tier drop shadows with soft alpha falloff mimicking ambient occlusion.
 * 2. Translucent acrylic glassmorphism with top-edge specular highlights.
 * 3. Smooth continuous corner rounding (squircles) that avoid harsh visual tangent breaks.
 * 4. Micro-glows and status pills.
 *
 * UIShapeManager encapsulates all ImDrawList vector generation for these surfaces.
 */

#include "imgui.h"
#include "ui/framework/ui_tokens.hpp"
#include <string>

namespace Folio::UI {

/**
 * @class UIShapeManager
 * @brief Geometry generator for modern panels, shadows, cards, and capsules.
 */
class UIShapeManager {
public:
    /**
     * @brief Renders a multi-pass blurred ambient drop shadow beneath an element.
     *
     * MATHEMATICAL PROCESS:
     * Decomposes the elevation depth into multiple concentric blurred layers:
     * - Layer 1 (Sharp): Offset (0, elevation * 0.4), spread 2px, alpha 0.18
     * - Layer 2 (Ambient): Offset (0, elevation * 0.8), spread elevation * 0.9, alpha 0.10
     *
     * @param drawList Target ImDrawList.
     * @param pMin Top-left corner of the casting element.
     * @param pMax Bottom-right corner of the casting element.
     * @param elevation Virtual height above background in points (e.g. Elevation::medium).
     * @param rounding Corner radius in points (e.g. Radii::md).
     */
    static void DrawShadow(
        ImDrawList* drawList,
        ImVec2 pMin,
        ImVec2 pMax,
        float elevation = Elevation::medium,
        float rounding = Radii::md
    );

    /**
     * @brief Renders a modern glassmorphic acrylic panel with subtle specular borders.
     *
     * LAYERING PROCESS:
     * 1. Ambient Drop Shadow (if elevation > 0).
     * 2. Translucent Base Acrylic Fill with subtle vertical gradient (slightly lighter at top).
     * 3. Perimeter Border with alpha falloff.
     * 4. Top-edge Specular Highlight (1px line simulating light reflection from overhead source).
     *
     * @param drawList Target ImDrawList.
     * @param pMin Top-left corner.
     * @param pMax Bottom-right corner.
     * @param baseFill Base background color (ImU32).
     * @param borderCol Border stroke color (ImU32).
     * @param rounding Corner radius.
     * @param elevation Shadow elevation.
     */
    static void DrawAcrylicPanel(
        ImDrawList* drawList,
        ImVec2 pMin,
        ImVec2 pMax,
        ImU32 baseFill,
        ImU32 borderCol,
        float rounding = Radii::md,
        float elevation = Elevation::low
    );

    /**
     * @brief Renders a smooth card surface with optional hover/active transition alpha.
     *
     * @param drawList Target ImDrawList.
     * @param pMin Top-left bounds.
     * @param pMax Bottom-right bounds.
     * @param fill Resting fill color.
     * @param hoverFill Hover fill color.
     * @param border Resting border color.
     * @param hoverBorder Hover border color.
     * @param hoverAlpha Interpolated transition in [0.0, 1.0].
     * @param rounding Corner radius.
     */
    static void DrawInteractiveCard(
        ImDrawList* drawList,
        ImVec2 pMin,
        ImVec2 pMax,
        ImU32 fill,
        ImU32 hoverFill,
        ImU32 border,
        ImU32 hoverBorder,
        float hoverAlpha,
        float rounding = Radii::md
    );

    /**
     * @brief Renders a pill-shaped status capsule badge with centered text.
     *
     * @param drawList Target ImDrawList.
     * @param center Center coordinates.
     * @param text Text label.
     * @param bgCol Background capsule fill.
     * @param textCol Text color.
     * @param font Optional font (nullptr for current).
     */
    static void DrawPillBadge(
        ImDrawList* drawList,
        ImVec2 center,
        const char* text,
        ImU32 bgCol,
        ImU32 textCol,
        ImFont* font = nullptr
    );

    /**
     * @brief Renders a soft radial glow around a button or indicator dot.
     *
     * @param drawList Target ImDrawList.
     * @param center Center point.
     * @param radius Glow spread radius.
     * @param glowColor Glow color (alpha decays radially).
     */
    static void DrawGlow(
        ImDrawList* drawList,
        ImVec2 center,
        float radius,
        ImU32 glowColor
    );
};

} // namespace Folio::UI
