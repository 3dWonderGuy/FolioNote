/**
 * @file input_manager.cpp
 * @brief Implementation of the top-level InputManager coordinating SDL3 events,
 *        hardware device dispatch, and the semantic InputStateMachine.
 *
 * ARCHITECTURE OVERVIEW:
 * ----------------------
 * FolioNote supports rich multimodal input across diverse hardware configurations:
 *   1. Active / EMR Stylus (Pressure, Tilt, Distance, Proximity, Barrel Buttons, Eraser)
 *   2. Capacitive Multi-Touch (Multi-finger tracking, Gestures, Pinch-to-Zoom, Two-Finger Pan)
 *   3. Mouse / Trackpad (Absolute position, Relative deltas, Buttons, Smooth Scroll Wheel)
 *   4. Keyboard Modifiers (Ctrl, Shift, Alt, Space for transient navigation)
 *
 * InputManager acts as the primary hardware ingestion bridge. It consumes raw operating
 * system events via SDL3, performs essential validation (window state stability, synthetic
 * event filtering, coordinate normalization), updates the low-level hardware tracking
 * structures in InputStateMachine, and triggers semantic evaluation and arbitration.
 *
 * DATA FLOW:
 * ----------
 *   [OS / SDL3 Event Loop]
 *            |
 *            v
 *   InputManager::ProcessEvent()
 *     |---> WindowStateManager (guard against input during window resizes / moves)
 *     |---> Timestamp Normalization (convert SDL ns timestamps to seconds relative to launch)
 *     |---> Hardware Handlers:
 *     |       * HandlePenEvent()       --> stateMachine.pen
 *     |       * HandleTouchEvent()     --> stateMachine.activeFingers (normalized [0,1] -> px)
 *     |       * HandleMouseEvent()     --> stateMachine.mouse (synthetic touch/pen filtered)
 *     |       * HandleKeyboardEvent()  --> stateMachine.keyboard
 *     |
 *     v
 *   ImGui Focus Arbitration (differentiate canvas/PDF document hover from UI chrome capture)
 *     |
 *     v
 *   InputStateMachine::ProcessInputState()
 *     (Device arbitration Stylus > Touch > Mouse -> Semantic dispatch to CanvasEngine / DocumentSession)
 */

#include "input_state_machine.hpp"
#include "app/window_state_manager.hpp"
#include "input_manager.hpp"

// =============================================================================
// EVENT DISPATCH & MAIN PIPELINE
// =============================================================================

/**
 * @brief Primary entry point for all SDL events destined for input handling.
 *
 * Executes a 4-stage pipeline:
 *   Stage 1: Window State Validation - Abort if window is in an unstable geometry state.
 *   Stage 2: Event Timestamp Tracking - Convert nanoseconds to application-relative seconds.
 *   Stage 3: Hardware-Specific Routing - Populate raw hardware telemetry structures.
 *   Stage 4: State Machine Evaluation - Arbitrate active device and dispatch semantic actions.
 *
 * @param event    The raw SDL_Event received from SDL_PollEvent.
 * @param canvas   Reference to CanvasEngine (handles rendering, viewport camera, stroke mesh).
 * @param session  Reference to DocumentSession (manages committed objects, pages, undo/redo).
 * @param windowSM Reference to WindowStateManager (tracks maximize/minimize/resize stability).
 */
