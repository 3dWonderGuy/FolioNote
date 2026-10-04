/**
 * =========================================================================================
 * @file core/objects/object_action_registry.cpp
 * @brief Implementation of Standard Canvas Object Actions Registry and Menu Builder
 * =========================================================================================
 *
 * ARCHITECTURAL DESIGN & INVERSION OF CONTROL:
 * --------------------------------------------
 * ObjectActionRegistry centralizes universal canvas object operations (deletion, z-ordering,
 * clipboard interactions, duplication, and specialized media controls) within the core
 * object hierarchy.
 *
 * Moving this implementation out of the header isolates heavy dependencies:
 * - CanvasEngine (viewport rebaking, camera, selection gizmo)
 * - DocumentSession (undo/redo command history, selection management)
 * - CanvasPage (z-ordering, object lifetime, dirty notifications)
 * - ClipboardManager (cross-subsystem copy/cut operations)
 * - Specialized container types (AttachmentObject, ImageObject, VideoObject)
 * - Platform subsystem APIs (<SDL3/SDL.h> for native external URL dispatch)
 *
 * Preventing these concrete subsystem headers from leaking into `object_action_registry.hpp`
 * breaks compilation cascades across the UI and state machine subsystems.
 */

#include "core/objects/object_action_registry.hpp"

#include <algorithm>

#include "core/objects/canvas_object.hpp"
#include "core/objects/attachment_container/attachment_container.hpp"
#include "core/objects/media/images/image_container.hpp"
#include "core/document/canvas_page.hpp"
#include "core/document/document_session.hpp"
#include "core/engine/canvas_engine.hpp"
#include "core/history/canvas_command.hpp"
#include "core/clipboard/clipboard_manager.hpp"

