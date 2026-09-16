/**
 * @file special_action_manager.cpp
 * @brief Implementation of SpecialActionManager handling double-click evaluation,
 *        callback distribution, and standard canvas action execution.
 */

#include "input/stateMachine/special_action_manager.hpp"
#include "core/engine/canvas_engine.hpp"
#include "core/document/document_session.hpp"
#include "utils/logger.hpp"

namespace FolioInput {

bool SpecialActionManager::EvaluateDoubleClick(float x, float y, uint64_t nowMs, const InputConfiguration& config) {
    if (!lastTap.isValid) {
        // First tap: initialize history
        lastTap.x = x;
        lastTap.y = y;
        lastTap.timestampMs = nowMs;
        lastTap.isValid = true;
        return false;
    }

    // Compute temporal difference in milliseconds
    uint64_t dt = (nowMs >= lastTap.timestampMs) ? (nowMs - lastTap.timestampMs) : 0;

    // Compute spatial distance in screen pixels: D = sqrt((x2 - x1)^2 + (y2 - y1)^2)
    float dx = x - lastTap.x;
    float dy = y - lastTap.y;
    float dist = std::hypot(dx, dy);

    if (dt <= config.doubleTapMaxIntervalMs && dist <= config.doubleTapMaxDistancePx) {
        // Double-tap criteria satisfied! Reset history to prevent a triple-tap from firing again
        ResetClickHistory();
        return true;
    }

    // Did not satisfy double-tap criteria; replace previous tap with this new baseline
    lastTap.x = x;
    lastTap.y = y;
    lastTap.timestampMs = nowMs;
    lastTap.isValid = true;
    return false;
}

void SpecialActionManager::ResetClickHistory() noexcept {
    lastTap = TapRecord{};
}

void SpecialActionManager::RegisterActionCallback(ActionCallback callback) {
    if (callback) {
        callbacks.push_back(std::move(callback));
    }
}

void SpecialActionManager::TriggerAction(SpecialActionType action, float x, float y, CanvasEngine& canvas, DocumentSession& session) {
    if (action == SpecialActionType::None) return;

    LOG_INFO(InputStateMachine, "Special Action Triggered: " + ActionToString(action) +
                                " at (" + std::to_string(x) + ", " + std::to_string(y) + ")");

    // 1. Notify external subscribers (e.g. UI radial menu, ribbon bar, overlay)
    for (auto& cb : callbacks) {
        if (cb) {
            cb(action, x, y);
        }
    }

    // 2. Built-in default execution logic for core canvas actions
    switch (action) {
        case SpecialActionType::Undo: {
            LOG_INFO(InputStateMachine, "Executing Undo via Special Action");
            auto activePage = session.GetActivePage();
            if (activePage && !activePage->objects.empty()) {
                activePage->objects.pop_back();
                canvas.SyncSelectionToSpatialIndex(&session);
                canvas.needsFullRebake = true;
                canvas.isDirty = true;
            }
            break;
        }

        case SpecialActionType::Redo: {
            LOG_INFO(InputStateMachine, "Executing Redo via Special Action (reserved)");
            // Reserved for future CommandManager redo stack integration
            break;
        }

        case SpecialActionType::ClearSelection: {
            canvas.ClearSelection(&session);
            canvas.needsFullRebake = true;
            canvas.isDirty = true;
            break;
        }

        case SpecialActionType::SelectAtPoint: {
            // Hit test canvas objects at local position (x, y)
            Point2D worldMm = canvas.transform.ScreenToWorld(x, y);
            auto activePage = session.GetActivePage();
            std::shared_ptr<CanvasObject> clickedObj = nullptr;
            if (activePage) {
                for (auto it = activePage->objects.rbegin(); it != activePage->objects.rend(); ++it) {
                    auto& obj = *it;
                    if (obj && obj->isVisible && obj->isSelectable &&
                        (obj->HitTest(worldMm.x, worldMm.y) || obj->HitTestCircle(worldMm.x, worldMm.y, 2.0))) {
                        clickedObj = obj;
                        break;
                    }
                }
            }

            canvas.ClearSelection(&session);
            if (clickedObj) {
                clickedObj->isSelected = 1;
                canvas.selectionGizmo.SetSelectedObjects(activePage->objects);
                canvas.selectionGizmo.OnPointerDown(x, y, canvas.transform);
                LOG_INFO(InputStateMachine, "Direct selection picked object uid=" + std::to_string(clickedObj->uid));
            }
            canvas.needsFullRebake = true;
            canvas.isDirty = true;
            break;
        }

        case SpecialActionType::ToggleEraser:
        case SpecialActionType::OpenContextMenu:
        case SpecialActionType::ZoomToFit:
        default:
            // Handled by external callback or state machine caller
            break;
    }
}

std::string SpecialActionManager::ActionToString(SpecialActionType action) {
    switch (action) {
        case SpecialActionType::None:            return "None";
        case SpecialActionType::Undo:            return "Undo";
        case SpecialActionType::Redo:            return "Redo";
        case SpecialActionType::ToggleEraser:    return "ToggleEraser";
        case SpecialActionType::SelectAtPoint:   return "SelectAtPoint";
        case SpecialActionType::OpenContextMenu: return "OpenContextMenu";
        case SpecialActionType::ClearSelection:  return "ClearSelection";
        case SpecialActionType::ZoomToFit:       return "ZoomToFit";
        default:                                 return "Unknown";
    }
}

} // namespace FolioInput
