#pragma once
/**
 * =========================================================================================
 * @file core/objects/object_action_registry.hpp
 * @brief Registry Defining Standard Canvas Object Actions, Stacking Pipeline, and Operations
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & INVERSION OF CONTROL:
 * --------------------------------------------
 * ObjectActionRegistry centralizes standard canvas object operations within the `core/objects`
 * domain, decoupling raw object geometry models from engine and session mutation state.
 *
 * 1. Default Universal Actions:
 *    The registry provides standard commands applicable to all canvas objects:
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
#include <algorithm>

#include "app/context_menu_item.hpp"
#include "core/objects/canvas_object.hpp"
#include "core/objects/attachment_container.hpp"
#include "core/document/canvas_page.hpp"
#include "core/document/document_session.hpp"
#include "core/engine/canvas_engine.hpp"
#include "core/history/canvas_command.hpp"

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
    ) {
        if (!obj || !page) return {};

        std::vector<ContextMenuItem> actions;
        actions.reserve(12);

        // =====================================================================
        // 1. UNIVERSAL DEFAULT ACTIONS
        // =====================================================================

        // Action: Delete (Primary domain action, order: 0)
        {
            ContextMenuItem act;
            act.label = "Delete";
            act.shortcut = "Del";
            act.icon = "🗑";
            act.iconKey = "trash";
            act.order = 0;
            act.isSeparatorBefore = false;
            act.isDestructive = true;
            act.onTrigger = [page, &session, &engine, targetObj = obj]() {
                page->RemoveObjectByUid(targetObj->uid);
                session.RecordHistoryCommand(page, std::make_unique<Folio::RemoveObjectsCommand>(targetObj));
                engine.selectionGizmo.ClearSelection();
                engine.isDirty = true;
                engine.needsFullRebake = true;
            };
            actions.push_back(std::move(act));
        }

        // Action: Bring to Front (order: 200)
        {
            ContextMenuItem act;
            act.label = "Bring to Front";
            act.shortcut = "Ctrl+Shift+]";
            act.icon = "⬆";
            act.iconKey = "bring_to_front";
            act.order = 200;
            act.isSeparatorBefore = true;
            act.onTrigger = [page, &engine, uid = obj->uid]() {
                page->BringToFront(uid);
                engine.isDirty = true;
                engine.needsFullRebake = true;
            };
            actions.push_back(std::move(act));
        }

        // Action: Bring Forward (Single-step z-order increment, order: 210)
        {
            ContextMenuItem act;
            act.label = "Bring Forward";
            act.shortcut = "Ctrl+]";
            act.icon = "⇡";
            act.iconKey = "bring_forward";
            act.order = 210;
            act.onTrigger = [page, &engine, uid = obj->uid]() {
                page->ShiftFront(uid);
                engine.isDirty = true;
                engine.needsFullRebake = true;
            };
            actions.push_back(std::move(act));
        }

        // Action: Send Backward (Single-step z-order decrement, order: 220)
        {
            ContextMenuItem act;
            act.label = "Send Backward";
            act.shortcut = "Ctrl+[";
            act.icon = "⇣";
            act.iconKey = "send_backward";
            act.order = 220;
            act.onTrigger = [page, &engine, uid = obj->uid]() {
                page->ShiftBack(uid);
                engine.isDirty = true;
                engine.needsFullRebake = true;
            };
            actions.push_back(std::move(act));
        }

        // Action: Send to Back (order: 230)
        {
            ContextMenuItem act;
            act.label = "Send to Back";
            act.shortcut = "Ctrl+Shift+[";
            act.icon = "⬇";
            act.iconKey = "send_to_back";
            act.order = 230;
            act.onTrigger = [page, &engine, uid = obj->uid]() {
                page->SendToBack(uid);
                engine.isDirty = true;
                engine.needsFullRebake = true;
            };
            actions.push_back(std::move(act));
        }

        // Action: Duplicate (order: 300)
        {
            ContextMenuItem act;
            act.label = "Duplicate";
            act.shortcut = "Ctrl+D";
            act.icon = "📄";
            act.iconKey = "duplicate";
            act.order = 300;
            act.isSeparatorBefore = true;
            act.onTrigger = [&session, &engine]() {
                session.DuplicateSelection(10.0);
                engine.isDirty = true;
                engine.needsFullRebake = true;
            };
            actions.push_back(std::move(act));
        }

        // Attach undo/redo and dirty hooks for specialized objects
        if (obj->type == ObjectType::AttachmentFile) {
            auto attach = std::static_pointer_cast<AttachmentObject>(obj);
            attach->onRelinkCallback = [page, &session, &engine, uid = attach->uid](
                const std::string& oldPath, const std::string& newPath,
                const std::string& oldName, const std::string& newName
            ) {
                session.RecordHistoryCommand(page, std::make_unique<Folio::RelinkAttachmentCommand>(
                    uid, oldPath, newPath, oldName, newName
                ));
                page->isModified = true;
                engine.isDirty = true;
                engine.needsFullRebake = true;
            };
        }

        // =====================================================================
        // 2. VIRTUAL HOOK: Allow object subclass to customize actions
        // =====================================================================
        obj->CustomizeActions(actions);

        // =====================================================================
        // 3. DETERMINISTIC SORTING
        // =====================================================================
        std::stable_sort(actions.begin(), actions.end(), [](const ContextMenuItem& a, const ContextMenuItem& b) {
            return a.order < b.order;
        });

        return actions;
    }
};

} // namespace Folio