void InputManager::ProcessEvent(const SDL_Event& event, CanvasEngine& canvas, DocumentSession& session, WindowStateManager& windowSM) {
    // -----------------------------------------------------------------------------
    // STAGE 1: WINDOW STATE VALIDATION
    // -----------------------------------------------------------------------------
    // Inform the window state manager about window events (SDL_EVENT_WINDOW_*).
    // It maintains an internal state machine (e.g. Stable, Resizing, Moving).
    windowSM.ProcessSDLEvent(event);

    // If the window is currently being resized or moved by the user or OS, canvas
    // dimensions, swapchain buffers, and projection matrices are in flux. Aborting
    // input processing here prevents stray strokes, coordinate warping, and division
    // by zero in aspect-ratio calculations.
    if (windowSM.getCurrentState() != WindowState::Stable) return;

    // -----------------------------------------------------------------------------
    // STAGE 2: EVENT TIMESTAMP TRACKING
    // -----------------------------------------------------------------------------
    // SDL3 provides high-resolution 64-bit nanosecond event timestamps (event.common.timestamp).
    // EventTimestampToSec converts this into floating-point seconds relative to application start:
    //   t_sec = (timestamp_ns - appStartTimeNs) * 1e-9
    // This monotonically increasing timestamp is essential for Catmull-Rom spline smoothing,
    // instantaneous drawing velocity calculation, and gesture flick detection.
    stateMachine.latestEventTimeSec = stateMachine.EventTimestampToSec(event.common.timestamp);

    // -----------------------------------------------------------------------------
    // STAGE 3: HARDWARE-SPECIFIC INPUT ROUTING
    // -----------------------------------------------------------------------------
    // Distribute the raw event to dedicated hardware handlers.
    // Each handler parses device-specific data packets and updates stateMachine telemetry.
    switch (event.type) {
        // --- KEYBOARD SHORTCUTS & MODIFIERS ---
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            HandleKeyboardEvent(event);
            break;

        // --- ACTIVE / PASSIVE DIGITIZER (STYLUS / PEN) ---
        case SDL_EVENT_PEN_PROXIMITY_IN:
        case SDL_EVENT_PEN_PROXIMITY_OUT:
        case SDL_EVENT_PEN_AXIS:
        case SDL_EVENT_PEN_DOWN:
        case SDL_EVENT_PEN_UP:
        case SDL_EVENT_PEN_MOTION:
        case SDL_EVENT_PEN_BUTTON_DOWN:
        case SDL_EVENT_PEN_BUTTON_UP:
            HandlePenEvent(event);
            break;

        // --- CAPACITIVE MULTI-TOUCH ---
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
        case SDL_EVENT_FINGER_MOTION:
            HandleTouchEvent(event);
            break;

        // --- MOUSE & TRACKPAD ---
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_WHEEL:
            HandleMouseEvent(event);
            break;

        default:
            // Other event types (controllers, sensors, drop events) are handled elsewhere or ignored.
            break;
    }

    // -----------------------------------------------------------------------------
    // STAGE 4: STATE MACHINE EVALUATION & UI ARBITRATION
    // -----------------------------------------------------------------------------
    // Determine whether Dear ImGui UI chrome has captured pointer focus.
    //
    // ARBITRATION LOGIC:
    // ImGui's IO flag `WantCaptureMouse` is true whenever the pointer is over any ImGui
    // window. However, both the main Infinite Canvas and the PDF Viewer render inside
    // ImGui window viewports (as ImGui::Image). Therefore, if the cursor is hovering
    // the document canvas (wasCanvasImageHovered) or the PDF page surface
    // (stateMachine.isPdfCanvasHovered), we negate WantCaptureMouse so that the canvas
    // receives interaction rather than the host ImGui window frame.
    //
    // Mathematical formulation:
    //   imguiHasFocus = WantCaptureMouse AND NOT (wasCanvasImageHovered OR isPdfCanvasHovered)
    bool imguiHasFocus = (ImGui::GetIO().WantCaptureMouse && !(wasCanvasImageHovered || stateMachine.isPdfCanvasHovered));
    
    // Diagnostic log for genuine physical mouse clicks (excluding synthetic SDL touch/pen mouse events)
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (event.button.which != SDL_TOUCH_MOUSEID && event.button.which != SDL_PEN_MOUSEID) {
            LOG_INFO(InputManager, "REAL MOUSE DOWN! WantCaptureMouse: " + std::to_string(ImGui::GetIO().WantCaptureMouse) + 
                                   ", wasCanvasImageHovered: " + std::to_string(wasCanvasImageHovered) + 
                                   ", imguiHasFocus: " + std::to_string(imguiHasFocus));
        }
    }

    // Hand off all consolidated telemetry to the InputStateMachine:
    // 1. Device Priority Arbitration: Stylus (highest) > Touch > Mouse.
    // 2. Interaction State Determination: Inking, Eraser, Panning, Selecting.
    // 3. Dispatch: Invokes CanvasEngine / DocumentSession APIs to draw strokes, pan, or erase.
    stateMachine.ProcessInputState(canvas, session, imguiHasFocus);
}

