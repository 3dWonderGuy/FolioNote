#include "input/input_state_machine.hpp"
#include "core/engine/canvas_engine.hpp"
#include "core/document/document_session.hpp"
#include "utils/logger.hpp"

size_t InputStateMachine::GetActiveFingerCount() const noexcept {
    size_t count = 0;
    for (const auto& slot : activeFingers) {
        if (slot.fingerID != -1) ++count;
    }
    return count;
}

void InputStateMachine::UpdateHardwareState(uint64_t nowMs) {
    size_t fingerCount = GetActiveFingerCount();

    DeviceType oldDevice = ActiveDevice;

    // =========================================================================
    // 1. DEVICE PRIORITY ARBITRATION
    // =========================================================================
    // We need to determine which device the user is actively trying to use, 
    // because processing multiple inputs simultaneously (like resting a palm 
    // while drawing with a pen) causes erratic behavior.
    //
    // Rule: Stylus (Pen) has the highest priority. If the pen is down, or has 
    // generated an event in the last 80ms, we ignore touch and mouse.
    if (pen.isDown || (nowMs - lastPenTimestampMs < 80)) {
        ActiveDevice = DeviceType::Stylus;
    } 
    // Rule: Touch is the second highest priority. If the pen is out of range,
    // but there are active fingers on the screen, or a touch event occurred 
    // in the last 80ms, we assume the user is using touch (likely for panning/zooming).
    else if (fingerCount > 0 || (nowMs - lastTouchTimestampMs < 80)) {
        ActiveDevice = DeviceType::Touch;
    } 
    // Rule: Fallback to Mouse. If neither pen nor touch is active, mouse wins.
    else {
        ActiveDevice = DeviceType::Mouse;
    }

    if (oldDevice != ActiveDevice) {
        std::string deviceName = (ActiveDevice == DeviceType::Stylus) ? "Stylus" :
                                 (ActiveDevice == DeviceType::Touch) ? "Touch" : "Mouse";
        LOG_INFO(InputStateMachine, "Active input device changed to: " + deviceName);
    }

    // =========================================================================
    // 2. STYLUS STATE & TOOL INTENT TRACKING
    // =========================================================================
    // We map the low-level hardware state (buttons, hover status) to high-level
    // semantic actions (Inking, Eraser, Selecting) so the Dispatcher doesn't 
    // need to worry about hardware buttons directly.
    InteractionState oldAction = currentAction;
    StylusState oldStylus = currentStylusState;

    if (ActiveDevice == DeviceType::Stylus) {
        // Determine physical contact state
        if (pen.isDown) {
            currentStylusState = StylusState::Engaged; // Pen tip is pressed against the screen
        } else if (pen.isHovering) {
            currentStylusState = StylusState::Hovering; // Pen is floating near the screen
        } else {
            currentStylusState = StylusState::OutOfRange; // Pen is gone
        }

        // Determine tool intent based on barrel buttons or eraser tip
        // By default we assume the user wants to draw (Inking).
        if (pen.barrel1 || pen.eraserTip) {
            stylusButtons = StylusButtonState::BarrelPressed;
            currentAction = InteractionState::Eraser; 
        } else if (pen.barrel2) {
            stylusButtons = StylusButtonState::Barrel2Pressed;
            currentAction = InteractionState::Selecting; // E.g., Lasso selection tool
        } else {
            stylusButtons = StylusButtonState::None;
            currentAction = InteractionState::Inking; // Regular drawing
        }
    } else {
        // If the stylus is not the active device, reset its state to prevent sticky behavior
        currentStylusState = StylusState::OutOfRange;
        stylusButtons = StylusButtonState::None;
    }

    if (oldStylus != currentStylusState) {
        std::string stylusName = (currentStylusState == StylusState::OutOfRange) ? "OutOfRange" :
                                 (currentStylusState == StylusState::Hovering) ? "Hovering" : "Engaged";
        LOG_INFO(InputStateMachine, "Stylus state changed to: " + stylusName);
    }
    
    if (oldAction != currentAction) {
        std::string actionName = (currentAction == InteractionState::Inking) ? "Inking" :
                                 (currentAction == InteractionState::Eraser) ? "Eraser" :
                                 (currentAction == InteractionState::Selecting) ? "Selecting" :
                                 (currentAction == InteractionState::Panning) ? "Panning" :
                                 (currentAction == InteractionState::Transforming) ? "Transforming" : "Idle";
        LOG_INFO(InputStateMachine, "Interaction state changed to: " + actionName);
    }
}

void InputStateMachine::ProcessInputState(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput) {
    uint64_t now = SDL_GetTicks();
    UpdateHardwareState(now);

    switch (ActiveDevice) {
        case DeviceType::Stylus: DispatchStylus(canvas, session, imguiWantsInput); break;
        case DeviceType::Touch:  DispatchTouch(canvas, session, imguiWantsInput);  break;
        case DeviceType::Mouse:  DispatchMouse(canvas, session, imguiWantsInput);  break;
        default: break;
    }

    oldStylusState = currentStylusState;
    wasMouseDown   = mouse.leftButton;
}

