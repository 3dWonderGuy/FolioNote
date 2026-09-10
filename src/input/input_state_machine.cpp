#include "input/input_state_machine.hpp"
#include "core/engine/canvas_engine.hpp"
#include "core/document/document_session.hpp"
#include "utils/logger.hpp"

// gets active fingers by checking for finger id != -1
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
    // Determines which physical device is driving input this frame. We cannot
    // process multiple devices simultaneously (e.g. palm resting while drawing)
    //
    // Priority order: Stylus > Touch > Mouse
    if (pen.isDown || pen.inProximity || pen.isHovering || (nowMs - lastPenTimestampMs < 600)) {
        // Stylus wins if in proximity, tip is down, hovering, or within 600ms grace window.
        ActiveDevice = DeviceType::Stylus;
    } else if (fingerCount > 0 || (nowMs - lastTouchTimestampMs < 250)) {
        // Touch wins over mouse when fingers are on screen or recently lifted.
        ActiveDevice = DeviceType::Touch;
    } else {
        // Mouse is the lowest-priority fallback device.
        ActiveDevice = DeviceType::Mouse;
    }

    // =========================================================================
    // 2. DEVICE SWITCH: RESTORE PER-DEVICE SAVED TOOL
    // =========================================================================
    // When the active device changes (e.g. user lifts pen and touches screen),
    // restore the incoming device's last saved tool. This gives each device its
    // own independent tool memory — switching back to the pen will always land
    // on whatever tool that pen was last using, not whatever touch left behind.
    //
    // Note: we only do this on a genuine device *change*, not on every frame,
    // so mid-session barrel-button overrides are not disturbed while the pen
    // is hovering between strokes.
    if (oldDevice != ActiveDevice) {
        std::string deviceName = (ActiveDevice == DeviceType::Stylus) ? "Stylus" :
                                 (ActiveDevice == DeviceType::Touch)  ? "Touch"  : "Mouse";
        LOG_INFO(InputStateMachine, "Active input device changed to: " + deviceName);

        // Restore this device's saved tool as the baseline action for this session.
        currentAction = GetActiveDeviceTool();
    }

    // =========================================================================
    // 3. STYLUS CONTACT STATE & TRANSIENT TOOL OVERRIDES
    // =========================================================================
    // Map low-level hardware state (tip contact, barrel buttons, eraser tip) to
    // high-level semantic actions. Barrel overrides are TRANSIENT — they change
    // currentAction only while held and do not modify stylusTool.savedTool.
    InteractionState oldAction = currentAction;
    StylusState oldStylus = currentStylusState;

    if (ActiveDevice == DeviceType::Stylus) {
        // Map physical tip contact to stylus state
        if (pen.isDown) {
            currentStylusState = StylusState::Engaged;
        } else if (pen.isHovering || pen.inProximity || (nowMs - lastPenTimestampMs < 600)) {
            currentStylusState = StylusState::Hovering;
        } else {
            currentStylusState = StylusState::OutOfRange;
        }

        // Barrel button 1 / barrel button 3 / eraser tip → transient Eraser override
        // Barrel button 2                                → transient Lasso/Select override
        // No button held                                 → restore the stylus's saved tool
        if (pen.barrel1 || pen.barrel3 || pen.eraserTip) {
            stylusButtons = StylusButtonState::BarrelPressed;
            currentAction = InteractionState::Eraser;
        } else if (pen.barrel2) {
            stylusButtons = StylusButtonState::Barrel2Pressed;
            currentAction = InteractionState::Selecting;
        } else {
            stylusButtons = StylusButtonState::None;
            currentAction = stylusTool.savedTool; // no override — use saved tool
        }
    } else {
        // Stylus is not active — clear its state to prevent sticky artifacts.
        currentStylusState = StylusState::OutOfRange;
        stylusButtons      = StylusButtonState::None;
    }

    if (oldStylus != currentStylusState) {
        std::string stylusName = (currentStylusState == StylusState::OutOfRange) ? "OutOfRange" :
                                 (currentStylusState == StylusState::Hovering)   ? "Hovering"   : "Engaged";
        LOG_INFO(InputStateMachine, "Stylus state changed to: " + stylusName);
    }

    if (oldAction != currentAction) {
        std::string actionName = (currentAction == InteractionState::Inking)      ? "Inking"      :
                                 (currentAction == InteractionState::Eraser)      ? "Eraser"      :
                                 (currentAction == InteractionState::Selecting)   ? "Selecting"   :
                                 (currentAction == InteractionState::Panning)     ? "Panning"     :
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
    wasMiddleDown  = mouse.middleButton;
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
            if (justDown) {
                if (canvas.selectionGizmo.OnPointerDown(canvasLocalX, canvasLocalY, canvas.transform)) {
                    canvas.isDirty = true;
                } else {
                    canvas.selectionGizmo.ClearSelection();
                    canvas.OnLassoDown(canvasLocalX, canvasLocalY);
                }
            }
            else if (isMoving) {
                if (canvas.selectionGizmo.isDragging) {
                    if (canvas.selectionGizmo.OnPointerMove(canvasLocalX, canvasLocalY, canvas.transform)) {
                        canvas.needsFullRebake = true;
                        canvas.isDirty = true;
                    }
                } else {
                    canvas.OnLassoMove(canvasLocalX, canvasLocalY);
                }
            }
            else if (justUp) {
                if (canvas.selectionGizmo.isDragging) {
                    canvas.selectionGizmo.OnPointerUp();
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                } else {
                    canvas.OnLassoUp(&session);
                }
            }
            break;
        }
        case InteractionState::Eraser: {
            if (justDown || isMoving) {
                canvas.EraseAt(canvasLocalX, canvasLocalY, eraserRadiusMm, session, isStrokeEraser);
            }
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

    // Space bar or middle mouse button always triggers transient panning,
    // regardless of the mouse's saved tool.
    // Otherwise, use the mouse's per-device saved tool.
    //
    // -------------------------------------------------------------------------
    // TESTING NOTE: mouseTool defaults to Idle (navigation/selection mode).
    // Inking via mouse is currently used for development testing only.
    // In production the mouse should primarily be a navigation and selection
    // device. Do NOT change this default without updating the UX design.
    // -------------------------------------------------------------------------
    if (mouse.middleButton || keyboard.space) {
        currentAction = InteractionState::Panning;
    } else {
        currentAction = mouseTool.savedTool;
    }

    if (oldAction != currentAction) {
        std::string actionName = (currentAction == InteractionState::Inking) ? "Inking" :
                                 (currentAction == InteractionState::Eraser) ? "Eraser" :
                                 (currentAction == InteractionState::Selecting) ? "Selecting" :
                                 (currentAction == InteractionState::Panning) ? "Panning" : "Idle";
        LOG_INFO(InputStateMachine, "Mouse interaction state changed to: " + actionName);
    }

    // =========================================================================
    // MIDDLE MOUSE / SPACE — TRANSIENT PAN OVERRIDE
    // =========================================================================
    // Middle mouse and Space bar override the mouse's saved tool with a
    // temporary Panning mode for as long as they're held. On release the
    // saved tool is restored automatically because DispatchMouse falls back
    // to mouseTool.savedTool — which was never changed by this transient.
    //
    // Pressing middle mouse ALSO directly drives canvas.Pan() independent of
    // the left button, so dragging with middle click alone pans the canvas.
    // =========================================================================
    const bool middleJustDown = mouse.middleButton && !wasMiddleDown;
    const bool middleIsMoving = mouse.middleButton && wasMiddleDown;
    const bool middleJustUp   = !mouse.middleButton && wasMiddleDown;

    const bool justDown = mouse.leftButton && !wasMouseDown;
    const bool isMoving = mouse.leftButton && wasMouseDown;
    const bool justUp   = !mouse.leftButton && wasMouseDown;

    // Middle-drag directly pans — no left button required.
    // This runs BEFORE the UI capture check so the canvas pan fires even
    // if ImGui was capturing the last left click.
    if (currentAction == InteractionState::Panning && middleIsMoving) {
        canvas.Pan(mouse.dx, mouse.dy);
    }

    // Space-bar pan also fires on any mouse movement while held.
    if (keyboard.space && (isMoving || middleIsMoving)) {
        canvas.Pan(mouse.dx, mouse.dy);
    }

    // On middle-up, zero out the relative delta to prevent a ghost pan
    // on the frame the button is released.
    if (middleJustUp) {
        mouse.dx = 0.0f;
        mouse.dy = 0.0f;
    }

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
    else if (currentAction == InteractionState::Eraser) {
        if (justDown || isMoving) {
            canvas.EraseAt(canvasLocalX, canvasLocalY, eraserRadiusMm, session, isStrokeEraser);
        }
    }
    else if (currentAction == InteractionState::Selecting) {
        if (justDown) {
            if (canvas.selectionGizmo.OnPointerDown(canvasLocalX, canvasLocalY, canvas.transform)) {
                canvas.isDirty = true;
            } else {
                canvas.selectionGizmo.ClearSelection();
                canvas.OnLassoDown(canvasLocalX, canvasLocalY);
            }
        }
        else if (isMoving) {
            if (canvas.selectionGizmo.isDragging) {
                if (canvas.selectionGizmo.OnPointerMove(canvasLocalX, canvasLocalY, canvas.transform)) {
                    canvas.needsFullRebake = true;
                    canvas.isDirty = true;
                }
            } else {
                canvas.OnLassoMove(canvasLocalX, canvasLocalY);
            }
        }
        else if (justUp) {
            if (canvas.selectionGizmo.isDragging) {
                canvas.selectionGizmo.OnPointerUp();
                canvas.needsFullRebake = true;
                canvas.isDirty = true;
            } else {
                canvas.OnLassoUp(&session);
            }
        }

        // Set cursor according to hovered gizmo handle when idle
        if (!canvas.selectionGizmo.isDragging && canvas.selectionGizmo.HasSelection()) {
            auto hit = canvas.selectionGizmo.HitTest(canvasLocalX, canvasLocalY, canvas.transform);
            if (hit.hit) {
                switch (hit.role) {
                    case HandleRole::TopLeft:
                    case HandleRole::BottomRight:
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                        break;
                    case HandleRole::TopRight:
                    case HandleRole::BottomLeft:
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
                        break;
                    case HandleRole::TopCenter:
                    case HandleRole::BottomCenter:
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                        break;
                    case HandleRole::LeftCenter:
                    case HandleRole::RightCenter:
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                        break;
                    case HandleRole::Rotation:
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        break;
                    case HandleRole::Body:
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                        break;
                    default:
                        break;
                }
            }
        } else if (canvas.selectionGizmo.isDragging) {
            if (canvas.selectionGizmo.activeRole == HandleRole::Rotation) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            } else if (canvas.selectionGizmo.activeRole == HandleRole::Body) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            }
        }
    }
    else if (currentAction == InteractionState::Panning && isMoving) {
        // Left-button drag while in pan mode (e.g. space held + left drag).
        // Middle-button drag is handled above before the UI capture check.
        canvas.Pan(mouse.dx, mouse.dy);
    }

    if (mouse.wheelY != 0.0f) {
        if (isCanvasHovered && !imguiWantsInput) {
            canvas.ZoomAt(canvasLocalX, canvasLocalY, mouse.wheelY > 0 ? 1.15 : 0.85);
        }
        mouse.wheelX = 0.0f;
        mouse.wheelY = 0.0f;
    }
}

