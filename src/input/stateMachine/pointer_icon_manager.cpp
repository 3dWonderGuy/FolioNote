/**
 * =========================================================================================
 * @file input/stateMachine/pointer_icon_manager.cpp
 * @brief Implementation of Hardware Cursor Management and Interaction Lock Anti-Flicker System
 * =========================================================================================
 *
 * GENERAL WORKING PROCESS & ANTI-FLICKER ARCHITECTURE:
 * -----------------------------------------------------
 * 1. Cursor Pre-Allocation:
 *    During Initialize(), creates native OS hardware cursors via SDL_CreateSystemCursor().
 *    Zero allocations occur during live mouse move loops.
 *
 * 2. Interaction Lock Mechanics:
 *    When a drag begins (e.g. Gizmo handle resize, inking stroke, viewport pan), the state
 *    machine calls Lock(shape, reason).
 *    While locked:
 *    - All transient SetHoverShape() calls are safely ignored.
 *    - Moving the mouse across handle boundaries or off-canvas does not revert the cursor.
 *    - Frame-level render ticks keep the locked cursor persistently active.
 *
 * 3. Synchronization with ImGui:
 *    Also synchronizes ImGui::SetMouseCursor() so that ImGui's internal hit-test systems
 *    stay in full alignment with the native SDL3 hardware cursor.
 */

#include "input/stateMachine/pointer_icon_manager.hpp"
#include "utils/logger.hpp"
#include <imgui.h>

namespace FolioInput {

PointerIconManager::PointerIconManager() {
    // Initial state: Arrow, unlocked
    currentShape = PointerCursorShape::Arrow;
    hoverShape   = PointerCursorShape::Arrow;
    lockedShape  = PointerCursorShape::Arrow;
    currentLock  = CursorLockReason::None;
}

PointerIconManager::~PointerIconManager() {
    Shutdown();
}

/**
 * @brief Pre-allocates and caches all SDL system cursors.
 *
 * Input: None.
 * Output: Populates internal cursors map with valid SDL_Cursor* pointers.
 */
void PointerIconManager::Initialize() {
    if (isInitialized) return;

    // Cache standard hardware system cursors via SDL3
    cursors[PointerCursorShape::Arrow]      = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    cursors[PointerCursorShape::Crosshair]  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    cursors[PointerCursorShape::TextInput]  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    cursors[PointerCursorShape::ResizeAll]  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE);
    cursors[PointerCursorShape::ResizeNS]   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
    cursors[PointerCursorShape::ResizeEW]   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    cursors[PointerCursorShape::ResizeNESW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NESW_RESIZE);
    cursors[PointerCursorShape::ResizeNWSE] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NWSE_RESIZE);
    cursors[PointerCursorShape::Hand]       = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    cursors[PointerCursorShape::NotAllowed] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NOT_ALLOWED);

    // SDL_SYSTEM_CURSOR_GRABBING may not be supported on all OS platforms; fallback to MOVE if null
    SDL_Cursor* grabbingCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_GRABBING);
    if (!grabbingCursor) {
        grabbingCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE);
    }
    cursors[PointerCursorShape::Grabbing] = grabbingCursor;

    isInitialized = true;
    LOG_INFO(InputStateMachine, "PointerIconManager initialized hardware system cursors.");
}

/**
 * @brief Releases all allocated SDL cursor resources.
 */
void PointerIconManager::Shutdown() {
    if (!isInitialized) return;

    for (auto& pair : cursors) {
        if (pair.second) {
            SDL_DestroyCursor(pair.second);
            pair.second = nullptr;
        }
    }
    cursors.clear();
    isInitialized = false;
}

/**
 * @brief Locks the cursor to a specific shape during modal dragging.
 *
 * @param shape Desired cursor shape.
 * @param reason The modal interaction taking ownership.
 */
void PointerIconManager::Lock(PointerCursorShape shape, CursorLockReason reason) noexcept {
    lockedShape = shape;
    currentLock = reason;
}

