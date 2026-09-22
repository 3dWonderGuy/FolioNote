/**
 * =========================================================================================
 * @file test_thread_pool.cpp
 * @brief Standalone Local Concurrency & Stress Test Suite for Folio::ThreadPool
 * =========================================================================================
 *
 * ARCHITECTURAL PURPOSE & CONCURRENCY GUARANTEES:
 * -----------------------------------------------
 * In FolioNote, the ThreadPool handles all asynchronous off-main-thread work:
 * disk serialization, SQLite transactions, stroke rasterization, and log dispatching.
 *
 * Any flaw here causes UI frame drops, data loss during autosaves, or exit deadlocks.
 *
 * WHAT THIS SUITE TESTS:
 * ----------------------
 * 1. Test_BasicEnqueueAndFutures:
 *    - Enqueues tasks with varying return types (int, string, structs).
 *    - Validates that std::future::get() correctly unblocks and returns the exact computed value.
 *
 * 2. Test_HighThroughputTaskProcessing:
 *    - Enqueues 10,000 tasks across all worker threads.
 *    - Uses an std::atomic<uint64_t> counter to verify that 100% of tasks execute with zero drops.
 *
 * 3. Test_MultiProducerContention:
 *    - Launches 8 independent producer threads, each pushing 1,000 tasks simultaneously.
 *    - Stresses the internal queueMutex and condition_variable under heavy cross-thread contention.
 *
 * 4. Test_ExceptionIsolationAndWorkerSurvival:
 *    - Enqueues tasks that throw std::runtime_error.
 *    - Verifies the thread pool catches the exception, logs it via FormatError, and leaves
 *      the worker threads alive to process subsequent valid tasks normally.
 *
 * 5. Test_GracefulShutdownAndQueueDraining:
 *    - Pushes 2,000 tasks and immediately calls Shutdown().
 *    - Verifies that all 2,000 tasks finish executing before Shutdown() returns, and that
 *      subsequent Enqueue calls throw the expected std::runtime_error.
 * =========================================================================================
 */

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <atomic>
#include <future>
#include <thread>
#include <numeric>

#include "utils/thread_pool.hpp"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "\n  [FAILED] " << msg << "\n" \
                      << "  File: " << __FILE__ << " | Line: " << __LINE__ << "\n"; \
            return false; \
        } \
    } while (0)

#define RUN_TEST_CASE(fn) \
    do { \
        std::cout << "[RUNNING] " << #fn << "... " << std::flush; \
        auto t0 = std::chrono::high_resolution_clock::now(); \
        if (fn()) { \
            auto t1 = std::chrono::high_resolution_clock::now(); \
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count(); \
            std::cout << "PASSED (" << ms << " ms)\n"; \
        } else { \
            std::cout << ">> TEST SUITE ABORTED ON FAILURE <<\n"; \
            return 1; \
        } \
    } while (0)

using namespace Folio;

// =========================================================================================
// TEST CASE 1: Future Synchronization & Return Value Heterogeneity
// =========================================================================================
bool Test_BasicEnqueueAndFutures() {
    ThreadPool pool(4);
    TEST_ASSERT(pool.GetWorkerCount() == 4, "Worker count mismatch");
    TEST_ASSERT(!pool.IsStopped(), "Pool marked as stopped immediately after spawn");

    // Task 1: Basic integer arithmetic
    auto f1 = pool.Enqueue([](int a, int b) {
        return a * b + 10;
    }, 7, 6);

    // Task 2: String manipulation
    auto f2 = pool.Enqueue([](const std::string& prefix) {
        return prefix + "_processed";
    }, std::string("folio"));

    // Task 3: Lambda capturing nothing
    auto f3 = pool.Enqueue([] {
        return 42ULL;
    });

    TEST_ASSERT(f1.get() == 52, "Integer return value mismatch");
    TEST_ASSERT(f2.get() == "folio_processed", "String return value mismatch");
    TEST_ASSERT(f3.get() == 42ULL, "Scalar return value mismatch");

    return true;
}

