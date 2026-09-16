/**
 * @file input_state_machine.hpp
 * @brief Definition of the multimodal InputStateMachine coordinating physical input arbitration,
 *        per-device tool memory, and semantic action dispatch.
 */

#pragma once
#include <SDL3/SDL.h>
#include "input/input_tracker.hpp"
#include "input/pen_palette.hpp"
#include "input/touch_gesture_recognizer.hpp"
#include "input/stateMachine/input_configuration.hpp"
#include "input/stateMachine/special_action_manager.hpp"
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
    Stylus,   ///< Pressure-sensitive pen/stylus (highest priority)
    Touch,    ///< Capacitive multi-touch finger contacts
    Mouse     ///< Standard mouse / trackpad
};

// =============================================================================
// INTERACTION STATE
// High-level semantic action the active device is performing on the canvas.
// =============================================================================
enum class InteractionState : uint8_t {
    Idle = 0,     ///< Pointer navigation mode (no active canvas tool)
    Inking,       ///< Drawing a stroke with the active pen preset
    Eraser,       ///< Erasing strokes or segments
    Selecting,    ///< Lasso / freehand / box selection & gizmo manipulation
    Panning,      ///< Panning the canvas viewport
    Transforming, ///< Moving / scaling selected objects
    DrawingShape  ///< Drag-creating geometric vector shapes
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
    BarrelPressed,  ///< Barrel button 1 (or eraser tip) — used for Eraser override
    Barrel2Pressed  ///< Barrel button 2 — used for Lasso/Select override
};

// =============================================================================
// PER-DEVICE TOOL STATE
// Remembers the last tool explicitly selected for a device.
// =============================================================================
struct DeviceToolState {
    InteractionState savedTool = InteractionState::Inking;
};

class CanvasEngine;
class DocumentSession;

/**
 * @class InputStateMachine
 * @brief Manages multimodal input arbitration, per-device tool persistence, and action dispatch.
 */
class InputStateMachine {
public:
    // -------------------------------------------------------------------------
    // ACTIVE DEVICE & ARBITRATION
    // -------------------------------------------------------------------------
    DeviceType ActiveDevice = DeviceType::Mouse;

    // -------------------------------------------------------------------------
    // PER-DEVICE TOOL MEMORY
    // -------------------------------------------------------------------------
    DeviceToolState stylusTool = { InteractionState::Inking  };
    DeviceToolState touchTool  = { InteractionState::Panning };
    DeviceToolState mouseTool  = { InteractionState::Selecting };

    // -------------------------------------------------------------------------
    // CURRENT LIVE ACTION
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
    // HARDWARE TELEMETRY CONTAINERS
    // -------------------------------------------------------------------------
    PenState   pen;
    MouseState mouse;
    KeyboardState keyboard;
    std::array<TouchSlot, 10> activeFingers{};
    PenPalette palette;

    // Gesture recognizer, tuning configuration, and special actions manager
    TouchGestureRecognizer gestureRecognizer;
    FolioInput::InputConfiguration config;
    FolioInput::SpecialActionManager specialActions;

    // -------------------------------------------------------------------------
    // CANVAS LAYOUT & PDF MODE
    // -------------------------------------------------------------------------
    float canvasOriginX      = 0.0f;
    float canvasOriginY      = 0.0f;
    bool  isCanvasHovered    = false;
    bool  isPdfCanvasHovered = false;
    bool  isPdfModeActive    = false;

    // -------------------------------------------------------------------------
    // EVENT TIMESTAMPS
    // -------------------------------------------------------------------------
    uint64_t lastPenTimestampMs   = 0;
    uint64_t lastTouchTimestampMs = 0;
    uint64_t lastMouseTimestampMs = 0;
    double   latestEventTimeSec   = 0.0;
    uint64_t appStartTimeNs       = 0;

    // -------------------------------------------------------------------------
    // TRANSITION & CAPTURE FLAGS
    // -------------------------------------------------------------------------
    bool wasMouseDown     = false;
    bool wasTouchDown     = false;
    bool wasMiddleDown    = false;
    bool uiCapturedStylus = false;
    bool uiCapturedTouch  = false;
    bool uiCapturedMouse  = false;

    // =========================================================================
    // PUBLIC API
    // =========================================================================

    void InitTiming() noexcept {
        appStartTimeNs = SDL_GetTicksNS();
    }

    [[nodiscard]] double EventTimestampToSec(uint64_t eventTimestampNs) const noexcept {
        if (eventTimestampNs < appStartTimeNs) return 0.0;
        return static_cast<double>(eventTimestampNs - appStartTimeNs) * 1e-9;
    }

    [[nodiscard]] InteractionState GetActiveDeviceTool() const noexcept {
        switch (ActiveDevice) {
            case DeviceType::Stylus: return stylusTool.savedTool;
            case DeviceType::Touch:  return touchTool.savedTool;
            case DeviceType::Mouse:  return mouseTool.savedTool;
            default:                 return InteractionState::Idle;
        }
    }

    [[nodiscard]] bool IsShapeDrawingActive() const noexcept {
        return currentAction == InteractionState::DrawingShape;
    }

    void SetToolForDevice(DeviceType device, InteractionState tool) noexcept {
        switch (device) {
            case DeviceType::Stylus: stylusTool.savedTool = tool; break;
            case DeviceType::Touch:  touchTool.savedTool  = tool; break;
            case DeviceType::Mouse:  mouseTool.savedTool  = tool; break;
            default: break;
        }
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

    [[nodiscard]] bool IsDrawingMode() const noexcept {
        return currentAction == InteractionState::Inking;
    }

    [[nodiscard]] bool IsEraserMode() const noexcept {
        return currentAction == InteractionState::Eraser;
    }

    /**
     * @brief Counts the number of currently active finger contacts on screen.
     * @return Number of slots with fingerID != -1.
     */
    [[nodiscard]] size_t GetActiveFingerCount() const noexcept;

    /**
     * @brief Core per-frame entry point called after hardware events have been ingested.
     *
     * @param canvas          Reference to CanvasEngine.
     * @param session         Reference to DocumentSession.
     * @param imguiWantsInput True if Dear ImGui UI chrome has active pointer focus.
     */
    void ProcessInputState(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput);

private:
    void UpdateHardwareState(uint64_t nowMs);
    void DispatchStylus(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput);
    void DispatchMouse(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput);
    void DispatchTouch(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput);
};