// =============================================================================
// HARDWARE HANDLER: PEN / STYLUS
// =============================================================================

/**
 * @brief Processes high-precision stylus and active digitizer telemetry.
 *
 * Captures all physical capabilities of modern styluses (Surface Pen, Apple Pencil, Wacom):
 *   - Proximity / Hover detection (sensor field entry/exit before physical contact).
 *   - Continuous axis updates: Pressure [0.0, 1.0], Distance, Tilt-X, Tilt-Y [-90.0, +90.0].
 *   - Physical tip contact (Down, Motion, Up).
 *   - Hardware barrel buttons (Barrel 1, 2, 3) and physical eraser tail tip detection.
 *
 * @param event The SDL_Event containing SDL_EVENT_PEN_* data.
 */
void InputManager::HandlePenEvent(const SDL_Event& event) {
    // Record current system tick to establish recent pen activity for device arbitration timeouts
    stateMachine.lastPenTimestampMs = SDL_GetTicks();
    auto& pen = stateMachine.pen;

    // -------------------------------------------------------------------------
    // 1. PROXIMITY TELEMETRY
    // Stylus enters or exits the digitizer's electromagnetic resonance (EMR) field
    // (typically ~10-20mm above the display surface).
    // -------------------------------------------------------------------------
    if (event.type == SDL_EVENT_PEN_PROXIMITY_IN) {
        pen.inProximity = true;
        pen.isHovering = true;
        LOG_INFO(InputManager, "PEN PROXIMITY_IN (PenID: " + std::to_string(event.pproximity.which) + ")");
        return;
    }
    else if (event.type == SDL_EVENT_PEN_PROXIMITY_OUT) {
        // Pen was lifted beyond the digitizer's sensing range; reset hover & contact flags
        pen.inProximity = false;
        pen.isHovering = false;
        pen.isDown = false;
        pen.eraserTip = false;
        LOG_INFO(InputManager, "PEN PROXIMITY_OUT (PenID: " + std::to_string(event.pproximity.which) + ")");
        return;
    }

    // -------------------------------------------------------------------------
    // 2. HIGH-FREQUENCY AXIS TELEMETRY
    // Digitizers report continuous physical parameters independently of contact events.
    // -------------------------------------------------------------------------
    if (event.type == SDL_EVENT_PEN_AXIS) {
        // Pressure: Normalized [0.0, 1.0]. Clamped to avoid hardware driver overflows.
        if (event.paxis.axis == SDL_PEN_AXIS_PRESSURE) {
            pen.pressure = std::clamp(event.paxis.value, 0.0f, 1.0f);
        }
        // Distance: Normalized distance above surface while hovering in proximity.
        else if (event.paxis.axis == SDL_PEN_AXIS_DISTANCE) {
            pen.distance = event.paxis.value;
        }
        // Tilt X/Y: Stylus inclination angles in degrees relative to the screen normal (-90° to +90°).
        // Used by rendering shaders to simulate angle-dependent brush calligraphics and pencil shading.
        else if (event.paxis.axis == SDL_PEN_AXIS_XTILT) {
            pen.tiltX = event.paxis.value;
        }
        else if (event.paxis.axis == SDL_PEN_AXIS_YTILT) {
            pen.tiltY = event.paxis.value;
        }

        // Check if the physical tail-end eraser tip is currently active via hardware state bitmask
        bool isEraser = (event.paxis.pen_state & SDL_PEN_INPUT_ERASER_TIP) != 0;
        if (pen.eraserTip != isEraser) {
            pen.eraserTip = isEraser;
            LOG_INFO(InputManager, std::string("PEN ERASER TIP ") + (isEraser ? "ACTIVE" : "INACTIVE"));
        }
        return;
    }

    // -------------------------------------------------------------------------
    // 3. CONTACT & MOTION (DOWN, MOTION, UP)
    // -------------------------------------------------------------------------
    if (event.type == SDL_EVENT_PEN_DOWN) {
        // Pen tip touches the screen surface; start stroke / engagement
        pen.isDown = true;
        pen.isHovering = false;
        pen.inProximity = true;
        pen.x = event.ptouch.x;
        pen.y = event.ptouch.y;

        // Check eraser status either from the ptouch.eraser boolean or pen_state bitmask
        bool isEraser = event.ptouch.eraser || ((event.ptouch.pen_state & SDL_PEN_INPUT_ERASER_TIP) != 0);
        if (pen.eraserTip != isEraser) {
            pen.eraserTip = isEraser;
            LOG_INFO(InputManager, std::string("PEN ERASER TIP ") + (isEraser ? "ACTIVE" : "INACTIVE"));
        }
    }
    else if (event.type == SDL_EVENT_PEN_MOTION) {
        // Stylus position changed (either dragging while down or hovering above the glass)
        pen.x = event.pmotion.x;
        pen.y = event.pmotion.y;
        pen.inProximity = true;
        if (!pen.isDown) pen.isHovering = true;

        bool isEraser = (event.pmotion.pen_state & SDL_PEN_INPUT_ERASER_TIP) != 0;
        if (pen.eraserTip != isEraser) {
            pen.eraserTip = isEraser;
            LOG_INFO(InputManager, std::string("PEN ERASER TIP ") + (isEraser ? "ACTIVE" : "INACTIVE"));
        }
    }
    else if (event.type == SDL_EVENT_PEN_UP) {
        // Pen tip lifted off the surface; end stroke, transition back to hover state
        pen.isDown = false;
        pen.isHovering = true;

        bool isEraser = event.ptouch.eraser || ((event.ptouch.pen_state & SDL_PEN_INPUT_ERASER_TIP) != 0);
        if (pen.eraserTip != isEraser) {
            pen.eraserTip = isEraser;
            LOG_INFO(InputManager, std::string("PEN ERASER TIP ") + (isEraser ? "ACTIVE" : "INACTIVE"));
        }
    }
    // -------------------------------------------------------------------------
    // 4. HARDWARE BARREL BUTTONS
    // Physical side buttons on the pen stylus barrel (mapped to eraser or lasso shortcuts)
    // -------------------------------------------------------------------------
    else if (event.type == SDL_EVENT_PEN_BUTTON_DOWN || event.type == SDL_EVENT_PEN_BUTTON_UP) {
        bool down = (event.type == SDL_EVENT_PEN_BUTTON_DOWN);
        LOG_INFO(InputManager, "PEN BUTTON " + std::to_string(event.pbutton.button) + (down ? " DOWN" : " UP"));
        
        // SDL button indices: 1 = primary barrel, 2 = secondary barrel, 3 = tertiary
        if (event.pbutton.button == 1)      pen.barrel1 = down;
        else if (event.pbutton.button == 2) pen.barrel2 = down;
        else if (event.pbutton.button == 3) pen.barrel3 = down;
        else {
            LOG_WARN(InputManager, "Unknown pen button: " + std::to_string(event.pbutton.button));
        }

        bool isEraser = (event.pbutton.pen_state & SDL_PEN_INPUT_ERASER_TIP) != 0;
        if (pen.eraserTip != isEraser) {
            pen.eraserTip = isEraser;
            LOG_INFO(InputManager, std::string("PEN ERASER TIP ") + (isEraser ? "ACTIVE" : "INACTIVE"));
        }
    }
}

