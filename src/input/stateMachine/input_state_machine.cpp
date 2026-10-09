/**
 * @file input_state_machine.cpp
 * @brief Core implementation of InputStateMachine: hardware state arbitration,
 *        device priority resolution, tool persistence, and dispatcher routing.
 *
 * ARBITRATION ARCHITECTURE:
 * -------------------------
 * FolioNote implements strict single-driver device arbitration. When a user writes on a
 * touchscreen, multiple hardware sources report input simultaneously:
 *   - Stylus active digitizer (pen tip contact, pressure, tilt)
 *   - Capacitive multi-touch sensor (palm and fingers resting on glass)
 *   - Physical mouse / trackpad
 *
 * To prevent palm touches from creating stray strokes or interrupting drawing, the state
 * machine enforces a priority hierarchy:
 *   Stylus (Priority 1) > Touch (Priority 2) > Mouse (Priority 3)
 *
 * Temporal Grace Windows:
 *   - Pen Grace: `config.penGracePeriodMs` (default 600ms).
 *   - Touch Grace: `config.touchGracePeriodMs` (default 250ms).
 */

#include "input/stateMachine/input_state_machine.hpp"
#include "core/canvas_engine/canvas_engine.hpp"
#include "core/document/document_session.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <cmath>

/**
 * @brief Counts the number of active capacitive finger contacts.
 *
 * Checks all slots in the fixed-capacity `activeFingers` array for valid finger IDs (fingerID != -1).
 *
 * @return size_t Count of active finger contacts (0 to 10).
 */
size_t InputStateMachine::GetActiveFingerCount() const noexcept {
    size_t count = 0;
    for (const auto& slot : activeFingers) {
        if (slot.fingerID != -1) ++count;
    }
    return count;
}

/**
 * @brief Resolves active input device and handles tool memory restoration & transient overrides.
 *
 * ARBITRATION MATHEMATICAL FORMULATION:
 * -------------------------------------
 * Let $t$ be the current timestamp `nowMs`.
 * Let $t_{pen}$ be `lastPenTimestampMs` and $t_{touch}$ be `lastTouchTimestampMs`.
 *
 * Condition for Stylus Priority:
 *   $C_{stylus} = \text{pen.isDown} \lor \text{pen.inProximity} \lor \text{pen.isHovering} \lor (t - t_{pen} < T_{pen\_grace})$
 *
 * Condition for Touch Priority (if Stylus inactive):
 *   $C_{touch} = (\text{fingerCount} > 0) \lor (t - t_{touch} < T_{touch\_grace})$
 *
 * Otherwise:
 *   Fallback to Mouse.
 *
 * @param nowMs System tick in milliseconds from SDL_GetTicks().
 */
void InputStateMachine::UpdateHardwareState(uint64_t nowMs) {
    size_t fingerCount = GetActiveFingerCount();
    DeviceType oldDevice = ActiveDevice;

    // -------------------------------------------------------------------------
    // 1. DEVICE PRIORITY ARBITRATION
    // -------------------------------------------------------------------------
    if (pen.isDown || pen.inProximity || pen.isHovering || (nowMs - lastPenTimestampMs < config.penGracePeriodMs)) {
        ActiveDevice = DeviceType::Stylus;
    } else if (fingerCount > 0 || (nowMs - lastTouchTimestampMs < config.touchGracePeriodMs)) {
        ActiveDevice = DeviceType::Touch;
    } else {
        ActiveDevice = DeviceType::Mouse;
    }

    // -------------------------------------------------------------------------
    // 2. DEVICE SWITCH: RESTORE PER-DEVICE SAVED TOOL
    // -------------------------------------------------------------------------
    if (oldDevice != ActiveDevice) {
        std::string deviceName = (ActiveDevice == DeviceType::Stylus) ? "Stylus" :
                                 (ActiveDevice == DeviceType::Touch)  ? "Touch"  : "Mouse";
        LOG_INFO(InputStateMachine, "Active input device changed to: " + deviceName);

        // Restore saved tool for the incoming device
        currentAction = GetActiveDeviceTool();
    }

    // -------------------------------------------------------------------------
    // 3. STYLUS CONTACT STATE & TRANSIENT TOOL OVERRIDES
    // -------------------------------------------------------------------------
    InteractionState oldAction = currentAction;
    StylusState oldStylus = currentStylusState;

    if (ActiveDevice == DeviceType::Stylus) {
        if (pen.isDown) {
            currentStylusState = StylusState::Engaged;
        } else if (pen.isHovering || pen.inProximity || (nowMs - lastPenTimestampMs < config.penGracePeriodMs)) {
            currentStylusState = StylusState::Hovering;
        } else {
            currentStylusState = StylusState::OutOfRange;
        }

        // Barrel button 1 / barrel button 3 / physical eraser tail -> transient Eraser override
        // Barrel button 2                                           -> transient Lasso/Select override
        // No button held                                            -> restore saved tool
        if (pen.barrel1 || pen.barrel3 || pen.eraserTip) {
            stylusButtons = StylusButtonState::BarrelPressed;
            currentAction = InteractionState::Eraser;
        } else if (pen.barrel2) {
            stylusButtons = StylusButtonState::Barrel2Pressed;
            currentAction = InteractionState::Selecting;
        } else {
            stylusButtons = StylusButtonState::None;
            currentAction = stylusTool.savedTool;
        }
    } else {
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
                                 (currentAction == InteractionState::DrawingShape) ? "DrawingShape" :
                                 (currentAction == InteractionState::Text)        ? "Text"        :
                                 (currentAction == InteractionState::Transforming) ? "Transforming" : "Idle";
        LOG_INFO(InputStateMachine, "Interaction state changed to: " + actionName);
    }
}

