#pragma once
/**
 * =========================================================================================
 * @file core/actions/canvas_action.hpp
 * @brief Base interface and standard composite building blocks for temporal Canvas Actions
 * =========================================================================================
 */

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <algorithm>
#include "core/actions/action_types.hpp"

namespace Folio {

/**
 * @class ICanvasAction
 * @brief Core interface for continuous, multi-frame canvas behaviors (gestures, math, animations).
 */
class ICanvasAction {
public:
    virtual ~ICanvasAction() = default;

    /**
     * @brief Human-readable identifier for profiling and debugging.
     */
    [[nodiscard]] virtual std::string_view GetName() const noexcept = 0;

    /**
     * @brief Bitmask of CanvasResource requirements for mutual exclusion arbitration.
     */
    [[nodiscard]] virtual uint32_t GetRequiredResources() const noexcept {
        return ToResourceMask(CanvasResource::None);
    }

    /**
     * @brief Invoked once when the action is scheduled and becomes active.
     */
    virtual void OnStart(ActionContext& ctx) { (void)ctx; }

    /**
     * @brief Invoked each frame during the engine tick (120+ FPS). Must be non-blocking.
     * @param dt Delta time since previous frame in seconds.
     * @param ctx Execution context with canvas, engine, and session handles.
     */
    virtual void OnUpdate(double dt, ActionContext& ctx) = 0;

    /**
     * @brief Evaluates whether the action has reached completion.
     */
    [[nodiscard]] virtual bool IsFinished(ActionContext& ctx) const = 0;

    /**
     * @brief Invoked once when the action finishes, is preempted, or fails.
     * @param reason Why the action ended (Finished, Interrupted, Failed).
     */
    virtual void OnEnd(ActionEndReason reason, ActionContext& ctx) { (void)reason; (void)ctx; }
};

/**
 * @class InstantAction
 * @brief Utility action that performs an atomic operation on start and immediately finishes.
 */
class InstantAction : public ICanvasAction {
private:
    std::string name;
    uint32_t resources{0};
    std::function<void(ActionContext&)> callback;
    bool executed{false};

public:
    InstantAction(std::string_view actionName,
                  std::function<void(ActionContext&)> func,
                  uint32_t requiredResources = 0)
        : name(actionName), resources(requiredResources), callback(std::move(func)) {}

    [[nodiscard]] std::string_view GetName() const noexcept override { return name; }
    [[nodiscard]] uint32_t GetRequiredResources() const noexcept override { return resources; }

    void OnStart(ActionContext& ctx) override {
        if (callback) {
            callback(ctx);
        }
        executed = true;
    }

    void OnUpdate(double dt, ActionContext& ctx) override {
        (void)dt; (void)ctx;
    }

    [[nodiscard]] bool IsFinished(ActionContext& ctx) const override {
        (void)ctx;
        return executed;
    }
};

/**
 * @class WaitAction
 * @brief Pauses execution for a specified duration in seconds.
 */
class WaitAction : public ICanvasAction {
private:
    double targetDuration{0.0};
    double elapsed{0.0};

public:
    explicit WaitAction(double durationSeconds)
        : targetDuration(durationSeconds > 0.0 ? durationSeconds : 0.0) {}

    [[nodiscard]] std::string_view GetName() const noexcept override { return "WaitAction"; }

    void OnStart(ActionContext& ctx) override {
        (void)ctx;
        elapsed = 0.0;
    }

    void OnUpdate(double dt, ActionContext& ctx) override {
        (void)ctx;
        elapsed += dt;
    }

    [[nodiscard]] bool IsFinished(ActionContext& ctx) const override {
        (void)ctx;
        return elapsed >= targetDuration;
    }

