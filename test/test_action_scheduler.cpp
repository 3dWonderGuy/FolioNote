/**
 * =========================================================================================
 * @file test_action_scheduler.cpp
 * @brief Unit tests for ActionScheduler, ICanvasAction, and resource contention arbitration
 * =========================================================================================
 */

#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <memory>

#include "core/actions/action_types.hpp"
#include "core/actions/canvas_action.hpp"
#include "core/actions/action_scheduler.hpp"

using namespace Folio;

// Test Action tracking its own lifecycle transitions
class LifecycleTestAction : public ICanvasAction {
public:
    std::string name;
    uint32_t resources{0};
    int updateCount{0};
    int finishAfterUpdates{3};
    bool started{false};
    bool ended{false};
    ActionEndReason endReason{ActionEndReason::Failed};

    LifecycleTestAction(std::string actionName, uint32_t reqRes, int maxUpdates)
        : name(std::move(actionName)), resources(reqRes), finishAfterUpdates(maxUpdates) {}

    [[nodiscard]] std::string_view GetName() const noexcept override { return name; }
    [[nodiscard]] uint32_t GetRequiredResources() const noexcept override { return resources; }

    void OnStart(ActionContext& ctx) override {
        (void)ctx;
        started = true;
    }

    void OnUpdate(double dt, ActionContext& ctx) override {
        (void)dt; (void)ctx;
        ++updateCount;
    }

    [[nodiscard]] bool IsFinished(ActionContext& ctx) const override {
        (void)ctx;
        return updateCount >= finishAfterUpdates;
    }

    void OnEnd(ActionEndReason reason, ActionContext& ctx) override {
        (void)ctx;
        ended = true;
        endReason = reason;
    }
};

void TestInstantAction() {
    std::cout << "[Test] Running InstantAction test..." << std::endl;
    ActionScheduler scheduler;
    ActionContext ctx;

    bool executed = false;
    auto instant = std::make_unique<InstantAction>(
        "TestInstant",
        [&executed](ActionContext&) { executed = true; },
        ToResourceMask(CanvasResource::Selection)
    );

    scheduler.Schedule(std::move(instant), ctx);

    assert(executed && "InstantAction callback should execute immediately on schedule");
    assert(!scheduler.HasActiveActions() && "InstantAction should not remain active after schedule");
    assert(!scheduler.IsResourceClaimed(CanvasResource::Selection) && "Resources must be released");
    std::cout << "  -> PASSED" << std::endl;
}

void TestWaitAction() {
    std::cout << "[Test] Running WaitAction test..." << std::endl;
    ActionScheduler scheduler;
    ActionContext ctx;

    auto wait = std::make_unique<WaitAction>(0.5); // 500 ms
    scheduler.Schedule(std::move(wait), ctx);

    assert(scheduler.HasActiveActions());
    assert(scheduler.ActiveActionCount() == 1);

    // Tick 200 ms
    scheduler.Update(0.2, ctx);
    assert(scheduler.HasActiveActions() && "Should still be active at 200ms");

    // Tick another 200 ms (total 400 ms)
    scheduler.Update(0.2, ctx);
    assert(scheduler.HasActiveActions() && "Should still be active at 400ms");

    // Tick 150 ms (total 550 ms >= 500 ms)
    scheduler.Update(0.15, ctx);
    assert(!scheduler.HasActiveActions() && "WaitAction should finish after 500ms");
    std::cout << "  -> PASSED" << std::endl;
}

void TestResourceContentionAndPreemption() {
    std::cout << "[Test] Running Resource Contention & Preemption test..." << std::endl;
    ActionScheduler scheduler;
    ActionContext ctx;

    auto actionA = std::make_unique<LifecycleTestAction>("ActionA", ToResourceMask(CanvasResource::Selection), 10);
    auto* rawA = actionA.get();

    scheduler.Schedule(std::move(actionA), ctx);
    assert(rawA->started);
    assert(scheduler.IsResourceClaimed(CanvasResource::Selection));
    assert(!scheduler.IsResourceClaimed(CanvasResource::Camera));

    // Tick 1 frame
    scheduler.Update(0.016, ctx);
    assert(rawA->updateCount == 1);

    // Schedule Action B requesting the SAME resource (Selection)
    auto actionB = std::make_unique<LifecycleTestAction>("ActionB", ToResourceMask(CanvasResource::Selection), 5);
    auto* rawB = actionB.get();

    scheduler.Schedule(std::move(actionB), ctx);

    // Action A should have been preempted with Interrupted!
    assert(rawA->ended);
    assert(rawA->endReason == ActionEndReason::Interrupted);

    // Action B should now be active
    assert(rawB->started);
    assert(!rawB->ended);
    assert(scheduler.IsResourceClaimed(CanvasResource::Selection));

    // Tick Action B to completion
    for (int i = 0; i < 5; ++i) {
        scheduler.Update(0.016, ctx);
    }

    assert(rawB->ended);
    assert(rawB->endReason == ActionEndReason::Finished);
    assert(!scheduler.IsResourceClaimed(CanvasResource::Selection));
    std::cout << "  -> PASSED" << std::endl;
}

