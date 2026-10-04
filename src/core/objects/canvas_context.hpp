#pragma once
/**
 * =========================================================================================
 * @file core/objects/canvas_context.hpp
 * @brief Unified Environment Dependency Injection Context for Canvas Objects
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & PURPOSE:
 * -------------------------------
 * Solves deep subsystem coupling and cascading recompilation bottlenecks across the canvas hierarchy.
 *
 * 1. Zero Downcasting & Polymorphic Interaction:
 *    Callers operating on `std::shared_ptr<CanvasObject>` or `CanvasObject*` must never call
 *    `std::dynamic_pointer_cast`. All input interactions (clicks, double clicks, activation)
 *    dispatch polymorphically via virtual interaction methods like `OnPointerClick(const CanvasContext&)`.
 *
 * 2. Dependency Inversion & Subsystem Access:
 *    Instead of individual object headers directly including heavy application subsystems
 *    (`file_manager.hpp`, `canvas_engine.hpp`, `document_session.hpp`, `clipboard_manager.hpp`,
 *    `settings_manager.hpp`), this context forward-declares and wraps references/pointers to these
 *    core services.
 *
 * 3. Settings Manager Integration:
 *    Allows canvas objects to read application configuration and persist user preferences
 *    (e.g., default container dimensions, grid snapping, tool presets, media playback volume)
 *    through `ctx.settingsManager` without hard header dependencies.
 *
 * 4. Spatial & Modifier Telemetry:
 *    Carries the exact physical world coordinates (in millimeters), click cadence (single vs.
 *    double click), and active keyboard/input modifiers (Ctrl, Shift, Alt).
 */

#include <cstdint>

// Forward declarations of core application subsystems (zero upward header dependencies)
class CanvasEngine;
class DocumentSession;
class InputStateMachine;
class SettingsManager;

namespace Folio {

class FileManager;
class ClipboardManager;
class ContextMenuManager;

// Interface abstraction aliases
using IFileManager        = FileManager;
using IClipboard          = ClipboardManager;
using IContextMenuManager = ContextMenuManager;
using IInputStateMachine  = ::InputStateMachine;
using ISettingsManager    = ::SettingsManager;

/**
 * @struct CanvasContext
 * @brief Dependency injection context passed into polymorphic CanvasObject interaction hooks.
 */
struct CanvasContext {
    // Core Engine & Session References (Always Present)
    IInputStateMachine&  stateMachine;  ///< Active input device arbitration and tool state
    DocumentSession&     session;       ///< Active document session, page graph, and undo history
    CanvasEngine&        engine;        ///< Canvas viewport, render pipeline, and dirty tracking

    // Optional Subsystems (Pointers avoid allocating dummy singletons on the stack)
    IFileManager*        fileManager     = nullptr;  ///< Filesystem operations (or nullptr if unavailable)
    IClipboard*          clipboard       = nullptr;  ///< System clipboard synchronization (or nullptr)
    IContextMenuManager* contextMenu     = nullptr;  ///< Floating context menu dispatch (or nullptr)
    ISettingsManager*    settingsManager = nullptr;  ///< Application configuration and user preferences (or nullptr)

    // Interaction Telemetry
    double   worldX        = 0.0;       ///< Pointer X position in physical canvas millimeters (mm)
    double   worldY        = 0.0;       ///< Pointer Y position in physical canvas millimeters (mm)
    bool     isDoubleClick = false;     ///< True if this event represents a double-click gesture
    uint32_t modifiers     = 0;         ///< Keymod bitmask (Shift, Ctrl, Alt, Meta flags)
};

} // namespace Folio

// Global namespace alias for seamless access
using CanvasContext = Folio::CanvasContext;