/**
 * @brief Unlocks the cursor when a modal interaction completes.
 *
 * @param reason The modal interaction releasing ownership.
 */
void PointerIconManager::Unlock(CursorLockReason reason) noexcept {
    if (currentLock == reason) {
        currentLock = CursorLockReason::None;
    }
}

/**
 * @brief Sets the transient hover cursor (e.g. hovering canvas, handles, or tools).
 *
 * @param shape Requested cursor shape.
 */
void PointerIconManager::SetHoverShape(PointerCursorShape shape) noexcept {
    if (currentLock == CursorLockReason::None) {
        hoverShape = shape;
    }
}

/**
 * @brief Maps a HandleRole to its corresponding PointerCursorShape.
 *
 * MATHEMATICAL MAPPING:
 * ---------------------
 *  TopLeft / BottomRight  -> Principal diagonal (\) -> ResizeNWSE
 *  TopRight / BottomLeft  -> Secondary diagonal (/) -> ResizeNESW
 *  TopCenter / BottomCenter -> Vertical axis (|)   -> ResizeNS
 *  LeftCenter / RightCenter -> Horizontal axis (-) -> ResizeEW
 *  Body                     -> 4-way translation  -> ResizeAll
 *  Rotation                 -> Orbit handle       -> Hand
 *
 * @param role Manipulator handle role.
 * @return PointerCursorShape Expected cursor shape.
 */
PointerCursorShape PointerIconManager::GetShapeForHandleRole(HandleRole role) const noexcept {
    switch (role) {
        case HandleRole::TopLeft:
        case HandleRole::BottomRight:
            return PointerCursorShape::ResizeNWSE;

        case HandleRole::TopRight:
        case HandleRole::BottomLeft:
            return PointerCursorShape::ResizeNESW;

        case HandleRole::TopCenter:
        case HandleRole::BottomCenter:
            return PointerCursorShape::ResizeNS;

        case HandleRole::LeftCenter:
        case HandleRole::RightCenter:
            return PointerCursorShape::ResizeEW;

        case HandleRole::Rotation:
            return PointerCursorShape::Hand;

        case HandleRole::Body:
            return PointerCursorShape::ResizeAll;

        case HandleRole::None:
        case HandleRole::Custom:
        default:
            return PointerCursorShape::Arrow;
    }
}

/**
 * @brief Helper to set hover cursor based on a gizmo HandleRole.
 *
 * @param role Manipulator handle role.
 */
void PointerIconManager::SetHoverForHandleRole(HandleRole role) noexcept {
    SetHoverShape(GetShapeForHandleRole(role));
}

/**
 * @brief Sets the active hardware cursor via SDL_SetCursor if changed.
 *
 * @param shape Target cursor shape.
 */
void PointerIconManager::SetSdlCursor(PointerCursorShape shape) {
    if (shape == PointerCursorShape::Hidden) {
        SDL_HideCursor();
        currentShape = shape;
        return;
    }

    SDL_ShowCursor();

    if (currentShape == shape) {
        return; // Avoid redundant OS system calls
    }

    auto it = cursors.find(shape);
    if (it != cursors.end() && it->second) {
        SDL_SetCursor(it->second);
    } else {
        auto fallback = cursors.find(PointerCursorShape::Arrow);
        if (fallback != cursors.end() && fallback->second) {
            SDL_SetCursor(fallback->second);
        }
    }
    currentShape = shape;
}

/**
 * @brief Synchronizes and commits the cursor shape to SDL and ImGui for the current frame.
 *
 * GENERAL WORKING PROCESS:
 * -------------------------
 * 1. Checks if a continuous drag holds an active cursor lock (e.g. gizmo resize, stroke inking).
 * 2. If locked, the locked shape takes absolute precedence, preventing hover-drop flicker.
 * 3. If unlocked and ImGui UI chrome has focus, reflects ImGui's requested cursor.
 * 4. If unlocked and pointer is over canvas, enforces the active tool/hover shape.
 *
 * @param isOverCanvas Whether pointer is within the active canvas area.
 * @param imguiHasFocus Whether ImGui UI chrome currently captures input.
 */
