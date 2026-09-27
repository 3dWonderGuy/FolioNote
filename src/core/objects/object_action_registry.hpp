#pragma once
/**
 * =========================================================================================
 * @file core/objects/object_action_registry.hpp
 * @brief Registry Interface for Standard Canvas Object Actions and Stacking Operations
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & INVERSION OF CONTROL:
 * --------------------------------------------
 * ObjectActionRegistry centralizes universal canvas object operations within the `core/objects`
 * domain, decoupling raw object geometry models from engine and session mutation state.
 *
 * Concrete implementation details, heavy upward subsystem dependencies (CanvasEngine,
 * DocumentSession, CanvasPage, VideoObject, ImageObject, ClipboardManager), and platform
 * headers are isolated inside `object_action_registry.cpp` to prevent compilation cascade
 * bottlenecks across the engine and UI layers.
 *
 * 1. Default Universal Actions:
 *    The registry provides standard commands applicable to canvas objects:
 *      - Delete (order: 0, destructive): Removes object from page, records history, clears selection.
 *      - Bring to Front (order: 200): Shifts object to the highest rendering index.
 *      - Bring Forward (order: 210): Swaps object with immediate forward neighbor.
 *      - Send Backward (order: 220): Swaps object with immediate backward neighbor.
 *      - Send to Back (order: 230): Shifts object to index 0 (background plane).
 *      - Duplicate (order: 300): Clones active selection with spatial offset (+10mm, +10mm).
 *
 * 2. Virtual Hook Customization:
 *    CanvasObject exposes a lightweight virtual hook: `CustomizeActions(actions)`.
 *    Subclasses (e.g. AttachmentObject, TextBoxObject, Shapes) override this hook to
 *    inject domain-specific commands (e.g. "Open Attachment", "Locate / Re-link File")
 *    or modify default actions before display.
 *
 * 3. Sorting & Deterministic Layout:
 *    Actions are stably sorted by `ContextMenuItem::order` weight (ascending), ensuring
 *    predictable menu layouts across arbitrary object hierarchies.
 */

#include <vector>
#include <memory>

#include "app/context_menu_item.hpp"

// Forward declarations of core engine and document types
class CanvasObject;
class CanvasPage;
class CanvasEngine;
class DocumentSession;

namespace Folio {

/**
 * @class ObjectActionRegistry
 * @brief Factory and registry building fully populated, interactive action items for canvas objects.
 */
class ObjectActionRegistry {
public:
    /**
     * @brief Generates and returns the complete, ordered list of actions for a given canvas object.
     *
     * MATHEMATICAL & WORKING PROCESS:
     * 1. Constructs standard universal actions with explicit sorting weights:
     *    - Delete:
     *        `page->RemoveObjectByUid(targetObj->uid)`
     *        Records atomic `RemoveObjectsCommand` into session history for undo/redo.
     *        Clears selection gizmo and marks viewport dirty (`needsFullRebake = true`).
     *    - Stacking & Layering (Z-Index):
     *        - Bring to Front: Moves target element to index `N - 1` where `N = objects.size()`.
     *        - Bring Forward (`ShiftFront`): If index `i < N - 1`, swaps element at `i` with `i + 1`.
     *        - Send Backward (`ShiftBack`): If index `i > 0`, swaps element at `i` with `i - 1`.
     *        - Send to Back: Moves target element to index `0`.
     *    - Duplicate:
     *        Invokes `session.DuplicateSelection(offsetMm = 10.0)` which applies translation
     *        matrix `T = [1, 0, +10; 0, 1, +10]` to all selected objects.
     * 2. Invocates the virtual hook `obj->CustomizeActions(actions)` allowing the object
     *    subclass to inject domain-specific commands or adjust default properties.
     * 3. Executes `std::stable_sort` on `order` to ensure deterministic visual layout.
     *
     * @param[in] obj Target CanvasObject clicked or selected.
     * @param[in] page Active CanvasPage hosting the object.
     * @param[in,out] engine CanvasEngine managing camera, selection gizmo, and viewport rebaking.
     * @param[in,out] session DocumentSession managing undo/redo history and document operations.
     * @return std::vector<ContextMenuItem> Ready-to-render, sorted list of interactive menu actions.
     */
    static std::vector<ContextMenuItem> BuildActionsForObject(
        const std::shared_ptr<CanvasObject>& obj,
        std::shared_ptr<CanvasPage> page,
        CanvasEngine& engine,
        DocumentSession& session
    );
};

} // namespace Folio
