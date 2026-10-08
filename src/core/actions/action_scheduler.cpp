/**
 * =========================================================================================
 * @file core/actions/action_scheduler.cpp
 * @brief Implementation of high-performance Action Scheduler
 * =========================================================================================
 */

#include "core/actions/action_scheduler.hpp"
#include <algorithm>

namespace Folio {

ActionScheduler::ActionScheduler(size_t reserveCapacity) {
    activeActions.reserve(reserveCapacity);
}

ActionScheduler::~ActionScheduler() {
    // If scheduler is destroyed while actions are still active, notify them
    ActionContext emptyCtx{};
    for (auto& entry : activeActions) {
        if (entry.action) {
            entry.action->OnEnd(ActionEndReason::Interrupted, emptyCtx);
        }
    }
    activeActions.clear();
}

void ActionScheduler::RecalculateResourceMask() noexcept {
    claimedResourceMask = 0;
    for (const auto& entry : activeActions) {
        claimedResourceMask |= entry.resources;
    }
}

void ActionScheduler::Schedule(std::unique_ptr<ICanvasAction> action, ActionContext& ctx) {
    if (!action) return;

    if (isUpdating) {
        // Enqueue to prevent iterator invalidation during hot update loop
        pendingQueue.push_back(std::move(action));
        return;
    }

    const uint32_t reqResources = action->GetRequiredResources();

    // Preempt and cancel conflicting active actions
    if (reqResources != 0) {
        for (auto it = activeActions.begin(); it != activeActions.end(); ) {
            if ((it->resources & reqResources) != 0) {
                it->action->OnEnd(ActionEndReason::Interrupted, ctx);
                it = activeActions.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Initialize the incoming action
    action->OnStart(ctx);

    // If incoming action claims Camera, Selection, or DocumentStructure, instantly mark layer dirty
    if ((reqResources & (ToResourceMask(CanvasResource::Camera) |
                         ToResourceMask(CanvasResource::Selection) |
                         ToResourceMask(CanvasResource::DocumentStructure))) != 0) {
        ctx.InvalidateLayer();
    }

    // If action completed immediately during OnStart (e.g. InstantAction)
    if (action->IsFinished(ctx)) {
        action->OnEnd(ActionEndReason::Finished, ctx);
        ctx.InvalidateLayer();
        RecalculateResourceMask();
        return;
    }

    // Register into active pool
    activeActions.push_back(RunningAction{std::move(action), reqResources});
    claimedResourceMask |= reqResources;
}

void ActionScheduler::CancelWithResources(uint32_t resourceMask, ActionContext& ctx) {
    if (resourceMask == 0) return;

    for (auto it = activeActions.begin(); it != activeActions.end(); ) {
        if ((it->resources & resourceMask) != 0) {
            it->action->OnEnd(ActionEndReason::Interrupted, ctx);
            ctx.InvalidateLayer();
            it = activeActions.erase(it);
        } else {
            ++it;
        }
    }

    RecalculateResourceMask();
}

void ActionScheduler::CancelAll(ActionContext& ctx) {
    for (auto& entry : activeActions) {
        if (entry.action) {
            entry.action->OnEnd(ActionEndReason::Interrupted, ctx);
        }
    }
    activeActions.clear();
    claimedResourceMask = 0;

    for (auto& pending : pendingQueue) {
        if (pending) {
            pending->OnEnd(ActionEndReason::Interrupted, ctx);
        }
    }
    pendingQueue.clear();
    ctx.InvalidateLayer();
}

void ActionScheduler::Update(double dt, ActionContext& ctx) {
    isUpdating = true;

    // Process all running actions for this frame
    for (auto it = activeActions.begin(); it != activeActions.end(); ) {
        it->action->OnUpdate(dt, ctx);

        // Instantly mark layer dirty during active camera, selection, or document actions
        if ((it->resources & (ToResourceMask(CanvasResource::Camera) |
                              ToResourceMask(CanvasResource::Selection) |
                              ToResourceMask(CanvasResource::DocumentStructure))) != 0) {
            ctx.InvalidateLayer();
        }

        if (it->action->IsFinished(ctx)) {
            it->action->OnEnd(ActionEndReason::Finished, ctx);
            ctx.InvalidateLayer();
            it = activeActions.erase(it);
        } else {
            ++it;
        }
    }

    isUpdating = false;

    // Flush any pending actions that were queued during update
    if (!pendingQueue.empty()) {
        auto queued = std::move(pendingQueue);
        pendingQueue.clear();
        for (auto& action : queued) {
            Schedule(std::move(action), ctx);
        }
    }

    RecalculateResourceMask();
}

bool ActionScheduler::HasActiveActions() const noexcept {
    return !activeActions.empty() || !pendingQueue.empty();
}

size_t ActionScheduler::ActiveActionCount() const noexcept {
    return activeActions.size() + pendingQueue.size();
}

bool ActionScheduler::IsResourceClaimed(CanvasResource res) const noexcept {
    return (claimedResourceMask & ToResourceMask(res)) != 0;
}

uint32_t ActionScheduler::GetClaimedResources() const noexcept {
    return claimedResourceMask;
}

bool ActionScheduler::IsActionRunning(std::string_view name) const noexcept {
    for (const auto& entry : activeActions) {
        if (entry.action && entry.action->GetName() == name) {
            return true;
        }
    }
    for (const auto& pending : pendingQueue) {
        if (pending && pending->GetName() == name) {
            return true;
        }
    }
    return false;
}

} // namespace Folio

