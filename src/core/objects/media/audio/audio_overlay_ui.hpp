#pragma once
/**
 * =========================================================================================
 * @file core/objects/media/audio/audio_overlay_ui.hpp
 * @brief Floating Modern Glassmorphic Controller Capsule HUD for Selected Audio Objects
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & INTERACTION MODEL:
 * -----------------------------------------
 * When an AudioObject is selected on the canvas, this interactive floating capsule HUD
 * is projected directly below the audio chip badge in screen-space.
 *
 * Features:
 *  1. Dynamic Screen Projection:
 *     - Projects world-space anchor P_world = (worldX + chipW * 0.5, worldY + chipH + 2.5 mm)
 *       into screen-space coordinates using CanvasTransform::WorldToScreen().
 *     - Automatically tracks the audio badge during canvas panning, zooming, and dragging.
 *  2. Modern Dark Glassmorphism Styling:
 *     - Capsule geometry with rounded borders (rounding = 18.0f).
 *     - Translucent dark surface with electric violet neon accents.
 *  3. Tactile Media Controls:
 *     - Play / Pause toggle with glow indicators.
 *     - Stop & rewind button.
 *     - Interactive seek scrubber slider with mm:ss / mm:ss formatted timestamps.
 *     - Compact volume slider and mute toggle.
 */

#include <memory>
#include <functional>
#include <imgui.h>

#include "core/canvas_engine/transform/canvas_transform.hpp"

namespace Folio {

class AudioObject;

class AudioOverlayUI {
public:
    /**
     * @brief Renders the floating modern glassmorphic controller capsule HUD for a selected AudioObject.
     *
     * Mathematical Coordinate Process:
     * 1. Anchor Calculation:
     *    Badge anchor is placed at bottom-center in world mm:
     *      worldAnchorX = audio.worldX + audio.worldWidth * 0.5;
     *      worldAnchorY = audio.worldY + audio.worldHeight + 2.5; // 2.5 mm gap
     * 2. Viewport Coordinate Mapping:
     *    screenAnchor = transform.WorldToScreen(worldAnchorX, worldAnchorY);
     *    finalPos = ImVec2(canvasOrigin.x + screenAnchor.x - capsuleWidth * 0.5f,
     *                      canvasOrigin.y + screenAnchor.y);
     *
     * @param audio The AudioObject instance being controlled.
     * @param transform Canvas transform holding current pan, zoom, and DPI scale.
     * @param canvasOrigin Screen-space origin of the canvas viewport.
     * @param onDirty Repaint callback invoked when playback or slider state changes.
     */
    static void Render(AudioObject& audio, const CanvasTransform& transform,
                       const ImVec2& canvasOrigin, std::function<void()> onDirty = nullptr);
};

} // namespace Folio