void PointerIconManager::Apply(bool isOverCanvas, bool imguiHasFocus) {
    if (!isInitialized) {
        Initialize();
    }

    PointerCursorShape targetShape = PointerCursorShape::Arrow;
    ImGuiMouseCursor targetImGui = ImGuiMouseCursor_Arrow;

    if (currentLock != CursorLockReason::None) {
        // Absolute priority: modal drag interaction lock
        targetShape = lockedShape;
    } else if (imguiHasFocus) {
        // ImGui UI chrome (ribbon, navigation panels, dialogs) has priority
        ImGuiMouseCursor imCursor = ImGui::GetMouseCursor();
        switch (imCursor) {
            case ImGuiMouseCursor_TextInput:  targetShape = PointerCursorShape::TextInput; break;
            case ImGuiMouseCursor_ResizeAll:  targetShape = PointerCursorShape::ResizeAll; break;
            case ImGuiMouseCursor_ResizeNS:   targetShape = PointerCursorShape::ResizeNS;  break;
            case ImGuiMouseCursor_ResizeEW:   targetShape = PointerCursorShape::ResizeEW;  break;
            case ImGuiMouseCursor_ResizeNESW: targetShape = PointerCursorShape::ResizeNESW; break;
            case ImGuiMouseCursor_ResizeNWSE: targetShape = PointerCursorShape::ResizeNWSE; break;
            case ImGuiMouseCursor_Hand:       targetShape = PointerCursorShape::Hand;       break;
            case ImGuiMouseCursor_NotAllowed: targetShape = PointerCursorShape::NotAllowed; break;
            case ImGuiMouseCursor_None:       targetShape = PointerCursorShape::Hidden;    break;
            default:                          targetShape = PointerCursorShape::Arrow;     break;
        }
    } else if (isOverCanvas) {
        // Cursor over canvas: enforce tool/hover shape
        targetShape = hoverShape;
    } else {
        targetShape = PointerCursorShape::Arrow;
    }

    // Map resolved shape to ImGuiMouseCursor to maintain ImGui backend alignment
    switch (targetShape) {
        case PointerCursorShape::TextInput:  targetImGui = ImGuiMouseCursor_TextInput; break;
        case PointerCursorShape::ResizeAll:  targetImGui = ImGuiMouseCursor_ResizeAll; break;
        case PointerCursorShape::ResizeNS:   targetImGui = ImGuiMouseCursor_ResizeNS;  break;
        case PointerCursorShape::ResizeEW:   targetImGui = ImGuiMouseCursor_ResizeEW;  break;
        case PointerCursorShape::ResizeNESW: targetImGui = ImGuiMouseCursor_ResizeNESW; break;
        case PointerCursorShape::ResizeNWSE: targetImGui = ImGuiMouseCursor_ResizeNWSE; break;
        case PointerCursorShape::Hand:
        case PointerCursorShape::Grabbing:   targetImGui = ImGuiMouseCursor_Hand;       break;
        case PointerCursorShape::NotAllowed: targetImGui = ImGuiMouseCursor_NotAllowed; break;
        case PointerCursorShape::Hidden:     targetImGui = ImGuiMouseCursor_None;       break;
        case PointerCursorShape::Crosshair:  targetImGui = ImGuiMouseCursor_Arrow;      break; // ImGui lacks native crosshair
        case PointerCursorShape::Arrow:
        default:                             targetImGui = ImGuiMouseCursor_Arrow;      break;
    }

    // Inform ImGui so it doesn't overwrite on its own render pass
    ImGui::SetMouseCursor(targetImGui);

    // Apply native hardware cursor directly via SDL
    SetSdlCursor(targetShape);
}

} // namespace FolioInput
