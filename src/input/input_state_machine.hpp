#pragma once
#include <SDL3/SDL.h>
#include "input/input_tracker.hpp"
#include "input/pen_palette.hpp"
#include "input/touch_gesture_recognizer.hpp"
#include "utils/logger.hpp"
#include <array>
#include <string>
#include <cstdint>

// =============================================================================
// DEVICE TYPE
// Identifies which physical input source is currently driving interaction.
// =============================================================================
enum class DeviceType : uint8_t {
    Unknown = 0,
    Stylus,   // Pressure-sensitive pen/stylus (highest priority)
    Touch,    // Capacitive finger touch
    Mouse     // Standard mouse / trackpad
};

// =============================================================================
// INTERACTION STATE
// High-level semantic action the active device is performing on the canvas.
// This is what dispatchers (DispatchStylus/Mouse/Touch) act on — not raw HW state.
// =============================================================================
enum class InteractionState : uint8_t {
    Idle = 0,    // No active canvas operation (e.g. pointer / navigation mode)
    Inking,      // Drawing a stroke with the active pen preset
    Eraser,      // Erasing strokes or points
    Selecting,   // Lasso / freehand selection
    Panning,     // Panning the canvas viewport
    Transforming,// Moving / scaling selected objects (future use)
    DrawingShape // Drag-creating vector shapes
};

// =============================================================================
// STYLUS CONTACT STATE
// Tracks whether the pen is out of range, hovering, or pressed against the surface.
// =============================================================================
enum class StylusState : uint8_t {
    OutOfRange = 0,
    Hovering,
    Engaged
};

// =============================================================================
// STYLUS BUTTON STATE
// Maps physical barrel button hardware to semantic button actions.
// =============================================================================
enum class StylusButtonState : uint8_t {
    None = 0,
    BarrelPressed,  // Barrel button 1 (or eraser tip) — used for Eraser override
    Barrel2Pressed  // Barrel button 2 — used for Lasso/Select override
};

// =============================================================================
// PER-DEVICE TOOL STATE
// Each device independently remembers the last tool the user explicitly selected
// for it. When a device becomes active, its savedTool is restored. This lets the
// stylus stay in "Lasso" while you briefly pan with your finger, then come back
// to Lasso naturally when you pick the pen back up.
//
// Barrel-button overrides (Eraser, Lasso) are TRANSIENT — they override
// currentAction only while held and do NOT modify savedTool.
// =============================================================================
struct DeviceToolState {
    InteractionState savedTool = InteractionState::Inking;
};

class CanvasEngine;
class DocumentSession;

class InputStateMachine {
public:
    // -------------------------------------------------------------------------
    // ACTIVE DEVICE
    // Determined each frame by UpdateHardwareState() via priority arbitration.
    // Stylus > Touch > Mouse.
    // -------------------------------------------------------------------------
    DeviceType ActiveDevice = DeviceType::Mouse;

    // -------------------------------------------------------------------------
    // PER-DEVICE TOOL MEMORY
    // Each device has its own saved tool that persists across device switches.
    // Defaults:
    //   Stylus → Inking    (pen draws by default)
    //   Touch  → Panning   (finger navigates by default; "Touch Paint" enables Inking)
    //   Mouse  → Idle      (mouse is a navigation/select device by default)
    //
    // NOTE: Mouse default is intentionally Idle (pointer/box-select navigation).
    // Inking via mouse is currently only enabled for testing — see DispatchMouse().
    // -------------------------------------------------------------------------
    DeviceToolState stylusTool  = { InteractionState::Inking  };
    DeviceToolState touchTool   = { InteractionState::Panning };
    DeviceToolState mouseTool   = { InteractionState::Selecting };

    // -------------------------------------------------------------------------
    // CURRENT ACTION (live / computed each frame)
    // This is what dispatchers read. It may differ from savedTool temporarily
    // due to transient overrides (barrel buttons, space-bar panning, etc.).
    // -------------------------------------------------------------------------
    InteractionState currentAction = InteractionState::Selecting;

    // -------------------------------------------------------------------------
    // STYLUS TELEMETRY
    // -------------------------------------------------------------------------
    StylusState       currentStylusState = StylusState::OutOfRange;
    StylusState       oldStylusState     = StylusState::OutOfRange;
    StylusButtonState stylusButtons      = StylusButtonState::None;
    bool  isStrokeEraser                 = true;
    float eraserRadiusMm                 = 3.0f;
    float lastEraserX                    = 0.0f;
    float lastEraserY                    = 0.0f;
    bool  isEraserActive                 = false;

