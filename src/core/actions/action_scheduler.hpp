#pragma once
/**
 * =========================================================================================
 * @file core/actions/action_scheduler.hpp
 * @brief High-performance runtime coordinator for active temporal Canvas Actions
 * =========================================================================================
 */

#include <vector>
#include <memory>
#include <string_view>
#include "core/actions/action_types.hpp"
#include "core/actions/canvas_action.hpp"

namespace Folio {

/**
 * @class ActionScheduler
 * @brief Manages running multi-frame actions, enforces resource mutual exclusion,
 *        and drives frame-by-frame updates (120+ FPS).
 */
class ActionScheduler {
public:
    explicit ActionScheduler(size_t reserveCapacity = 16);
    ~ActionScheduler();

    // Non-copyable, movable
    ActionScheduler(const ActionScheduler&) = delete;
    ActionScheduler& operator=(const ActionScheduler&) = delete;
    ActionScheduler(ActionScheduler&&) noexcept = default;
    ActionScheduler& operator=(ActionScheduler&&) noexcept = default;

    /**
     * @brief Schedules a new action for execution.
     * Automatically preempts and cancels any running actions with conflicting resources.
     * @param action Unique pointer to the action to schedule.
     * @param ctx Execution context with canvas and engine handles.
     */
    void Schedule(std::unique_ptr<ICanvasAction> action, ActionContext& ctx);

    /**
     * @brief Cancels all actions holding any of the specified resources.
     * @param resourceMask Bitmask of CanvasResource to cancel.
     * @param ctx Execution context.
     */
    void CancelWithResources(uint32_t resourceMask, ActionContext& ctx);

    /**
     * @brief Cancels all active actions with ActionEndReason::Interrupted.
     * @param ctx Execution context.
     */
    void CancelAll(ActionContext& ctx);

    /**
     * @brief Drives the active actions for one frame. Call once per frame at 120 FPS.
     * @param dt Elapsed time since previous frame in seconds.
     * @param ctx Execution context.
     */
    void Update(double dt, ActionContext& ctx);

    /**
     * @brief Checks if any actions are currently active.
     */
    [[nodiscard]] bool HasActiveActions() const noexcept;

    /**
     * @brief Returns count of currently running actions.
     */
    [[nodiscard]] size_t ActiveActionCount() const noexcept;

    /**
     * @brief Checks if a specific resource is currently locked by a running action.
     */
    [[nodiscard]] bool IsResourceClaimed(CanvasResource res) const noexcept;

    /**
     * @brief Returns the complete active resource mask.
     */
    [[nodiscard]] uint32_t GetClaimedResources() const noexcept;

    /**
     * @brief Checks if an action with a specific name is currently active.
     */
    [[nodiscard]] bool IsActionRunning(std::string_view name) const noexcept;

private:
    struct RunningAction {
        std::unique_ptr<ICanvasAction> action;
        uint32_t resources{0};
    };

    std::vector<RunningAction> activeActions;
    std::vector<std::unique_ptr<ICanvasAction>> pendingQueue;
    uint32_t claimedResourceMask{0};
    bool isUpdating{false};

    void RecalculateResourceMask() noexcept;
};

// Convenient type alias for domain unification
using ActionManager = ActionScheduler;

} // namespace Folio