/**
 * @brief High-level entry point called each frame to evaluate arbitration and route to dispatchers.
 *
 * @param canvas          Reference to CanvasEngine for drawing, zooming, and viewport manipulation.
 * @param session         Reference to DocumentSession for stroke persistence and undo/redo.
 * @param imguiWantsInput Boolean flag indicating whether ImGui UI chrome currently holds focus.
 */
void InputStateMachine::ProcessInputState(CanvasEngine& canvas, DocumentSession& session, bool imguiWantsInput) {
    uint64_t now = SDL_GetTicks();
    UpdateHardwareState(now);

    auto activePg = session.GetActivePage();
    isPdfModeActive = (activePg && activePg->isDedicatedPdf);
    // Markdown editor is disabled for prerelease; dedicated PDF viewer manages its own pointer events.
    bool isDedicatedDocActive = isPdfModeActive;

    // If active page is a dedicated standalone PDF document, stroke projection and interaction
    // are handled directly by its view to prevent writing to the background canvas.
    if (!isDedicatedDocActive) {
        switch (ActiveDevice) {
            case DeviceType::Stylus: DispatchStylus(canvas, session, imguiWantsInput); break;
            case DeviceType::Touch:  DispatchTouch(canvas, session, imguiWantsInput);  break;
            case DeviceType::Mouse:  DispatchMouse(canvas, session, imguiWantsInput);  break;
            default: break;
        }
    }

    // Save previous frame states for edge transitions
    oldStylusState = currentStylusState;
    wasMouseDown   = mouse.leftButton;
    wasMiddleDown  = mouse.middleButton;
}

/**
 * @brief Computes velocity-scaled dynamic eraser radius when isDynamicEraser is active.
 *
 * Mathematical Model:
 * Let dt = currentTimeSec - lastEraserTimeSec.
 * Pointer displacement distPx = hypot(dx, dy).
 * Instantaneous velocity v = distPx / dt in pixels per second.
 *
 * Normalized velocity parameter t = clamp((v - 100.0) / 1900.0, 0.0, 1.0).
 * Response curve: s = sqrt(t) (concave curve providing fast initial scaling).
 * Target radius: R_target = R_min + s * (R_max - R_min).
 *
 * Asymmetric smoothing:
 * alpha = 0.35 when expanding (fast swipe opens broad wiping swath).
 * alpha = 0.12 when contracting (gentle recovery prevents jitter).
 * R_current += alpha * (R_target - R_current).
 */
float InputStateMachine::UpdateDynamicEraserRadius(float currentX, float currentY, double currentTimeSec, bool isMoving, bool isDown) {
    if (!isDynamicEraser) {
        return eraserRadiusMm;
    }

    if (currentTimeSec <= 0.0) {
        currentTimeSec = SDL_GetTicks() * 0.001;
    }

    // Keep min/max radius bounds in sync with state machine parameters
    auto params = eraserPhysics.GetParameters();
    if (params.minRadiusMm != dynamicEraserMinRadiusMm || params.maxRadiusMm != dynamicEraserMaxRadiusMm) {
        params.minRadiusMm = dynamicEraserMinRadiusMm;
        params.maxRadiusMm = dynamicEraserMaxRadiusMm;
        eraserPhysics.SetParameters(params);
    }

    // Handle initial state or temporal discontinuity (> 250ms gap)
    double dt = currentTimeSec - lastEraserTimeSec;
    if (lastEraserTimeSec <= 0.0 || dt > 0.25 || dt <= 0.0001) {
        lastVelocityX = currentX;
        lastVelocityY = currentY;
        lastEraserTimeSec = currentTimeSec;
        if (currentDynamicEraserRadiusMm < dynamicEraserMinRadiusMm) {
            currentDynamicEraserRadiusMm = dynamicEraserMinRadiusMm;
            eraserPhysics.Reset(dynamicEraserMinRadiusMm);
        }
        return currentDynamicEraserRadiusMm;
    }

    float dx = currentX - lastVelocityX;
    float dy = currentY - lastVelocityY;
    float distPx = std::hypot(dx, dy);

    float speedPxPerSec = 0.0f;
    if (distPx > 0.5f) {
        speedPxPerSec = static_cast<float>(distPx / dt);
        lastVelocityX = currentX;
        lastVelocityY = currentY;
    }

    // Run physical mass-spring-damper simulation step
    currentDynamicEraserRadiusMm = eraserPhysics.Update(speedPxPerSec, static_cast<float>(dt));
    lastEraserTimeSec = currentTimeSec;

    return currentDynamicEraserRadiusMm;
}