void TestOrthogonalConcurrentActions() {
    std::cout << "[Test] Running Orthogonal Concurrent Actions test..." << std::endl;
    ActionScheduler scheduler;
    ActionContext ctx;

    // Action A claims Selection
    auto actionA = std::make_unique<LifecycleTestAction>("SelectionAction", ToResourceMask(CanvasResource::Selection), 3);
    auto* rawA = actionA.get();

    // Action B claims Camera (non-conflicting!)
    auto actionB = std::make_unique<LifecycleTestAction>("CameraAction", ToResourceMask(CanvasResource::Camera), 3);
    auto* rawB = actionB.get();

    scheduler.Schedule(std::move(actionA), ctx);
    scheduler.Schedule(std::move(actionB), ctx);

    assert(scheduler.ActiveActionCount() == 2);
    assert(scheduler.IsResourceClaimed(CanvasResource::Selection));
    assert(scheduler.IsResourceClaimed(CanvasResource::Camera));
    assert(!rawA->ended && "Non-conflicting action must not be interrupted");

    // Both tick together
    scheduler.Update(0.016, ctx);
    assert(rawA->updateCount == 1);
    assert(rawB->updateCount == 1);

    scheduler.Update(0.016, ctx);
    scheduler.Update(0.016, ctx);

    assert(rawA->ended && rawA->endReason == ActionEndReason::Finished);
    assert(rawB->ended && rawB->endReason == ActionEndReason::Finished);
    assert(!scheduler.HasActiveActions());
    assert(scheduler.GetClaimedResources() == 0);
    std::cout << "  -> PASSED" << std::endl;
}

void TestSequentialAction() {
    std::cout << "[Test] Running SequentialAction test..." << std::endl;
    ActionScheduler scheduler;
    ActionContext ctx;

    auto step1 = std::make_unique<LifecycleTestAction>("Step1", ToResourceMask(CanvasResource::Selection), 2);
    auto* raw1 = step1.get();

    auto step2 = std::make_unique<LifecycleTestAction>("Step2", ToResourceMask(CanvasResource::LiveMathSolver), 2);
    auto* raw2 = step2.get();

    auto seq = std::make_unique<SequentialAction>();
    seq->AddAction(std::move(step1));
    seq->AddAction(std::move(step2));

    scheduler.Schedule(std::move(seq), ctx);

    assert(raw1->started);
    assert(!raw2->started && "Step 2 must not start until Step 1 completes");

    // Tick step 1
    scheduler.Update(0.016, ctx);
    scheduler.Update(0.016, ctx); // Step 1 finishes here, step 2 should start

    assert(raw1->ended && raw1->endReason == ActionEndReason::Finished);
    assert(raw2->started && "Step 2 should have started after Step 1 finished");

    // Tick step 2
    scheduler.Update(0.016, ctx);
    scheduler.Update(0.016, ctx);

    assert(raw2->ended && raw2->endReason == ActionEndReason::Finished);
    assert(!scheduler.HasActiveActions());
    std::cout << "  -> PASSED" << std::endl;
}

void TestParallelAction() {
    std::cout << "[Test] Running ParallelAction test..." << std::endl;
    ActionScheduler scheduler;
    ActionContext ctx;

    auto child1 = std::make_unique<LifecycleTestAction>("Child1", ToResourceMask(CanvasResource::Selection), 2);
    auto* raw1 = child1.get();

    auto child2 = std::make_unique<LifecycleTestAction>("Child2", ToResourceMask(CanvasResource::Camera), 4);
    auto* raw2 = child2.get();

    auto parallel = std::make_unique<ParallelAction>();
    parallel->AddAction(std::move(child1));
    parallel->AddAction(std::move(child2));

    scheduler.Schedule(std::move(parallel), ctx);

    assert(raw1->started);
    assert(raw2->started);

    // Tick 2 frames: child1 finishes, child2 still active
    scheduler.Update(0.016, ctx);
    scheduler.Update(0.016, ctx);
    assert(raw1->ended);
    assert(!raw2->ended);
    assert(scheduler.HasActiveActions());

    // Tick 2 more frames: child2 finishes
    scheduler.Update(0.016, ctx);
    scheduler.Update(0.016, ctx);
    assert(raw2->ended);
    assert(!scheduler.HasActiveActions());
    std::cout << "  -> PASSED" << std::endl;
}

void TestCancellation() {
    std::cout << "[Test] Running Cancellation test..." << std::endl;
    ActionScheduler scheduler;
    ActionContext ctx;

    auto actA = std::make_unique<LifecycleTestAction>("ActA", ToResourceMask(CanvasResource::Selection), 10);
    auto* rawA = actA.get();
    auto actB = std::make_unique<LifecycleTestAction>("ActB", ToResourceMask(CanvasResource::Camera), 10);
    auto* rawB = actB.get();

    scheduler.Schedule(std::move(actA), ctx);
    scheduler.Schedule(std::move(actB), ctx);

    // Cancel only Selection
    scheduler.CancelWithResources(ToResourceMask(CanvasResource::Selection), ctx);
    assert(rawA->ended && rawA->endReason == ActionEndReason::Interrupted);
    assert(!rawB->ended && "ActB (Camera) should not have been canceled");

    // Cancel all remaining
    scheduler.CancelAll(ctx);
    assert(rawB->ended && rawB->endReason == ActionEndReason::Interrupted);
    assert(!scheduler.HasActiveActions());
    std::cout << "  -> PASSED" << std::endl;
}

int main() {
    std::cout << "=== Running FolioNote ActionScheduler Unit Tests ===" << std::endl;

    TestInstantAction();
    TestWaitAction();
    TestResourceContentionAndPreemption();
    TestOrthogonalConcurrentActions();
    TestSequentialAction();
    TestParallelAction();
    TestCancellation();

    std::cout << "=== ALL ACTION SCHEDULER TESTS PASSED SUCCESSFULLY! ===" << std::endl;
    return 0;
}

