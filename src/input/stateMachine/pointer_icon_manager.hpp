/**
 * =========================================================================================
 * @file input/stateMachine/pointer_icon_manager.hpp
 * @brief Centralized Hardware & Canvas Pointer Cursor Management and Anti-Flicker Controller
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & PURPOSE:
 * -------------------------------
 * In desktop graphics engines (e.g., Photoshop, Illustrator, Figma, OneNote), cursor
 * management requires strict statefulness and input capture:
 *
 * 1. Hardware Cursor Authority:
 *    Directly manages SDL3 hardware cursors (SDL_Cursor*) rather than relying exclusively
 *    on Dear ImGui's internal cursor stack. Dear ImGui resets cursor state at the start
 *    of each frame, which causes rapid alternating flicker if mouse-move events do not
 *    fire on every frame.
 *
 * 2. Drag & Interaction Locking (Anti-Flicker Mechanism):
 *    When resizing a gizmo handle, inking a stroke, or panning the viewport, the cursor
 *    is locked to a specific shape until the pointer is released (PointerUp). Transient
 *    hover tests and boundary crossings cannot override or flicker the cursor.
 *
 * 3. Handle Role Mapping:
 *    Polymorphically maps 8-point gizmo handles (TopLeft, RightCenter, Body, etc.) to
 *    appropriate bi-directional resize, move, or rotate hardware cursors.
 *
 * 4. Tool & Inking Reticles:
 *    Switches effortlessly between Precision Crosshair (Drawing/Shapes), Text I-Beam,
 *    Open/Grabbing Hand (Panning), and Hidden (Eraser with custom canvas reticle).
 */

#pragma once

#include <SDL3/SDL.h>
#include <cstdint>
#include <unordered_map>
#include "core/engine/gizmo_types.hpp"

namespace FolioInput {

/**
 * @enum PointerCursorShape
 * @brief Strongly typed semantic cursor shapes supported by the canvas engine.
 */
enum class PointerCursorShape : uint8_t {
    Arrow = 0,         ///< Standard desktop pointer arrow
    Crosshair,         ///< Precision crosshair for inking, pen, and shape creation
    TextInput,         ///< Text I-beam for typing and text box editing
    ResizeAll,         ///< 4-way move cross (dragging selection body)
    ResizeNS,          ///< Vertical bi-directional resize (TopCenter, BottomCenter)
    ResizeEW,          ///< Horizontal bi-directional resize (LeftCenter, RightCenter)
    ResizeNESW,        ///< Diagonal bi-directional resize (TopRight, BottomLeft)
    ResizeNWSE,        ///< Diagonal bi-directional resize (TopLeft, BottomRight)
    Hand,              ///< Open hand (panning mode hover, clickable link)
    Grabbing,          ///< Closed grasping hand (active viewport pan drag)
    NotAllowed,        ///< Prohibited action
    Hidden             ///< Hardware cursor hidden (custom reticles, e.g. Eraser)
};

/**
 * @enum CursorLockReason
 * @brief Identifies which active continuous interaction holds exclusive cursor lock.
 */
enum class CursorLockReason : uint8_t {
    None = 0,
    GizmoResize,     ///< Actively dragging a scale handle
    GizmoMove,       ///< Actively dragging gizmo body (moving selection)
    GizmoRotate,     ///< Actively rotating selection
    Inking,          ///< Drawing an active ink stroke
    ShapeDrawing,    ///< Drag-creating a geometric shape
    BoxSelecting,    ///< Marquee selection or lasso drag
    Panning,         ///< Viewport pan dragging
    Eraser           ///< Active eraser stroke
};

/**
 * @class PointerIconManager
 * @brief Thread-safe/frame-deterministic pointer icon and cursor lock coordinator.
 */
class PointerIconManager {
public:
    PointerIconManager();
    ~PointerIconManager();

    // Prevent copying to protect raw SDL_Cursor* resource lifetimes
    PointerIconManager(const PointerIconManager&) = delete;
    PointerIconManager& operator=(const PointerIconManager&) = delete;

    /**
     * @brief Pre-allocates and caches all SDL system cursors.
     */
    void Initialize();

    /**
     * @brief Releases all allocated SDL cursor resources.
     */
    void Shutdown();

    /**
     * @brief Locks the cursor to a specific shape during modal dragging.
     *
     * While locked, transient hover updates are rejected to eliminate flicker.
     *
     * @param shape Desired cursor shape.
     * @param reason The modal interaction taking ownership.
     */
    void Lock(PointerCursorShape shape, CursorLockReason reason) noexcept;

    /**
     * @brief Unlocks the cursor when a modal interaction completes.
     *
     * @param reason The modal interaction releasing ownership.
     */
    void Unlock(CursorLockReason reason) noexcept;

    /**
     * @brief Checks if a cursor lock is currently active.
     */
    [[nodiscard]] bool IsLocked() const noexcept { return currentLock != CursorLockReason::None; }

    /**
     * @brief Returns the reason for the current cursor lock.
     */
    [[nodiscard]] CursorLockReason GetCurrentLockReason() const noexcept { return currentLock; }

    /**
     * @brief Sets the transient hover cursor (e.g. hovering canvas, handles, or tools).
     *
     * Ignored if a cursor lock is currently active.
     *
     * @param shape Requested cursor shape.
     */
    void SetHoverShape(PointerCursorShape shape) noexcept;

    /**
     * @brief Helper to set hover cursor based on a gizmo HandleRole.
     *
     * @param role Manipulator handle role.
     */
    void SetHoverForHandleRole(HandleRole role) noexcept;

    /**
     * @brief Maps a HandleRole to its corresponding PointerCursorShape.
     *
     * @param role Manipulator handle role.
     * @return PointerCursorShape Expected cursor shape.
     */
    [[nodiscard]] PointerCursorShape GetShapeForHandleRole(HandleRole role) const noexcept;

    /**
     * @brief Synchronizes and commits the cursor shape to SDL and ImGui for the current frame.
     *
     * GENERAL WORKING PROCESS:
     * -------------------------
     * 1. If ImGui UI chrome has focus and cursor is NOT locked by a canvas drag, delegates to
     *    ImGui's requested cursor.
     * 2. If the cursor is locked (e.g. gizmo drag, inking), enforces the locked shape regardless
     *    of mouse position.
     * 3. If over the canvas, enforces the active tool / hover shape.
     * 4. Updates SDL_SetCursor only when the shape has actually changed to prevent OS IPC overhead.
     *
     * @param isOverCanvas Whether pointer is within the active canvas area.
     * @param imguiHasFocus Whether ImGui UI chrome currently captures input.
     */
    void Apply(bool isOverCanvas, bool imguiHasFocus);

    /**
     * @brief Returns the currently active cursor shape.
     */
    [[nodiscard]] PointerCursorShape GetCurrentShape() const noexcept { return currentShape; }

private:
    void SetSdlCursor(PointerCursorShape shape);

    PointerCursorShape currentShape = PointerCursorShape::Arrow;
    PointerCursorShape hoverShape   = PointerCursorShape::Arrow;
    PointerCursorShape lockedShape  = PointerCursorShape::Arrow;
    CursorLockReason currentLock    = CursorLockReason::None;

    std::unordered_map<PointerCursorShape, SDL_Cursor*> cursors;
    bool isInitialized = false;
};

} // namespace FolioInput
