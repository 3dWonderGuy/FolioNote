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
#include "core/objects/media/videos/video_container.hpp"
#include "core/clipboard/clipboard_manager.hpp"

#include <SDL3/SDL.h>  // for SDL_OpenURL in "Open in Native Player" action

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

        // Action: Set as Background / Unlock from Background (order: 190)
        // Background elements are marked non-selectable (isSelectable = 0) and sent to back,
        // so pen/lasso operations draw over them smoothly without selecting or moving them.
        if (obj->type == ObjectType::Image) {
            ContextMenuItem bgAct;
            if (obj->isSelectable) {
                bgAct.label = "Set as Background";
                bgAct.icon = "🖼";
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

        // =====================================================================
        // VIDEO OBJECT CONTEXT MENU ACTIONS
        // =====================================================================
        // All video transport actions are gated on ObjectType::Video.
        // The background/unlock action is also provided for video objects (same
        // semantics as for images: lock to background so drawing goes over it).
        // =====================================================================
        if (obj->type == ObjectType::Video) {
            auto vid = std::static_pointer_cast<Folio::VideoObject>(obj);

            // ── Action: Play / Pause toggle ───────────────────────────────────
            {
                ContextMenuItem act;
                act.label = vid->isPlaying ? "Pause" : "Play";
                act.icon  = vid->isPlaying ? "⏸" : "▶";
                act.iconKey = vid->isPlaying ? "pause" : "play";
                act.order = 100;
                act.isSeparatorBefore = true;
                act.onTrigger = [vid, &engine]() {
                    if (vid->isPlaying) {
                        vid->Pause();
                    } else {
                        vid->Play([&engine]() {
                            engine.needsFullRebake = true;
                            engine.isDirty = true;
                        });
                    }
                    engine.needsFullRebake = true;
                    engine.isDirty = true;
                };
                actions.push_back(std::move(act));
            }

            // ── Action: Stop ─────────────────────────────────────────────────
            {
                ContextMenuItem act;
                act.label   = "Stop";
                act.icon    = "⏹";
                act.iconKey = "stop";
                act.order   = 110;
                act.onTrigger = [vid, &engine]() {
                    vid->Stop();
                    engine.isDirty = true;
                };
                actions.push_back(std::move(act));
            }

            // ── Action: Mute / Unmute ────────────────────────────────────────
            {
                ContextMenuItem act;
                act.label   = vid->isMuted ? "Unmute" : "Mute";
                act.icon    = vid->isMuted ? "🔊" : "🔇";
                act.iconKey = vid->isMuted ? "unmute" : "mute";
                act.order   = 120;
                act.onTrigger = [vid, &engine]() {
                    vid->ToggleMute();
                    engine.isDirty = true;
                };
                actions.push_back(std::move(act));
            }

            // ── Action: Loop toggle ──────────────────────────────────────────
            {
                ContextMenuItem act;
                act.label   = vid->isLooping ? "Disable Loop" : "Enable Loop";
                act.icon    = "🔁";
                act.iconKey = "loop";
                act.order   = 130;
                act.onTrigger = [vid, &engine]() {
                    vid->isLooping = !vid->isLooping;
                    engine.isDirty = true;
                };
                actions.push_back(std::move(act));
            }

            // ── Action: Reset to Native Size ─────────────────────────────────
            // Restores video to its natural decoded pixel dimensions at current DPI.
            // Math: worldW = nativeVideoW / pixelsPerMm
            {
                ContextMenuItem act;
                act.label   = "Reset to Native Size";
                act.icon    = "⤢";
                act.iconKey = "reset_size";
                act.order   = 140;
                act.isSeparatorBefore = true;
                act.onTrigger = [vid, page, &engine, &session]() {
                    vid->ResetToNativeSize(engine.transform.pixelsPerMm);
                    page->UpdateObject(vid);
                    if (vid->isSelected) engine.selectionGizmo.RecalculateBounds();
                    engine.isDirty = true;
                    engine.needsFullRebake = true;
                    session.NotifyPageModified(page);
                };
                actions.push_back(std::move(act));
            }

            // ── Action: Open in Native Player (system default video player) ──
            {
                ContextMenuItem act;
                act.label   = "Open in Native Player";
                act.icon    = "🎬";
                act.iconKey = "open_external";
                act.order   = 150;
                act.onTrigger = [vid]() {
                    if (!vid->sourceUrl.empty()) {
                        // SDL_OpenURL handles both file:// paths and http:// URLs
                        SDL_OpenURL(vid->sourceUrl.c_str());
                    }
                };
                actions.push_back(std::move(act));
            }

            // ── Action: Set as Background / Unlock from Background ───────────
            // Same semantics as images: non-selectable + sent to back = inking background layer
            {
                ContextMenuItem bgAct;
                if (vid->isSelectable) {
                    bgAct.label   = "Set as Background";
                    bgAct.icon    = "📌";
                    bgAct.iconKey = "pin_background";
                    bgAct.order   = 190;
                    bgAct.isSeparatorBefore = true;
                    bgAct.onTrigger = [vid, page, &engine, &session]() {
                        vid->isSelectable = 0;
                        vid->isSelected   = 0;
                        page->SendToBack(vid->uid);
                        engine.selectionGizmo.ClearSelection();
                        engine.isDirty = true;
                        engine.needsFullRebake = true;
                        page->isModified = true;
                        session.NotifyPageModified(page);
                    };
                } else {
                    bgAct.label   = "Unlock from Background";
                    bgAct.icon    = "🔓";
                    bgAct.iconKey = "unpin_background";
                    bgAct.order   = 190;
                    bgAct.isSeparatorBefore = true;
                    bgAct.onTrigger = [vid, page, &engine, &session]() {
                        vid->isSelectable = 1;
                        vid->isSelected   = 1;
                        engine.selectionGizmo.SetSelectedObjects({vid});
                        engine.isDirty = true;
                        engine.needsFullRebake = true;
                        page->isModified = true;
                        session.NotifyPageModified(page);
                    };
                }
                actions.push_back(std::move(bgAct));
            }
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