// -------------------------------------------------------------
// TOUCH GESTURES (Single finger scroll / 2-finger pinch-pan)
// -------------------------------------------------------------
void InputStateMachine::DispatchTouch(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput) {
    size_t fingerCount = GetActiveFingerCount();
    uint64_t now = SDL_GetTicks();

    bool touchDown = fingerCount > 0;
    bool justDown  = touchDown && !wasTouchDown;
    bool justUp    = !touchDown && wasTouchDown;

    // Suppress all canvas dispatch when ImGui captured the touch gesture start
    if (justDown) {
        uiCapturedTouch = imguiWantsInput;
    }
    if (uiCapturedTouch) {
        if (justUp) uiCapturedTouch = false;
        wasTouchDown = touchDown;
        return;
    }

    wasTouchDown = touchDown;

    // =========================================================================
    // TOUCH INKING MODE (1 finger draws, 2+ fingers always navigate)
    // =========================================================================
    // When the user has enabled "Touch Paint" (touchTool.savedTool == Inking),
    // a single finger behaves like a stylus — it draws with the active pen.
    // Two or more fingers always fall through to the gesture recognizer for
    // pan/zoom, regardless of the current touch tool mode.
    //
    // NOTE: Multi-finger simultaneous inking (drawing with all fingers at once)
    // is a possible future enhancement but is not implemented here. It would
    // require significant changes to how strokes are tracked per-finger.
    // For now we default to: 1 finger = ink, 2+ fingers = navigate.
    if (touchTool.savedTool == InteractionState::Inking && fingerCount == 1) {
        // Find the single active finger
        const TouchSlot* slot = nullptr;
        for (const auto& s : activeFingers) {
            if (s.fingerID != -1) { slot = &s; break; }
        }
        if (slot) {
            const float canvasLocalX = slot->point.x - canvasOriginX;
            const float canvasLocalY = slot->point.y - canvasOriginY;
            // Use pressure if the touchscreen reports it, otherwise assume full pressure
            const float pressure = (slot->point.pressure > 0.0f) ? slot->point.pressure : 1.0f;

            if (justDown) {
                canvas.OnPointerDown(canvasLocalX, canvasLocalY, pressure, latestEventTimeSec, palette.GetActivePen(), 0.0f, 0.0f);
            } else if (touchDown && wasTouchDown) {
                canvas.OnPointerMove(canvasLocalX, canvasLocalY, pressure, latestEventTimeSec, 0.0f, 0.0f);
            } else if (justUp) {
                canvas.OnPointerUp(session, palette.GetActivePen());
            }
        }
        return; // Do not pass single-finger inking through to gesture recognizer
    }

    // =========================================================================
    // GESTURE RECOGNIZER (Panning mode, or 2+ finger navigation in Inking mode)
    // =========================================================================
    // Feed raw active finger positions into the gesture recognizer.
    TouchGestureEvent gesture = gestureRecognizer.Evaluate(activeFingers, fingerCount, now, false);

    switch (gesture.type) {
        case TouchGestureType::SingleFingerScroll: {
            // Single finger drag — pan/scroll the canvas
            canvas.Pan(gesture.deltaX, gesture.deltaY);
            break;
        }
        case TouchGestureType::TwoFingerPinchPan: {
            // Two-finger gesture — pan and zoom simultaneously around the focal point
            canvas.Pan(gesture.deltaX, gesture.deltaY);
            if (std::abs(gesture.zoomDelta - 1.0f) > 0.001f) {
                float localFocusX = gesture.focusX - canvasOriginX;
                float localFocusY = gesture.focusY - canvasOriginY;
                canvas.ZoomAt(localFocusX, localFocusY, gesture.zoomDelta);
            }
            break;
        }
        case TouchGestureType::Tap: {
            // Quick tap — reserved for future use (e.g. select item, dismiss popup)
            break;
        }
        case TouchGestureType::PressAndHold: {
            // Stationary hold — reserved for future use (e.g. context menu, radial menu)
            break;
        }
        default: break;
    }
}