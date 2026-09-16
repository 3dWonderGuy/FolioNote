/**
 * @file input_state_machine_touch.cpp
 * @brief Implementation of Capacitive Multi-Touch dispatching: Touch Paint (single-finger ink),
 *        two-finger pinch-pan, tap selection, long-press holds, and multi-touch shortcuts (Undo/Redo).
 *
 * MULTI-TOUCH ARCHITECTURE:
 * -------------------------
 * Touch interactions on canvas follow a clear modal separation:
 *   1. Touch Paint Disabled (Default):
 *      - 1 Finger Drag: Panning / scrolling the canvas.
 *      - 2 Fingers: Pinch-to-zoom and simultaneous two-finger panning around the focal centroid.
 *      - 2 Finger Tap: Shortcut for Undo.
 *      - 3 Finger Tap: Shortcut for Redo.
 *      - 1 Finger Tap: Direct object hit-testing / selection or popup dismissal.
 *      - 1 Finger Hold: Triggers context / radial menu.
 *   2. Touch Paint Enabled (`touchTool.savedTool == Inking`):
 *      - 1 Finger Drag: Inks strokes on canvas with active pen preset.
 *      - 2+ Fingers: Always navigates (pinch-pan) regardless of mode.
 */

#include "input/stateMachine/input_state_machine.hpp"
#include "core/engine/canvas_engine.hpp"
#include "core/document/document_session.hpp"
#include "utils/logger.hpp"

void InputStateMachine::DispatchTouch(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput) {
    size_t fingerCount = GetActiveFingerCount();
    uint64_t now = SDL_GetTicks();

    bool touchDown = fingerCount > 0;
    bool justDown  = touchDown && !wasTouchDown;
    bool justUp    = !touchDown && wasTouchDown;

    // -------------------------------------------------------------------------
    // 1. UI CAPTURE CHECK
    // -------------------------------------------------------------------------
    if (justDown) {
        uiCapturedTouch = imguiWantsInput;
    }
    if (uiCapturedTouch) {
        if (justUp) uiCapturedTouch = false;
        wasTouchDown = touchDown;
        return;
    }

    wasTouchDown = touchDown;

    // -------------------------------------------------------------------------
    // 2. TOUCH INKING MODE (Touch Paint: 1 Finger Inks, 2+ Fingers Navigate)
    // -------------------------------------------------------------------------
    if (touchTool.savedTool == InteractionState::Inking && fingerCount == 1) {
        const TouchSlot* slot = nullptr;
        for (const auto& s : activeFingers) {
            if (s.fingerID != -1) { slot = &s; break; }
        }
        if (slot) {
            const float canvasLocalX = slot->point.x - canvasOriginX;
            const float canvasLocalY = slot->point.y - canvasOriginY;
            const float pressure = (slot->point.pressure > 0.0f) ? slot->point.pressure : 1.0f;

            if (justDown) {
                canvas.OnPointerDown(canvasLocalX, canvasLocalY, pressure, latestEventTimeSec, palette.GetActivePen(), 0.0f, 0.0f);
            } else if (touchDown && wasTouchDown) {
                canvas.OnPointerMove(canvasLocalX, canvasLocalY, pressure, latestEventTimeSec, 0.0f, 0.0f);
            } else if (justUp) {
                canvas.OnPointerUp(session, palette.GetActivePen());
            }
        }
        return; // Suppress gesture recognizer while single-finger inking
    }

    // -------------------------------------------------------------------------
    // 3. GESTURE RECOGNITION (Navigation & Special Action Shortcuts)
    // -------------------------------------------------------------------------
    TouchGestureEvent gesture = gestureRecognizer.Evaluate(activeFingers, fingerCount, now, false);

    const float localFocusX = gesture.focusX - canvasOriginX;
    const float localFocusY = gesture.focusY - canvasOriginY;

    switch (gesture.type) {
        // Single finger scroll / pan
        case TouchGestureType::SingleFingerScroll: {
            canvas.Pan(gesture.deltaX, gesture.deltaY);
            break;
        }

        // Two-finger pinch-to-zoom and simultaneous panning
        case TouchGestureType::TwoFingerPinchPan: {
            canvas.Pan(gesture.deltaX, gesture.deltaY);
            if (std::abs(gesture.zoomDelta - 1.0f) > config.zoomDeltaEpsilon) {
                canvas.ZoomAt(localFocusX, localFocusY, gesture.zoomDelta);
            }
            break;
        }

        // Multi-touch digital ink shortcut: 2-Finger Tap = UNDO
        case TouchGestureType::TwoFingerTap: {
            LOG_INFO(InputStateMachine, "2-Finger Tap gesture recognized -> Triggering UNDO");
            specialActions.TriggerAction(FolioInput::SpecialActionType::Undo, localFocusX, localFocusY, canvas, session);
            break;
        }

        // Multi-touch digital ink shortcut: 3-Finger Tap = REDO
        case TouchGestureType::ThreeFingerTap: {
            LOG_INFO(InputStateMachine, "3-Finger Tap gesture recognized -> Triggering REDO");
            specialActions.TriggerAction(FolioInput::SpecialActionType::Redo, localFocusX, localFocusY, canvas, session);
            break;
        }

        // Single finger tap: select object under contact or clear selection
        case TouchGestureType::Tap: {
            LOG_INFO(InputStateMachine, "Touch Tap gesture recognized at (" + std::to_string(localFocusX) + ", " + std::to_string(localFocusY) + ")");
            specialActions.TriggerAction(FolioInput::SpecialActionType::SelectAtPoint, localFocusX, localFocusY, canvas, session);
            break;
        }

        // Stationary long-press hold: open context menu or radial menu
        case TouchGestureType::PressAndHold: {
            LOG_INFO(InputStateMachine, "Touch Press-and-Hold recognized -> Triggering Context Menu");
            specialActions.TriggerAction(FolioInput::SpecialActionType::OpenContextMenu, localFocusX, localFocusY, canvas, session);
            break;
        }

        default: break;
    }
}