    [[nodiscard]] double GetElapsed() const noexcept { return elapsed; }
    [[nodiscard]] double GetDuration() const noexcept { return targetDuration; }
};

/**
 * @class SequentialAction
 * @brief Executes a series of child actions sequentially, advancing to the next when the current finishes.
 */
class SequentialAction : public ICanvasAction {
private:
    std::vector<std::unique_ptr<ICanvasAction>> actions;
    size_t currentIndex{0};
    bool startedCurrent{false};
    uint32_t aggregatedResources{0};

public:
    SequentialAction() = default;

    explicit SequentialAction(std::vector<std::unique_ptr<ICanvasAction>> sequence) {
        for (auto& act : sequence) {
            AddAction(std::move(act));
        }
    }

    void AddAction(std::unique_ptr<ICanvasAction> action) {
        if (action) {
            aggregatedResources |= action->GetRequiredResources();
            actions.push_back(std::move(action));
        }
    }

    [[nodiscard]] std::string_view GetName() const noexcept override { return "SequentialAction"; }
    [[nodiscard]] uint32_t GetRequiredResources() const noexcept override { return aggregatedResources; }

    void OnStart(ActionContext& ctx) override {
        currentIndex = 0;
        startedCurrent = false;
        if (!actions.empty()) {
            actions[currentIndex]->OnStart(ctx);
            startedCurrent = true;
        }
    }

    void OnUpdate(double dt, ActionContext& ctx) override {
        while (currentIndex < actions.size()) {
            auto& current = actions[currentIndex];
            if (!startedCurrent) {
                current->OnStart(ctx);
                startedCurrent = true;
            }

            current->OnUpdate(dt, ctx);

            if (current->IsFinished(ctx)) {
                current->OnEnd(ActionEndReason::Finished, ctx);
                ++currentIndex;
                startedCurrent = false;
            } else {
                break; // Still executing current action
            }
        }
    }

    [[nodiscard]] bool IsFinished(ActionContext& ctx) const override {
        (void)ctx;
        return currentIndex >= actions.size();
    }

    void OnEnd(ActionEndReason reason, ActionContext& ctx) override {
        if (reason != ActionEndReason::Finished && currentIndex < actions.size()) {
            if (startedCurrent) {
                actions[currentIndex]->OnEnd(reason, ctx);
            }
        }
    }
};

/**
 * @class ParallelAction
 * @brief Executes multiple child actions concurrently until all have completed.
 */
class ParallelAction : public ICanvasAction {
private:
    struct ChildState {
        std::unique_ptr<ICanvasAction> action;
        bool finished{false};
    };

    std::vector<ChildState> children;
    uint32_t aggregatedResources{0};

public:
    ParallelAction() = default;

    void AddAction(std::unique_ptr<ICanvasAction> action) {
        if (action) {
            aggregatedResources |= action->GetRequiredResources();
            children.push_back(ChildState{std::move(action), false});
        }
    }

    [[nodiscard]] std::string_view GetName() const noexcept override { return "ParallelAction"; }
    [[nodiscard]] uint32_t GetRequiredResources() const noexcept override { return aggregatedResources; }

    void OnStart(ActionContext& ctx) override {
        for (auto& child : children) {
            child.finished = false;
            child.action->OnStart(ctx);
        }
    }

    void OnUpdate(double dt, ActionContext& ctx) override {
        for (auto& child : children) {
            if (!child.finished) {
                child.action->OnUpdate(dt, ctx);
                if (child.action->IsFinished(ctx)) {
                    child.action->OnEnd(ActionEndReason::Finished, ctx);
                    child.finished = true;
                }
            }
        }
    }

    [[nodiscard]] bool IsFinished(ActionContext& ctx) const override {
        (void)ctx;
        return std::all_of(children.begin(), children.end(), [](const ChildState& c) {
            return c.finished;
        });
    }

    void OnEnd(ActionEndReason reason, ActionContext& ctx) override {
        for (auto& child : children) {
            if (!child.finished) {
                child.action->OnEnd(reason, ctx);
                child.finished = true;
            }
        }
    }
};

} // namespace Folio