// =============================================================================
// HARDWARE HANDLER: MOUSE & TRACKPAD
// =============================================================================

/**
 * @brief Processes standard mouse and trackpad interactions.
 *
 * SYNTHETIC EVENT SUPPRESSION:
 * Operating systems (Windows Ink, macOS Cocoa) synthesize legacy mouse events
 * whenever touch or pen contacts occur, ensuring legacy applications receive clicks.
 * Because FolioNote has native, dedicated pipelines for Touch and Pen with higher
 * precision, processing synthetic mouse events would cause double clicks, jitter,
 * and conflicting device arbitration. We identify and drop synthetic events using
 * SDL's sentinel device IDs: `SDL_TOUCH_MOUSEID` and `SDL_PEN_MOUSEID`.
 *
 * @param event The SDL_Event containing SDL_EVENT_MOUSE_* data.
 */
void InputManager::HandleMouseEvent(const SDL_Event& event) {
    // Filter out synthetic mouse events generated by SDL from capacitive touch or pen input
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event.button.which == SDL_TOUCH_MOUSEID || event.button.which == SDL_PEN_MOUSEID) {
            return;
        }
    } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
        if (event.motion.which == SDL_TOUCH_MOUSEID || event.motion.which == SDL_PEN_MOUSEID) {
            return;
        }
    }

    // Timestamp genuine physical mouse activity
    stateMachine.lastMouseTimestampMs = SDL_GetTicks();
    auto& mouse = stateMachine.mouse;

    // -------------------------------------------------------------------------
    // BUTTON STATE TRACKING
    // -------------------------------------------------------------------------
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        bool down = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        if (event.button.button == SDL_BUTTON_LEFT)        mouse.leftButton = down;
        else if (event.button.button == SDL_BUTTON_RIGHT)  mouse.rightButton = down;
        else if (event.button.button == SDL_BUTTON_MIDDLE) mouse.middleButton = down;
        
        // Window-relative coordinates in pixels
        mouse.x = event.button.x;
        mouse.y = event.button.y;
    }
    // -------------------------------------------------------------------------
    // MOTION & DELTAS
    // -------------------------------------------------------------------------
    else if (event.type == SDL_EVENT_MOUSE_MOTION) {
        mouse.x = event.motion.x;
        mouse.y = event.motion.y;
        // Relative displacement deltas (useful for camera panning and drag vectors)
        mouse.dx = event.motion.xrel;
        mouse.dy = event.motion.yrel;
    }
    // -------------------------------------------------------------------------
    // SCROLL WHEEL
    // -------------------------------------------------------------------------
    else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        // wheelX: Positive = right, Negative = left (horizontal scroll / trackpad gesture)
        // wheelY: Positive = forward/up (zoom in / scroll up), Negative = backward/down
        mouse.wheelX = event.wheel.x;
        mouse.wheelY = event.wheel.y;
    }
}