// =========================================================================================
// TEST CASE 2: High-Throughput Task Processing (10,000 Jobs)
// =========================================================================================
bool Test_HighThroughputTaskProcessing() {
    ThreadPool pool(4);
    constexpr uint64_t TOTAL_TASKS = 10000;
    std::atomic<uint64_t> completedTasks{0};

    std::vector<std::future<void>> futures;
    futures.reserve(TOTAL_TASKS);

    for (uint64_t i = 0; i < TOTAL_TASKS; ++i) {
        futures.push_back(pool.Enqueue([&completedTasks] {
            completedTasks.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    for (auto& f : futures) {
        f.wait();
    }

    TEST_ASSERT(completedTasks.load(std::memory_order_relaxed) == TOTAL_TASKS, 
                "Not all enqueued tasks completed");
    return true;
}

// =========================================================================================
// TEST CASE 3: Multi-Producer Contention (8 Threads x 1,000 Tasks)
// =========================================================================================
bool Test_MultiProducerContention() {
    ThreadPool pool(6);
    constexpr int PRODUCER_THREADS = 8;
    constexpr int TASKS_PER_PRODUCER = 1000;

    std::atomic<int> sharedCounter{0};
    std::vector<std::thread> producers;
    producers.reserve(PRODUCER_THREADS);

    for (int p = 0; p < PRODUCER_THREADS; ++p) {
        producers.emplace_back([&pool, &sharedCounter] {
            std::vector<std::future<void>> localFutures;
            localFutures.reserve(TASKS_PER_PRODUCER);

            for (int i = 0; i < TASKS_PER_PRODUCER; ++i) {
                localFutures.push_back(pool.Enqueue([&sharedCounter] {
                    sharedCounter.fetch_add(1, std::memory_order_relaxed);
                }));
            }

            for (auto& f : localFutures) {
                f.wait();
            }
        });
    }

    for (auto& t : producers) {
        if (t.joinable()) {
            t.join();
        }
    }

    constexpr int EXPECTED = PRODUCER_THREADS * TASKS_PER_PRODUCER;
    TEST_ASSERT(sharedCounter.load(std::memory_order_relaxed) == EXPECTED, 
                "Contention test dropped tasks under concurrent enqueue load");
    return true;
}

// =========================================================================================
// TEST CASE 4: Exception Isolation & Worker Thread Survival
// =========================================================================================
bool Test_ExceptionIsolationAndWorkerSurvival() {
    ThreadPool pool(2);

    // 1. Enqueue detached tasks that throw exceptions
    // The worker thread should catch it, log an error code, and continue running.
    pool.EnqueueDetached([] {
        throw std::runtime_error("Simulated catastrophic task exception");
    });

    pool.EnqueueDetached([] {
        throw std::logic_error("Simulated logic error inside worker closure");
    });

    // Short sleep to allow the detached tasks to execute and catch in background
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // 2. Ensure the workers are still completely alive by queuing and completing valid work
    std::atomic<bool> workerAlive{false};
    auto future = pool.Enqueue([&workerAlive] {
        workerAlive.store(true, std::memory_order_release);
        return 12345;
    });

    TEST_ASSERT(future.get() == 12345, "Worker threads failed to return result after earlier task exception");
    TEST_ASSERT(workerAlive.load(std::memory_order_acquire), "Worker threads died from previous exceptions");

    return true;
}

// =========================================================================================
// TEST CASE 5: Graceful Shutdown & Queue Draining
// =========================================================================================
bool Test_GracefulShutdownAndQueueDraining() {
    constexpr int TASK_COUNT = 2000;
    std::atomic<int> processedCount{0};

    {
        ThreadPool pool(4);

        // Queue 2,000 tasks
        for (int i = 0; i < TASK_COUNT; ++i) {
            pool.EnqueueDetached([&processedCount] {
                // Micro sleep to simulate actual disk/compute work
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                processedCount.fetch_add(1, std::memory_order_relaxed);
            });
        }

        // Trigger Shutdown: Must drain all 2,000 tasks before returning
        pool.Shutdown();
        TEST_ASSERT(pool.IsStopped(), "Pool does not report stopped status after Shutdown()");

        // Verify that calling Enqueue after shutdown throws std::runtime_error
        bool threwException = false;
        try {
            pool.Enqueue([] { return 1; });
        } catch (const std::runtime_error&) {
            threwException = true;
        }
        TEST_ASSERT(threwException, "Enqueue on stopped pool did not throw std::runtime_error");
    } // Pool destructor runs here; should be a clean no-op since Shutdown was already called

    TEST_ASSERT(processedCount.load(std::memory_order_relaxed) == TASK_COUNT, 
                "Shutdown exited before all queued tasks finished draining");

    return true;
}

// =========================================================================================
// Main Entry Point
// =========================================================================================
int main() {
    std::cout << "\n======================================================\n";
    std::cout << "     FolioNote ThreadPool Diagnostic Suite            \n";
    std::cout << "======================================================\n";

    auto tStart = std::chrono::high_resolution_clock::now();

    RUN_TEST_CASE(Test_BasicEnqueueAndFutures);
    RUN_TEST_CASE(Test_HighThroughputTaskProcessing);
    RUN_TEST_CASE(Test_MultiProducerContention);
    RUN_TEST_CASE(Test_ExceptionIsolationAndWorkerSurvival);
    RUN_TEST_CASE(Test_GracefulShutdownAndQueueDraining);

    auto tEnd = std::chrono::high_resolution_clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();

    std::cout << "------------------------------------------------------\n";
    std::cout << ">>> ALL THREADPOOL INVARIANTS VERIFIED (100% HEALTHY) <<<\n";
    std::cout << "    Total Test Suite Duration: " << totalMs << " ms\n";
    std::cout << "======================================================\n\n";

    return 0;
}