/**
 * =========================================================================================
 * @file core/overlay/mock_dummy_overlay.hpp
 * @brief Demonstration Mock Overlay Subsystem for Layer 3 Interactive Pipeline
 *
 * Capabilities Demonstrated:
 *   1. Sub-frame 60 FPS animation loop with rolling framerate counter.
 *   2. Sinusoidal oscillating geometry and dynamic color cycling.
 *   3. Interactive clickable button with click accumulator and background color toggles.
 *   4. Strict input isolation: events consumed here do NOT bubble to canvas gizmos.
 *   5. Zero dynamic memory allocation in OnUpdate() and OnRenderOverlay().
 * =========================================================================================
 */

#pragma once

#include "core/overlay/interactive_overlay.hpp"
#include "core/text/font_manager.hpp"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <array>

namespace Folio {

class MockDummyOverlay : public IInteractiveOverlay {
public:
    MockDummyOverlay() = default;
    ~MockDummyOverlay() override = default;

    // =========================================================================
    // LIFECYCLE
    // =========================================================================

    [[nodiscard]] bool Initialize() override {
        // Pre-cache fonts during initialization to satisfy zero-allocation render loop constraint
        cachedFont = FontManager::Instance().GetFont("Segoe UI", 12.0f, false);
        buttonFont = FontManager::Instance().GetFont("Segoe UI", 11.0f, true);
        isInitialized = true;
        return true;
    }

    void Shutdown() override {
        isInitialized = false;
    }

    // =========================================================================
    // ENGINE TICK (UPDATE)
    // =========================================================================

    /**
     * @brief Advances animation timers and calculates instantaneous FPS.
     * Guaranteed zero heap allocation.
     */
    void OnUpdate(uint64_t nowMs, double deltaTime) override {
        (void)nowMs;
        if (deltaTime > 0.00001) {
            // Exponential moving average filter for smooth FPS presentation
            // alpha = 0.1 gives 90% history weight, 10% new sample
            const double instantFps = 1.0 / deltaTime;
            currentFps = (currentFps * 0.9) + (instantFps * 0.1);
        }

        // Advance sinusoidal phase (speed: 3 radians per second)
        oscillationPhase += static_cast<float>(deltaTime * 3.0);
        if (oscillationPhase > 6.2831853f) {
            oscillationPhase -= 6.2831853f;
        }
    }

    // =========================================================================
    // RENDERING
    // =========================================================================

    /**
     * @brief Renders the mock overlay on the screen composite context.
     * Guaranteed zero heap allocation: uses stack-allocated char buffers for text formatting.
     */
    void OnRenderOverlay(BLContext& ctx, const OverlayRect& screenRect) override {
        if (screenRect.IsEmpty()) return;

        ctx.save();

        // 1. Clip to screen bounds with rounded corners
        constexpr double cornerRadius = 6.0;
        ctx.clip_to_round_rect(BLRoundRect(screenRect.x, screenRect.y,
                                           screenRect.width, screenRect.height,
                                           cornerRadius, cornerRadius));

        // 2. Toggled background tint based on click state
        static constexpr std::array<BLRgba32, 4> bgPalette = {
            BLRgba32(0x1E, 0x22, 0x2D, 0xF5), // Slate dark
            BLRgba32(0x1B, 0x2E, 0x3D, 0xF5), // Navy blue
            BLRgba32(0x19, 0x36, 0x2D, 0xF5), // Deep emerald
            BLRgba32(0x33, 0x1E, 0x36, 0xF5)  // Royal amethyst
        };
        ctx.set_fill_style(bgPalette[paletteIndex % bgPalette.size()]);
        ctx.fill_round_rect(BLRoundRect(screenRect.x, screenRect.y,
                                        screenRect.width, screenRect.height,
                                        cornerRadius, cornerRadius));

        // 3. Subsystem Header Bar
        ctx.set_fill_style(BLRgba32(0x14, 0x17, 0x1F, 0xE0));
        ctx.fill_rect(BLRect(screenRect.x, screenRect.y, screenRect.width, 24.0));

        // 4. Running 60 FPS counter (formatted into stack buffer)
        char fpsText[48];
        std::snprintf(fpsText, sizeof(fpsText), "Live Overlay | FPS: %5.1f", currentFps);
        ctx.set_fill_style(BLRgba32(0x00, 0xE5, 0xFF, 0xFF)); // Neon Cyan
        ctx.fill_utf8_text(BLPoint(screenRect.x + 8.0, screenRect.y + 16.0), cachedFont, fpsText);

        // 5. Sinusoidal Oscillating Colored Bar
        // Math: t in [0.0, 1.0] from sin(phase)
        const double t = 0.5 + 0.5 * std::sin(static_cast<double>(oscillationPhase));
        const double barMargin = 10.0;
        const double availableWidth = std::max(10.0, static_cast<double>(screenRect.width) - (barMargin * 2.0));
        const double barWidth = availableWidth * 0.6;
        const double travelRange = availableWidth - barWidth;
        const double barX = screenRect.x + barMargin + (t * travelRange);
        const double barY = screenRect.y + 32.0;

        // Dynamic color shifting from Electric Blue to Vivid Violet
        const uint8_t r = static_cast<uint8_t>(0x3B + t * (0x93 - 0x3B));
        const uint8_t g = static_cast<uint8_t>(0x82 + (1.0 - t) * (0x3B - 0x82));
        const uint8_t b = static_cast<uint8_t>(0xF6);
        ctx.set_fill_style(BLRgba32(r, g, b, 0xFF));
        ctx.fill_round_rect(BLRoundRect(barX, barY, barWidth, 12.0, 3.0, 3.0));

        // 6. Interactive Click Me Button
        ComputeButtonRect(screenRect);
        const bool pressed = isButtonPressed;
        const bool hovered = isButtonHovered;

        // Button background
        if (pressed) {
            ctx.set_fill_style(BLRgba32(0x25, 0x63, 0xEB, 0xFF)); // Active blue
        } else if (hovered) {
            ctx.set_fill_style(BLRgba32(0x3B, 0x82, 0xF6, 0xEE)); // Hover light blue
        } else {
            ctx.set_fill_style(BLRgba32(0x27, 0x2D, 0x3B, 0xDD)); // Resting dark slate
        }
        ctx.fill_round_rect(BLRoundRect(cachedButtonRect.x, cachedButtonRect.y,
                                        cachedButtonRect.width, cachedButtonRect.height,
                                        4.0, 4.0));

        // Button outline
        ctx.set_stroke_style(hovered ? BLRgba32(0x60, 0xA5, 0xFA, 0xFF) : BLRgba32(0x4B, 0x55, 0x63, 0xFF));
        ctx.set_stroke_width(1.0);
        ctx.stroke_round_rect(BLRoundRect(cachedButtonRect.x, cachedButtonRect.y,
                                          cachedButtonRect.width, cachedButtonRect.height,
                                          4.0, 4.0));

        // Button text (stack buffer)
        char btnText[64];
        std::snprintf(btnText, sizeof(btnText), "Click Me: %u clicks", clickCount);
        ctx.set_fill_style(BLRgba32(0xFF, 0xFF, 0xFF, 0xFF));
        ctx.fill_utf8_text(BLPoint(cachedButtonRect.x + 12.0, cachedButtonRect.y + 18.0),
                           buttonFont, btnText);

        ctx.restore();
    }

