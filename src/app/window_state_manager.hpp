#pragma once
#include <SDL3/SDL.h>
#include <chrono>


/**
 * Window State manger responsible for tracking current window state
 * and managing freeze-frame logic during transitions such as resizing, moving, and fullscreen toggling.
 * States:
 * - Stable: Normal operation, 120 FPS rendering.
 * - TransitionFreezing: Temporary freeze during transitions to avoid VRAM reallocations.   
 * - Resizing: User is interactively resizing the window.
 * - Moving: User is moving the window.
 * - Fullscreen: Window is in fullscreen mode.
 * - Minimized: Window is minimized, rendering is suspended.
 * - Closing: Window is closing, cleanup operations may be performed.
 * - Idle: Window is idle, no user interaction.
 * - EmergancyState: Reserved for emergency handling, such as critical errors.
 */
enum class WindowState {
    Stable,
    TransitionFreezing,
    Resizing,
    Moving,
    Fullscreen,
    Minimized, 
    Closing,
    Idle,
    EmergencyState
};

class WindowStateManager {
public:

    WindowState currentState = WindowState::Stable; // sets current windows state
    WindowState previousState = WindowState::Stable; // tracks where we came from
    
    int windowWidth = 1920;         // tracks current window width
    int windowHeight = 1080;        // trackes current window height
    bool isFullscreen = false;      // checks if windows is in fullscreen mode or not
    bool isMaximized = false;       // checks if window is maximized, basicaly borderless max canvas space
    bool isFocused = true;          // checks if windown is under user focus (active window) or not


    // This is a frame freezer counter that is used to temporarily freeze the canvas rendering during transitions.
    // Issue I tracked earlier was that, going full screen mode or resize will cause gpu spikes and freezes
    int freezeFrameCounter = 0;
    const int DEFAULT_FREEZE_FRAMES = 30; // 30 frames @ ~60fps = ~500ms settling window for slow drivers (Intel Arc)

    // The SDL window handle - needed so we can toggle VSync during transitions
    SDL_Window* sdlWindow = nullptr;

    // Initializes the WindowStateManager with the initial window dimensions.
    void Init(int initialW, int initialH, SDL_Window* win = nullptr) {
        windowWidth = initialW;
        windowHeight = initialH;
        currentState = WindowState::Stable;
        sdlWindow = win;
    }

    // This is an event handler for the operating system's window lifecycle 
    // events produced by SDL3. It acts as a bridge between the OS window manager and your rendering pipeline.
    void ProcessSDLEvent(const SDL_Event& event) {

        switch (event.type) {
            
            // Handle window resize and pixel size change events
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                windowWidth = event.window.data1;
                windowHeight = event.window.data2;
                EnterTransition(WindowState::Resizing);
                break;

            // Handle window move events
            case SDL_EVENT_WINDOW_MOVED:
                    currentState = WindowState::Moving;
                break;

            // Handle window minimize and hide events
            case SDL_EVENT_WINDOW_MINIMIZED:
            case SDL_EVENT_WINDOW_HIDDEN:
                currentState = WindowState::Minimized;
                break;
            
            // Handle window restore and show events
            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_SHOWN:
                isMaximized = false;
                EnterTransition(WindowState::TransitionFreezing);
                break;

            // Handle window maximize and fullscreen events
            case SDL_EVENT_WINDOW_MAXIMIZED:
                isMaximized = true;
                EnterTransition(WindowState::Fullscreen);
                break;

            // Handle window unmaximize events
            case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
                isFullscreen = true;
                EnterTransition(WindowState::Fullscreen);
                break;

            // Handle window exit fullscreen events
            case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
                isFullscreen = false;
                EnterTransition(WindowState::TransitionFreezing);
                break;

            // Handle window focus events
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                isFocused = true;
                EnterTransition(WindowState::Stable);
                break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
                isFocused = false;
                // Only drop to Idle if we aren't already fully Minimized/Suspended
                if (currentState != WindowState::Minimized) {
                    EnterTransition(WindowState::Idle);
                }
                break;  

            // Handle window close request events
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                EnterTransition(WindowState::Closing);
                break;
        }
    }