// -------------------------------------------------------------
// STYLUS (Mapped relative to Canvas Top-Left Origin)
// -------------------------------------------------------------
void InputStateMachine::DispatchStylus(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput) {
    const bool justDown = (currentStylusState == StylusState::Engaged && oldStylusState != StylusState::Engaged);
    const bool isMoving = (currentStylusState == StylusState::Engaged && oldStylusState == StylusState::Engaged);
    const bool justUp   = (currentStylusState != StylusState::Engaged && oldStylusState == StylusState::Engaged);

    if (justDown) {
        uiCapturedStylus = imguiWantsInput;
    }

    if (uiCapturedStylus) {
        if (justUp) uiCapturedStylus = false;
        return;
    }

    const float canvasLocalX = pen.x - canvasOriginX;
    const float canvasLocalY = pen.y - canvasOriginY;

    switch (currentAction) {
        case InteractionState::Inking: {
            if (justDown) {
                canvas.OnPointerDown(canvasLocalX, canvasLocalY, pen.pressure, latestEventTimeSec, palette.GetActivePen(), pen.tiltX, pen.tiltY);
            } else if (isMoving) {
                canvas.OnPointerMove(canvasLocalX, canvasLocalY, pen.pressure, latestEventTimeSec, pen.tiltX, pen.tiltY);
            } else if (justUp) {
                // Canvas talks directly to DocumentSession — State Machine is not involved in the data handoff
                canvas.OnPointerUp(session, palette.GetActivePen());
            }
            break;
        }
        case InteractionState::Selecting: {
            if (justDown)      canvas.OnLassoDown(canvasLocalX, canvasLocalY);
            else if (isMoving) canvas.OnLassoMove(canvasLocalX, canvasLocalY);
            else if (justUp)   canvas.OnLassoUp();
            break;
        }
        case InteractionState::Eraser: {
            // Future eraser hook
            break;
        }
        default: break;
    }
}

// -------------------------------------------------------------
// MOUSE (Mapped relative to Canvas Top-Left Origin)
// -------------------------------------------------------------
void InputStateMachine::DispatchMouse(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput) {
    InteractionState oldAction = currentAction;

    if (mouse.middleButton || keyboard.space) {
        currentAction = InteractionState::Panning;
    } else {
        currentAction = InteractionState::Inking;
    }

    if (oldAction != currentAction) {
        std::string actionName = (currentAction == InteractionState::Inking) ? "Inking" :
                                 (currentAction == InteractionState::Panning) ? "Panning" : "Idle";
        LOG_INFO(InputStateMachine, "Mouse interaction state changed to: " + actionName);
    }

    const bool justDown = mouse.leftButton && !wasMouseDown;
    const bool isMoving = mouse.leftButton && wasMouseDown;
    const bool justUp   = !mouse.leftButton && wasMouseDown;

    if (justDown) {
        uiCapturedMouse = imguiWantsInput;
    }

    if (uiCapturedMouse) {
        if (justUp) uiCapturedMouse = false;
        return;
    }

    const float canvasLocalX = mouse.x - canvasOriginX;
    const float canvasLocalY = mouse.y - canvasOriginY;

    if (currentAction == InteractionState::Inking) {
        if (justDown) {
            canvas.OnPointerDown(canvasLocalX, canvasLocalY, 1.0f, latestEventTimeSec, palette.GetActivePen(), 0.0f, 0.0f);
        } else if (isMoving) {
            canvas.OnPointerMove(canvasLocalX, canvasLocalY, 1.0f, latestEventTimeSec, 0.0f, 0.0f);
        } else if (justUp) {
            // Canvas talks directly to DocumentSession — State Machine is not involved in the data handoff
            canvas.OnPointerUp(session, palette.GetActivePen());
        }
    } 
    else if (currentAction == InteractionState::Panning && isMoving) {
        canvas.Pan(mouse.dx, mouse.dy);
    }

    if (mouse.wheelY != 0.0f) {
        canvas.ZoomAt(canvasLocalX, canvasLocalY, mouse.wheelY > 0 ? 1.15 : 0.85);
        mouse.wheelX = 0.0f;
        mouse.wheelY = 0.0f;
    }
}

// -------------------------------------------------------------
// TOUCH GESTURES (Single finger scroll / 2-finger pinch-pan)
// -------------------------------------------------------------
void InputStateMachine::DispatchTouch(CanvasEngine& canvas, DocumentSession& /*session*/, bool imguiWantsInput) {
    size_t fingerCount = GetActiveFingerCount();
    uint64_t now = SDL_GetTicks();
    
    bool touchDown = fingerCount > 0;
    bool justDown = touchDown && !wasTouchDown;
    bool justUp = !touchDown && wasTouchDown;

    if (justDown) {
        uiCapturedTouch = imguiWantsInput;
    }

    if (uiCapturedTouch) {
        if (justUp) uiCapturedTouch = false;
        wasTouchDown = touchDown;
        return;
    }

    wasTouchDown = touchDown;

    TouchGestureEvent gesture = gestureRecognizer.Evaluate(activeFingers, fingerCount, now, false);

    switch (gesture.type) {
        case TouchGestureType::SingleFingerScroll: {
            canvas.Pan(gesture.deltaX, gesture.deltaY);
            break;
        }
        case TouchGestureType::TwoFingerPinchPan: {
            canvas.Pan(gesture.deltaX, gesture.deltaY);
            if (std::abs(gesture.zoomDelta - 1.0f) > 0.001f) {
                float localFocusX = gesture.focusX - canvasOriginX;
                float localFocusY = gesture.focusY - canvasOriginY;
                canvas.ZoomAt(localFocusX, localFocusY, gesture.zoomDelta);
            }
            break;
        }
        case TouchGestureType::Tap: {
            // Hit test selection / focus cursor
            break;
        }
        case TouchGestureType::PressAndHold: {
            // Radial or context menu trigger
            break;
        }
        default: break;
    }
}