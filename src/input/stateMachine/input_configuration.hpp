/**
 * @file input_configuration.hpp
 * @brief Centralized configuration constants and tuning parameters for the input subsystem.
 *
 * PURPOSE & RATIONALE:
 * --------------------
 * Digital ink, multi-touch navigation, and multimodal device arbitration rely on precise
 * temporal windows (milliseconds) and spatial distance thresholds (pixels / millimeters).
 * Hardcoding these magic numbers across multiple dispatchers leads to maintenance friction,
 * inconsistent gesture feel, and difficulty when calibrating for different hardware form-factors.
 *
 * This configuration structure unifies all timing, distance, and threshold constants:
 *   - Device arbitration grace periods (palm rejection & device switching)
 *   - Gesture thresholds (tap vs. hold vs. scroll vs. pinch)
 *   - Special action detection (double-click intervals, multi-finger tap timeouts)
 *   - Viewport navigation multipliers (mouse wheel zoom factors)
 *   - Object hit-testing tolerances
 *
 * FUTURE EXPANSION:
 * -----------------
 * All values are grouped in a plain-old-data (POD) struct with defaults. In future releases,
 * this struct will directly map to/from a user-facing JSON settings file
 * (e.g. `assets/config/input_settings.json`), allowing users to tune stylus pressure sensitivity,
 * hold durations, or double-tap speeds from a settings UI.
 */

#pragma once
#include <cstdint>

namespace FolioInput {

/**
 * @struct InputConfiguration
 * @brief Holds all tunable constants and timing thresholds for InputStateMachine and dispatchers.
 */
struct InputConfiguration {
    // =========================================================================
    // 1. DEVICE ARBITRATION GRACE WINDOWS (Milliseconds)
    // =========================================================================
    // When a user writes with a stylus, their palm or wrist frequently rests on the
    // screen, or the pen tip briefly lifts between cursive letters / strokes.
    // To prevent the capacitive touch digitizer or mouse from prematurely hijacking
    // input during these brief intervals, the state machine enforces grace periods.

    /**
     * @brief Grace window during which Stylus retains priority after lifting off glass.
     *
     * Unit: Milliseconds. Default: 600 ms.
     * Rationale: Average natural pause between handwritten words/strokes is 200-500ms.
     * 600ms guarantees smooth cursive flow without accidental palm-pan triggering.
     */
    uint64_t penGracePeriodMs = 600;

    /**
     * @brief Grace window during which Touch retains priority after fingers lift.
     *
     * Unit: Milliseconds. Default: 250 ms.
     * Rationale: Prevents mouse cursor snapping or synthetic events from interrupting
     * multi-touch flick/scroll momentum.
     */
    uint64_t touchGracePeriodMs = 250;

    // =========================================================================
    // 2. GESTURE & TOUCH DETECTION THRESHOLDS
    // =========================================================================

    /**
     * @brief Spatial slop threshold for distinguishing stationary taps from drag/scroll.
     *
     * Unit: Pixels (screen space). Default: 8.0 px.
     * Math: Movement distance D = hypot(currX - startX, currY - startY).
     * If D < dragThresholdPx, the contact is considered stationary (eligible for Tap or Hold).
     * If D >= dragThresholdPx, the contact transitions immediately into Scrolling/Panning.
     */
    float dragThresholdPx = 8.0f;

    /**
     * @brief Minimum dwell duration to trigger a Press-and-Hold (long-press) action.
     *
     * Unit: Milliseconds. Default: 450 ms.
     * Math: Delta time dt = (nowMs - touchStartTimeMs).
     * If dt >= holdTimeMs while D < dragThresholdPx, PressAndHold fires (e.g. context menu).
     */
    uint64_t holdTimeMs = 450;

    /**
     * @brief Maximum duration for a multi-finger (2-finger or 3-finger) tap-and-release.
     *
     * Unit: Milliseconds. Default: 250 ms.
     * Rationale: Quick 2-finger tap triggers Undo; 3-finger tap triggers Redo.
     * Contacts lasting longer than this are classified as pinch/pan gestures instead.
     */
    uint64_t multiFingerTapMaxDurationMs = 250;

    // =========================================================================
    // 3. SPECIAL ACTIONS & DOUBLE-TAP DETECTION
    // =========================================================================

    /**
     * @brief Maximum elapsed time between two consecutive clicks/taps to count as a double-click.
     *
     * Unit: Milliseconds. Default: 300 ms.
     * Math: dt = (tap2TimeMs - tap1TimeMs) <= doubleTapMaxIntervalMs.
     */
    uint64_t doubleTapMaxIntervalMs = 300;

    /**
     * @brief Maximum allowable spatial drift between tap 1 and tap 2 for double-tap recognition.
     *
     * Unit: Pixels. Default: 12.0 px.
     * Math: hypot(tap2.x - tap1.x, tap2.y - tap1.y) <= doubleTapMaxDistancePx.
     */
    float doubleTapMaxDistancePx = 12.0f;

    // =========================================================================
    // 4. VIEWPORT & NAVIGATION MULTIPLIERS
    // =========================================================================

    /**
     * @brief Multiplicative zoom scale applied per positive notch of the mouse scroll wheel.
     *
     * Default: 1.15f (+15% scale per wheel tick).
     */
    float mouseWheelZoomInFactor = 1.15f;

    /**
     * @brief Multiplicative zoom scale applied per negative notch of the mouse scroll wheel.
     *
     * Default: 0.85f (-15% scale per wheel tick).
     */
    float mouseWheelZoomOutFactor = 0.85f;

    /**
     * @brief Minimum scale delta required to trigger zoom recalculation during pinch-to-zoom.
     *
     * Math: |scale - 1.0f| > zoomDeltaEpsilon.
     * Default: 0.001f. Prevents micro-jitter recalculations when fingers are stationary.
     */
    float zoomDeltaEpsilon = 0.001f;

    // =========================================================================
    // 5. OBJECT HIT-TESTING & ERASER GEOMETRY
    // =========================================================================

    /**
     * @brief Radius around click point in millimeters for selecting thin strokes or small objects.
     *
     * Unit: Millimeters (World space). Default: 2.0 mm.
     */
    double objectHitTestRadiusMm = 2.0;

    /**
     * @brief Default spherical eraser cursor radius.
     *
     * Unit: Millimeters (World space). Default: 3.0 mm.
     */
    float defaultEraserRadiusMm = 3.0f;

    /**
     * @brief Resets all configuration parameters to their factory defaults.
     */
    void ResetToDefaults() noexcept {
        *this = InputConfiguration{};
    }
};

} // namespace FolioInput