// =============================================================================
// HARDWARE HANDLER: KEYBOARD
// =============================================================================

/**
 * @brief Tracks modifier keys and hotkeys relevant to canvas navigation.
 *
 * Monitors:
 *   - Ctrl:  Multi-select modifier, zoom modifier, undo/redo shortcuts.
 *   - Shift: Constrained drawing axis (straight horizontal/vertical lines), aspect ratio lock.
 *   - Alt:   Color picker eyedropper, alternate clone drag.
 *   - Space: Transient canvas panning hand tool (hold Space + drag mouse to pan).
 *
 * @param event The SDL_Event containing SDL_EVENT_KEY_DOWN or SDL_EVENT_KEY_UP.
 */
void InputManager::HandleKeyboardEvent(const SDL_Event& event) {
    // Query the global modifier bitmask to capture both left and right variants of modifiers
    SDL_Keymod mod = SDL_GetModState();
    stateMachine.keyboard.ctrl  = (mod & SDL_KMOD_CTRL) != 0;
    stateMachine.keyboard.shift = (mod & SDL_KMOD_SHIFT) != 0;
    stateMachine.keyboard.alt   = (mod & SDL_KMOD_ALT) != 0;

    // Spacebar triggers transient hand/panning tool when held down
    stateMachine.keyboard.space = (event.key.key == SDLK_SPACE) ? (event.type == SDL_EVENT_KEY_DOWN) : stateMachine.keyboard.space; 
}
    
// =============================================================================
// HARDWARE HANDLER: CAPACITIVE MULTI-TOUCH
// =============================================================================