    // Called inside main loop every frame
    void Update() {
        // Do nothing, rendering should be suspended
        if (currentState == WindowState::Minimized || currentState == WindowState::Closing) {
            return;
        }

        // Count down the freeze window during any transition
        if (currentState == WindowState::TransitionFreezing || 
            currentState == WindowState::Resizing || 
            currentState == WindowState::Moving ||
            currentState == WindowState::Fullscreen) {
            
            if (freezeFrameCounter > 0) {
                freezeFrameCounter--;
            } else {
                // Freeze window expired - re-enable VSync and return to stable rendering.
                // This applies to ALL states including Fullscreen, otherwise VSync stays
                // disabled forever and the GPU pegs at 70%+ doing nothing.
#if defined(__ANDROID__)
                if (sdlWindow) SDL_GL_SetSwapInterval(0);
#else
                if (sdlWindow) SDL_GL_SetSwapInterval(1);
#endif
                ChangeState(WindowState::Stable);
            }
        }
    }

    // Returns true when the GPU swapchain is mid-transition and we should skip heavy GPU work
    bool IsInTransition() const {
        return currentState == WindowState::TransitionFreezing ||
               currentState == WindowState::Resizing ||
               currentState == WindowState::Moving ||
               (currentState == WindowState::Fullscreen && freezeFrameCounter > 0);
    }

    
    bool ShouldFreezeCanvasRender() const {
        return currentState == WindowState::TransitionFreezing  || 
               currentState == WindowState::Moving              ||
               currentState == WindowState::Idle                ||
               currentState == WindowState::EmergencyState      ||
               currentState == WindowState::Closing             ||
               currentState == WindowState::Resizing            ||
               (currentState == WindowState::Fullscreen && freezeFrameCounter > 0) ||
               currentState == WindowState::Minimized;
    }

    WindowState getCurrentState() const {
        return currentState;
    }

    const char* GetStateName() const {
        switch (currentState) {
            case WindowState::Stable:             return "Stable (Active Rendering)";
            case WindowState::TransitionFreezing: return "Transition Freezing";
            case WindowState::Resizing:           return "Interactive Resizing";
            case WindowState::Moving:             return "Window Moving";
            case WindowState::Minimized:          return "Minimized (Suspended)";
            case WindowState::Closing:            return "Closing";
            case WindowState::Idle:               return "Idle (Unfocused)";
            case WindowState::EmergencyState:     return "Emergency State";
            default:                              return "Unknown";
        }
    }

    const char* GetPreviousStateName() const {
        switch (previousState) {
            case WindowState::Stable:             return "Stable";
            case WindowState::TransitionFreezing: return "Transition Freezing";
            case WindowState::Resizing:           return "Interactive Resizing";
            case WindowState::Moving:             return "Window Moving";
            case WindowState::Minimized:          return "Minimized";
            case WindowState::Closing:            return "Closing";
            case WindowState::Idle:               return "Idle";
            case WindowState::EmergencyState:     return "Emergency State";
            default:                              return "Unknown";
        }
    }

private:
    // Core state routing logic
    void ChangeState(WindowState newState) {
        if (currentState != newState) {
            previousState = currentState;
            currentState = newState;
        }
    }

    void EnterTransition(WindowState targetState) {
        // CRITICAL: Disable VSync immediately before the state change.
        // On Intel Arc and some NVIDIA/AMD drivers, SDL_GL_SwapWindow can block
        // the main thread for hundreds of milliseconds while the OS is rebuilding
        // the display swapchain (e.g. during fullscreen toggle or window restore).
        // Disabling VSync here makes SwapWindow return instantly, preventing the
        // system-wide freeze and audio stutter the user experiences.
        if (sdlWindow) SDL_GL_SetSwapInterval(0);
        ChangeState(targetState);
        freezeFrameCounter = DEFAULT_FREEZE_FRAMES;
    }
};