    // -------------------------------------------------------------------------
    // HARDWARE STATE TRACKERS
    // Raw per-device input data populated by InputManager's Handle*Event methods.
    // -------------------------------------------------------------------------
    PenState   pen;
    MouseState mouse;
    KeyboardState keyboard;
    std::array<TouchSlot, 10> activeFingers{};
    PenPalette palette;

    TouchGestureRecognizer gestureRecognizer;

    // -------------------------------------------------------------------------
    // CANVAS LAYOUT
    // Screen-space offset of the canvas top-left corner (e.g. sidebar + ribbon).
    // All dispatchers subtract this to produce canvas-local coordinates.
    // -------------------------------------------------------------------------
    float canvasOriginX  = 0.0f;
    float canvasOriginY  = 0.0f;
    bool  isCanvasHovered = false;

    // -------------------------------------------------------------------------
    // EVENT TIMESTAMPS
    // -------------------------------------------------------------------------
    uint64_t lastPenTimestampMs   = 0;
    uint64_t lastTouchTimestampMs = 0;
    uint64_t lastMouseTimestampMs = 0;
    double   latestEventTimeSec   = 0.0;
    uint64_t appStartTimeNs       = 0;

    // -------------------------------------------------------------------------
    // TRANSITION FLAGS
    // Tracked across frames to compute justDown / justUp edge events.
    // -------------------------------------------------------------------------
    bool wasMouseDown  = false;
    bool wasTouchDown  = false;
    bool wasMiddleDown = false; // Middle mouse button previous-frame state (for transient pan)

    // -------------------------------------------------------------------------
    // UI CAPTURE FLAGS
    // Set to true when input begins over an ImGui widget. Suppresses canvas
    // dispatch for the full duration of that contact (until release).
    // -------------------------------------------------------------------------
    bool uiCapturedStylus = false;
    bool uiCapturedTouch  = false;
    bool uiCapturedMouse  = false;

    // =========================================================================
    // PUBLIC API
    // =========================================================================

    // Records the app start timestamp for converting SDL nanosecond event
    // timestamps into seconds relative to launch.
    void InitTiming() noexcept {
        appStartTimeNs = SDL_GetTicksNS();
    }

    // Converts a raw SDL nanosecond event timestamp to seconds since app launch.
    [[nodiscard]] double EventTimestampToSec(uint64_t eventTimestampNs) const noexcept {
        if (eventTimestampNs < appStartTimeNs) return 0.0;
        return static_cast<double>(eventTimestampNs - appStartTimeNs) * 1e-9;
    }

    // Returns the saved tool for the currently active device.
    [[nodiscard]] InteractionState GetActiveDeviceTool() const noexcept {
        switch (ActiveDevice) {
            case DeviceType::Stylus: return stylusTool.savedTool;
            case DeviceType::Touch:  return touchTool.savedTool;
            case DeviceType::Mouse:  return mouseTool.savedTool;
            default:                 return InteractionState::Idle;
        }
    }

    // Sets and persists the tool for a specific device.
    // Call this from ribbon UI buttons when the user explicitly picks a tool.
    // This does NOT immediately change currentAction — UpdateHardwareState()
    // will pick it up at the start of the next frame if the device is active.
    void SetToolForDevice(DeviceType device, InteractionState tool) noexcept {
        switch (device) {
            case DeviceType::Stylus: stylusTool.savedTool = tool; break;
            case DeviceType::Touch:  touchTool.savedTool  = tool; break;
            case DeviceType::Mouse:  mouseTool.savedTool  = tool; break;
            default: break;
        }
        // If the changed device is already active, reflect immediately
        if (device == ActiveDevice) {
            currentAction = tool;
        }
        std::string toolName = (tool == InteractionState::Inking) ? "Inking" :
                               (tool == InteractionState::Eraser) ? "Eraser" :
                               (tool == InteractionState::Selecting) ? "Selecting" :
                               (tool == InteractionState::Panning) ? "Panning" :
                               (tool == InteractionState::DrawingShape) ? "DrawingShape" : "Idle";
        LOG_INFO(InputStateMachine, "Set tool for device " + std::to_string(static_cast<int>(device)) + " to " + toolName);
    }

    // Main per-frame entry point. Call once per SDL event after all Handle*Event
    // calls. Performs device arbitration, resolves currentAction, then dispatches
    // high-level semantic events to the canvas and document.
    void ProcessInputState(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput);

private:
    void UpdateHardwareState(uint64_t nowMs);
    void DispatchStylus(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput);
    void DispatchMouse(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput);
    void DispatchTouch(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput);
    size_t GetActiveFingerCount() const noexcept;
};