/**
 * @brief Processes multi-touch capacitive screen events (up to 10 concurrent fingers).
 *
 * COORDINATE SPACE TRANSFORMATION:
 * SDL3 touch coordinates (event.tfinger.x, y, dx, dy) are normalized floating-point
 * values in the range [0.0, 1.0] relative to the touch device surface.
 * To align them with the screen-space pixel coordinates used by the Mouse and Pen
 * pipelines, we query the window dimensions (W, H) and transform:
 *
 *   Pixel X:       px  = x_normalized  * WindowWidth
 *   Pixel Y:       py  = y_normalized  * WindowHeight
 *   Pixel Delta X: pdx = dx_normalized * WindowWidth
 *   Pixel Delta Y: pdy = dy_normalized * WindowHeight
 *
 * SLOT ALLOCATION STRATEGY:
 * FolioNote uses a pre-allocated fixed-capacity array (std::array<TouchSlot, 10> activeFingers)
 * to avoid heap allocations in the high-frequency touch path:
 *   - Sentinel: `fingerID == -1` designates an empty slot.
 *   - FINGER_DOWN:    Locates the first empty slot (-1) and binds the finger ID, position, and pressure.
 *   - FINGER_MOTION:  Locates the existing slot matching the finger ID and updates its position/delta.
 *   - FINGER_UP/CANC: Locates the active slot, resets `fingerID = -1`, and clears coordinates.
 *
 * Downstream, TouchGestureRecognizer evaluates these active slots to compute multi-finger gestures:
 *   - Pinch-to-Zoom: Euclidean distance ratio between two finger contact points:
 *       scale = ||p1_curr - p2_curr|| / ||p1_prev - p2_prev||
 *   - Two-Finger Pan: Centroid translation delta:
 *       delta_C = ((p1_curr + p2_curr) / 2) - ((p1_prev + p2_prev) / 2)
 *
 * @param event The SDL_Event containing SDL_EVENT_FINGER_* data.
 */
void InputManager::HandleTouchEvent(const SDL_Event& event) {
    stateMachine.lastTouchTimestampMs = SDL_GetTicks();

    // Query window dimensions in pixels for normalized -> pixel coordinate conversion
    int w = 1920, h = 1080;
    SDL_Window* win = SDL_GetWindowFromID(event.tfinger.windowID);
    if (win) {
        SDL_GetWindowSizeInPixels(win, &w, &h);
    }
    float fw = static_cast<float>(w > 0 ? w : 1);
    float fh = static_cast<float>(h > 0 ? h : 1);

    // SDL3 touch coordinates are normalized [0.0, 1.0]; scale to window pixel space
    float px  = event.tfinger.x * fw;
    float py  = event.tfinger.y * fh;
    float pdx = event.tfinger.dx * fw;
    float pdy = event.tfinger.dy * fh;

    // -------------------------------------------------------------------------
    // FINGER CONTACT DOWN: Allocate slot
    // -------------------------------------------------------------------------
    if (event.type == SDL_EVENT_FINGER_DOWN) {
        LOG_INFO(InputManager, "TOUCH FINGER DOWN: ID=" + std::to_string(event.tfinger.fingerID) +
                               " at (" + std::to_string(px) + ", " + std::to_string(py) + ")");
        for (auto& slot : stateMachine.activeFingers) {
            // Find first unused slot marked by sentinel fingerID == -1
            if (slot.fingerID == -1) {
                slot.fingerID = event.tfinger.fingerID;
                slot.point.x = px;
                slot.point.y = py;
                slot.point.dx = pdx;
                slot.point.dy = pdy;
                slot.point.pressure = event.tfinger.pressure;
                break;
            }
        }
    }
    // -------------------------------------------------------------------------
    // FINGER MOTION: Update allocated slot
    // -------------------------------------------------------------------------
    else if (event.type == SDL_EVENT_FINGER_MOTION) {
        for (auto& slot : stateMachine.activeFingers) {
            if (slot.fingerID == event.tfinger.fingerID) {
                slot.point.x = px;
                slot.point.y = py;
                slot.point.dx = pdx;
                slot.point.dy = pdy;
                slot.point.pressure = event.tfinger.pressure;
                break;
            }
        }
    }
    // -------------------------------------------------------------------------
    // FINGER UP / CANCELED: Free slot
    // -------------------------------------------------------------------------
    else if (event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED) {
        LOG_INFO(InputManager, "TOUCH FINGER UP: ID=" + std::to_string(event.tfinger.fingerID));
        for (auto& slot : stateMachine.activeFingers) {
            if (slot.fingerID == event.tfinger.fingerID) {
                // Return slot to pool by resetting sentinel to -1
                slot.fingerID = -1;
                slot.point = {};
                break;
            }
        }
    }
}