    // =========================================================================
    // INPUT ROUTING
    // =========================================================================

    /**
     * @brief Consumes mouse events within overlay bounds.
     *
     * Input Isolation Test:
     *   Clicking the button increments the click counter and cycles the background
     *   tint. Returning true ensures the event never reaches CanvasEngine, proving
     *   that active overlays isolate input without disturbing canvas selection or gizmos.
     */
    [[nodiscard]] bool OnInputEvent(const SDL_Event& event, const OverlayRect& screenRect) override {
        ComputeButtonRect(screenRect);

        switch (event.type) {
            case SDL_EVENT_MOUSE_MOTION: {
                const int mx = static_cast<int>(event.motion.x);
                const int my = static_cast<int>(event.motion.y);
                isButtonHovered = cachedButtonRect.Contains(mx, my);
                // Consume motion if cursor is inside this overlay so canvas hover doesn't trigger
                return screenRect.Contains(mx, my);
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    const int mx = static_cast<int>(event.button.x);
                    const int my = static_cast<int>(event.button.y);
                    if (cachedButtonRect.Contains(mx, my)) {
                        isButtonPressed = true;
                        clickCount++;
                        paletteIndex = (paletteIndex + 1) % 4;
                        return true; // Consumed by button
                    }
                    if (screenRect.Contains(mx, my)) {
                        return true; // Consumed by overlay card
                    }
                }
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    const bool wasPressed = isButtonPressed;
                    isButtonPressed = false;
                    const int mx = static_cast<int>(event.button.x);
                    const int my = static_cast<int>(event.button.y);
                    if (wasPressed || screenRect.Contains(mx, my)) {
                        return true; // Consumed
                    }
                }
                break;
            }

            default:
                break;
        }

        return false;
    }

    // Duplication support
    [[nodiscard]] std::unique_ptr<IInteractiveOverlay> CloneOverlay() const override {
        auto clone = std::make_unique<MockDummyOverlay>();
        clone->paletteIndex = paletteIndex;
        clone->clickCount = clickCount;
        return clone;
    }

    [[nodiscard]] uint32_t GetClickCount() const noexcept {
        return clickCount;
    }

private:
    void ComputeButtonRect(const OverlayRect& screenRect) noexcept {
        cachedButtonRect.x = screenRect.x + 12;
        cachedButtonRect.y = screenRect.y + 52;
        cachedButtonRect.width = std::clamp(screenRect.width - 24, 60, 180);
        cachedButtonRect.height = 26;
    }

    bool isInitialized = false;
    BLFont cachedFont;
    BLFont buttonFont;

    double currentFps = 60.0;
    float oscillationPhase = 0.0f;

    uint32_t clickCount = 0;
    size_t paletteIndex = 0;
    bool isButtonHovered = false;
    bool isButtonPressed = false;

    OverlayRect cachedButtonRect;
};

} // namespace Folio
