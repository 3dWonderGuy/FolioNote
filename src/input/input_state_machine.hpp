#pragma once
#include <SDL3/SDL.h>
#include "input/input_tracker.hpp"
#include "input/pen_palette.hpp"
#include "input/touch_gesture_recognizer.hpp"
#include <array>
#include <cstdint>

enum class DeviceType : uint8_t {
    Unknown = 0,
    Stylus,
    Touch,
    Mouse
};

enum class InteractionState : uint8_t {
    Idle = 0,
    Inking,
    Eraser,
    Selecting,
    Panning,
    Transforming
};

enum class StylusState : uint8_t {
    OutOfRange = 0,
    Hovering,
    Engaged
};

enum class StylusButtonState : uint8_t {
    None = 0,
    BarrelPressed,
    Barrel2Pressed
};

class CanvasEngine;
class DocumentSession;

class InputStateMachine {
public:
    DeviceType ActiveDevice = DeviceType::Mouse;
    
    // Explicit tool and contact telemetry states
    InteractionState currentAction   = InteractionState::Inking;
    StylusState currentStylusState   = StylusState::OutOfRange;
    StylusState oldStylusState       = StylusState::OutOfRange;
    StylusButtonState stylusButtons  = StylusButtonState::None;

    // Trackers
    PenState pen;
    MouseState mouse;
    KeyboardState keyboard;
    std::array<TouchSlot, 10> activeFingers{};
    PenPalette palette;

    TouchGestureRecognizer gestureRecognizer;

    // Canvas screen origin offset (e.g. sidebar width, ribbon height)
    float canvasOriginX = 0.0f;
    float canvasOriginY = 0.0f;

    // Timestamps
    uint64_t lastPenTimestampMs   = 0;
    uint64_t lastTouchTimestampMs = 0;
    uint64_t lastMouseTimestampMs = 0;
    double latestEventTimeSec     = 0.0;
    uint64_t appStartTimeNs       = 0;

    // Transitions
    bool wasMouseDown = false;
    bool wasTouchDown = false;

    void InitTiming() noexcept {
        appStartTimeNs = SDL_GetTicksNS();
    }

    [[nodiscard]] double EventTimestampToSec(uint64_t eventTimestampNs) const noexcept {
        if (eventTimestampNs < appStartTimeNs) return 0.0;
        return static_cast<double>(eventTimestampNs - appStartTimeNs) * 1e-9;
    }

    // UI Interaction Locks
    bool uiCapturedStylus = false;
    bool uiCapturedTouch = false;
    bool uiCapturedMouse = false;

    void ProcessInputState(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput);

private:
    void UpdateHardwareState(uint64_t nowMs);
    void DispatchStylus(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput);
    void DispatchMouse(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput);
    void DispatchTouch(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput);
    size_t GetActiveFingerCount() const noexcept;
};