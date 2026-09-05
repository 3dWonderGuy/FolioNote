#pragma once
#include <SDL3/SDL.h>
#include "imgui.h"
#include "core/engine/canvas_engine.hpp"
#include "core/spatial/undo_redo_manager.hpp"
#include "app/window_state_manager.hpp"
#include "input_state_machine.hpp"
#include <algorithm>
#include <cmath>
#include <memory>



class DocumentSession;

// Top-level Manager unifying State Machine & Engine Loop
class InputManager {
public:

    // IO manager, it handles all the inputs and outputs
    InputStateMachine stateMachine;
    bool wasCanvasImageHovered = false;

    /**
     * @brief Processes an SDL event and updates the canvas and window state.
     * 
     * This function is used to track all of the user inputs from the user, mouse, 
     * stylus, and touch screen. It is also used to track the state of the window, 
     * such as when it is minimized, maximized, or resized.
     * 
     * @param event The SDL event to process.
     * @param canvas The canvas to update.
     * @param session The document session to commit objects to.
     * @param windowSM The window state manager to update.
     */
    void ProcessEvent(const SDL_Event& event, CanvasEngine& canvas, DocumentSession& session, WindowStateManager& windowSM);

    /**
     * @brief Handles keyboard events.
     * 
     * @param event The SDL event to handle.
     * @param canvas The canvas to update.
     */
    void HandleKeyboardEvent(const SDL_Event& event);
    /**
     * @brief Handles pen events.
     * 
     * @param event The SDL event to handle.
     */
    void HandlePenEvent(const SDL_Event& event);
    /**
     * @brief Handles touch screen events.
     * 
     * @param event The SDL event to handle.
     */
    void HandleTouchEvent(const SDL_Event& event);
    /**
     * @brief Handles mouse events.
     * 
     * @param event The SDL event to handle.
     */
    void HandleMouseEvent(const SDL_Event& event);
};