namespace Folio {

/**
 * @brief Constructs a sorted sequence of interactive context actions for a canvas object.
 *
 * MATHEMATICAL & PIPELINE BEHAVIOR:
 * ---------------------------------
 * 1. Universal Actions Setup:
 *    - Delete (order: 0, destructive):
 *        Removes object via `page->RemoveObjectByUid(targetObj->uid)`, pushes an atomic
 *        `RemoveObjectsCommand` onto the undo/redo stack, and flags `needsFullRebake = true`.
 *    - Layering / Z-Ordering:
 *        - Bring to Front (order: 200): Shifts object to index `N - 1`.
 *        - Bring Forward (order: 210): Increments z-index by swapping with index `i + 1`.
 *        - Send Backward (order: 220): Decrements z-index by swapping with index `i - 1`.
 *        - Send to Back (order: 230): Shifts object to background plane (index 0).
 *    - Clipboard (Copy order: 280, Cut order: 290):
 *        Dispatches active selection (or target object fallback) to `ClipboardManager::Instance()`.
 *    - Duplication (order: 300):
 *        Calls `session.DuplicateSelection(10.0)`, translating duplicates by vector `[+10mm, +10mm]^T`.
 *
 * 2. Specialized Container Callbacks:
 *    - AttachmentObject: Injects undoable `RelinkAttachmentCommand` on relinking events.
 *    - ImageObject: Injects visual state synchronization (gizmo bounds recalculation + dirty flags).
 *    - VideoObject: Populates transport controls (Play/Pause, Stop, Mute, Loop, Reset Size, Native Player).
 *
 * 3. Inversion-of-Control Hook:
 *    Calls `obj->CustomizeActions(actions)` allowing subclasses to inject custom entries.
 *
 * 4. Deterministic Ordering:
 *    Applies `std::stable_sort` over `ContextMenuItem::order` ascending.
 *
 * @param[in] obj Target canvas object selected or right-clicked.
 * @param[in] page Active canvas page hosting the object.
 * @param[in,out] engine Engine holding viewport state, camera, and selection gizmo.
 * @param[in,out] session Document session managing transaction history and dirty state.
 * @return std::vector<ContextMenuItem> Ordered, interactive action list.
 */
std::vector<ContextMenuItem> ObjectActionRegistry::BuildActionsForObject(
    const std::shared_ptr<CanvasObject>& obj,
    std::shared_ptr<CanvasPage> page,
    CanvasEngine& engine,
    DocumentSession& session
) {
    if (!obj || !page) return {};

    std::vector<ContextMenuItem> actions;
    actions.reserve(16);

    // =========================================================================
    // 1. UNIVERSAL DEFAULT ACTIONS
    // =========================================================================

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

    // Action: Set as Background / Unlock from Background (order: 190)
    // Background elements are marked non-selectable (isSelectable = 0) and sent to back,
    // so pen/lasso operations draw over them smoothly without selecting or moving them.
    {
        ContextMenuItem bgAct;
        if (obj->isSelectable) {
            bgAct.label = "Set as Background";
            bgAct.icon = "📌";
            bgAct.iconKey = "pin_background";
            bgAct.order = 190;
            bgAct.isSeparatorBefore = true;
            bgAct.onTrigger = [obj, page, &engine, &session]() {
                obj->isSelectable = 0;
                obj->isSelected = 0;
                page->SendToBack(obj->uid);
                engine.selectionGizmo.ClearSelection();
                engine.isDirty = true;
                engine.needsFullRebake = true;
                page->isModified = true;
                session.NotifyPageModified(page);
            };
        } else {
            bgAct.label = "Unlock from Background";
            bgAct.icon = "🔓";
            bgAct.iconKey = "unpin_background";
            bgAct.order = 190;
            bgAct.isSeparatorBefore = true;
            bgAct.onTrigger = [obj, page, &engine, &session]() {
                obj->isSelectable = 1;
                obj->isSelected = 1;
                engine.selectionGizmo.SetSelectedObjects({obj});
                engine.isDirty = true;
                engine.needsFullRebake = true;
                page->isModified = true;
                session.NotifyPageModified(page);
            };
        }
        actions.push_back(std::move(bgAct));
    }

    // Action: Copy (order: 280)
    {
        ContextMenuItem act;
        act.label = "Copy";
        act.shortcut = "Ctrl+C";
        act.icon = "📋";
        act.iconKey = "copy";
        act.order = 280;
        act.isSeparatorBefore = true;
        act.onTrigger = [obj, &session]() {
            auto selected = session.GetSelectedObjects();
            if (selected.empty()) {
                selected = { obj };
            }
            ClipboardManager::Instance().CopyObjects(selected);
        };
        actions.push_back(std::move(act));
    }

    // Action: Cut (order: 290)
    {
        ContextMenuItem act;
        act.label = "Cut";
        act.shortcut = "Ctrl+X";
        act.icon = "✂";
        act.iconKey = "cut";
        act.order = 290;
        act.onTrigger = [obj, &session, &engine]() {
            auto selected = session.GetSelectedObjects();
            if (selected.empty()) {
                selected = { obj };
            }
            ClipboardManager::Instance().CutObjects(selected, session);
            engine.selectionGizmo.ClearSelection();
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

    if (obj->type == ObjectType::Image) {
        auto img = std::static_pointer_cast<ImageObject>(obj);
        img->onVisualStateChanged = [img, page, &session, &engine]() {
            page->UpdateObject(img);
            if (img->isSelected) {
                engine.selectionGizmo.RecalculateBounds();
            }
            engine.isDirty = true;
            engine.needsFullRebake = true;
            session.NotifyPageModified(page);
        };
    }

    // =========================================================================
    // 2. VIRTUAL HOOK: Allow object subclass to customize domain-specific actions
    // =========================================================================
    obj->CustomizeActions(actions);

    // =========================================================================
    // 3. DETERMINISTIC SORTING
    // =========================================================================
    std::stable_sort(actions.begin(), actions.end(), [](const ContextMenuItem& a, const ContextMenuItem& b) {
        return a.order < b.order;
    });

    return actions;
}

} // namespace Folio
