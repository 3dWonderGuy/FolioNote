#include "input_state_machine.hpp"
#include "app/window_state_manager.hpp"
#include "input_manager.hpp"

void InputManager::ProcessEvent(const SDL_Event& event, CanvasEngine& canvas, DocumentSession& session, WindowStateManager& windowSM) {
    // -----------------------------------------------------------------------------
    // STAGE 1: WINDOW STATE VALIDATION
    // -----------------------------------------------------------------------------
    // First, let the window state manager process the raw SDL event to track resizes,
    // minimize, maximize, and other window lifecycle events.
    windowSM.ProcessSDLEvent(event);

    // If the window is currently being resized or moved (i.e. not Stable),
    // we abort input processing. This prevents stray drawing or interactions
    // while the canvas dimensions are in a state of flux.
    if(windowSM.getCurrentState() != WindowState::Stable) return;

    // -----------------------------------------------------------------------------
    // STAGE 2: EVENT TIMESTAMP TRACKING
    // -----------------------------------------------------------------------------
    // Convert the raw nanosecond timestamp from the SDL event into seconds relative
    // to the application start time. This is critical for stroke smoothing, velocity
    // calculation, and gesture recognition in the State Machine.
    stateMachine.latestEventTimeSec = stateMachine.EventTimestampToSec(event.common.timestamp);

    // -----------------------------------------------------------------------------
    // STAGE 3: HARDWARE-SPECIFIC INPUT ROUTING
    // -----------------------------------------------------------------------------
    // Route the incoming SDL event to the appropriate hardware handler.
    // Each handler updates the 'stateMachine' directly by mutating its public fields 
    // (e.g. stateMachine.pen, stateMachine.mouse).
    switch (event.type) {
        // --- KEYBOARD SHORTCUTS ---
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            HandleKeyboardEvent(event);
            break;

        // --- STYLUS / PEN ---
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

        // --- MOUSE ---
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_WHEEL:
            HandleMouseEvent(event);
            break;

        default:
            // Unhandled events (like joystick, sensors, etc.) are ignored.
            break;
    }

    // -----------------------------------------------------------------------------
    // STAGE 4: STATE MACHINE EVALUATION
    // -----------------------------------------------------------------------------
    // Now that the raw hardware state tracking variables (pen, mouse, touch, keyboard) 
    // have been updated, we ask the InputStateMachine to make sense of it all.
    // It will:
    // 1. Determine which device is "Active" (Arbitration).
    // 2. Decide the user's intent (Inking, Eraser, Panning, etc).
    // 3. Dispatch the high-level semantic events to the Canvas and Document.
    bool imguiWantsInput = (ImGui::GetIO().WantCaptureMouse && !wasCanvasImageHovered);
    
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        LOG_INFO(InputManager, "MOUSE DOWN! WantCaptureMouse: " + std::to_string(ImGui::GetIO().WantCaptureMouse) + 
                               ", wasCanvasImageHovered: " + std::to_string(wasCanvasImageHovered) + 
                               ", imguiWantsInput: " + std::to_string(imguiWantsInput));
    }
    stateMachine.ProcessInputState(canvas, session, imguiWantsInput);
}

void InputManager::HandlePenEvent(const SDL_Event& event) {
    // Record the timestamp of the last pen event for device arbitration in the State Machine.
    // If a pen event happened recently, the State Machine will prioritize stylus input over touch or mouse.
    stateMachine.lastPenTimestampMs = SDL_GetTicks();
    
    // Grab a reference to the pen state structure inside the State Machine
    auto& pen = stateMachine.pen;

    // Handle high-frequency telemetry data like pressure, tilt, and hover distance.
    // These often arrive independently of button down/up events.
    if (event.type == SDL_EVENT_PEN_AXIS) {
        if (event.paxis.axis == SDL_PEN_AXIS_PRESSURE)      pen.pressure = std::clamp(event.paxis.value, 0.0f, 1.0f);
        else if (event.paxis.axis == SDL_PEN_AXIS_DISTANCE) pen.distance = event.paxis.value;
        else if (event.paxis.axis == SDL_PEN_AXIS_XTILT)    pen.tiltX = event.paxis.value;
        else if (event.paxis.axis == SDL_PEN_AXIS_YTILT)    pen.tiltY = event.paxis.value;
        return;
    }


    if (event.type == SDL_EVENT_PEN_DOWN) {
        // The stylus tip has physically touched the screen/tablet surface.
        pen.isDown = true;
        pen.isHovering = false;
        pen.x = event.ptouch.x;
        pen.y = event.ptouch.y;
    }
    else if (event.type == SDL_EVENT_PEN_MOTION) {
        // The stylus has moved. It could be dragging on the screen (isDown == true)
        // or hovering just above it (isDown == false).
        pen.x = event.pmotion.x;
        pen.y = event.pmotion.y;
        if (!pen.isDown) pen.isHovering = true;
    }
    else if (event.type == SDL_EVENT_PEN_UP) {
        // The stylus tip has lifted off the screen/tablet surface.
        pen.isDown = false;
        pen.isHovering = true;
    }
    else if (event.type == SDL_EVENT_PEN_BUTTON_DOWN || event.type == SDL_EVENT_PEN_BUTTON_UP) {
        bool down = (event.type == SDL_EVENT_PEN_BUTTON_DOWN);
        if (event.pbutton.button == 1) pen.barrel1 = down;
        else if (event.pbutton.button == 2) pen.barrel2 = down;
    }
}

void InputManager::HandleMouseEvent(const SDL_Event& event) {
    stateMachine.lastMouseTimestampMs = SDL_GetTicks();
    auto& mouse = stateMachine.mouse;

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        bool down = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        if (event.button.button == SDL_BUTTON_LEFT)        mouse.leftButton = down;
        else if (event.button.button == SDL_BUTTON_RIGHT)  mouse.rightButton = down;
        else if (event.button.button == SDL_BUTTON_MIDDLE) mouse.middleButton = down;
        mouse.x = event.button.x;
        mouse.y = event.button.y;
    }
    else if (event.type == SDL_EVENT_MOUSE_MOTION) {
        mouse.x = event.motion.x;
        mouse.y = event.motion.y;
        mouse.dx = event.motion.xrel;
        mouse.dy = event.motion.yrel;
    }
    else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        mouse.wheelX = event.wheel.x;
        mouse.wheelY = event.wheel.y;
    }
}

void InputManager::HandleKeyboardEvent(const SDL_Event& event) {
    SDL_Keymod mod = SDL_GetModState();
    stateMachine.keyboard.ctrl  = (mod & SDL_KMOD_CTRL) != 0;
    stateMachine.keyboard.shift = (mod & SDL_KMOD_SHIFT) != 0;
    stateMachine.keyboard.alt   = (mod & SDL_KMOD_ALT) != 0;
    stateMachine.keyboard.space = (event.key.key == SDLK_SPACE) ? (event.type == SDL_EVENT_KEY_DOWN) : stateMachine.keyboard.space; 
    
}
    
void InputManager::HandleTouchEvent(const SDL_Event& event) {
    stateMachine.lastTouchTimestampMs = SDL_GetTicks();

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

    if (event.type == SDL_EVENT_FINGER_DOWN) {
        for (auto& slot : stateMachine.activeFingers) {
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
    else if (event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED) {
        for (auto& slot : stateMachine.activeFingers) {
            if (slot.fingerID == event.tfinger.fingerID) {
                slot.fingerID = -1;
                slot.point = {};
                break;
            }
        }
    }
}