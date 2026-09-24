#pragma once

/**
 * =========================================================================================
 * @file thread_pool.hpp
 * @brief High-performance asynchronous worker thread pool for the Folio core engine.
 * 
 * General Concept (How it works in Plain English):
 *   Think of the ThreadPool like a restaurant kitchen:
 *   - The "Worker Threads" are the cooks (usually 3 to 7 cooks based on your CPU cores).
 *   - The "Task Queue" is the order ticket carousel.
 *   
 *   Instead of hiring a brand new cook every time an order comes in (which in computer terms
 *   means spawning an OS thread—an expensive operation that consumes ~1MB of stack and thousands
 *   of CPU cycles), we keep a small team of cooks permanently waiting in the kitchen.
 * 
 *   When an order arrives (e.g. saving an ink file, generating a thumbnail, or indexing notes):
 *     1. The caller drops the task onto the ticket carousel (tasks queue).
 *     2. A sleeping worker cook is immediately woken up by a bell (condition_variable).
 *     3. The cook grabs the task, cooks it, and goes right back to sleep until the next order.
 *     4. If there are no tasks, all workers sleep at 0% CPU usage.
 * =========================================================================================
 */

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <atomic>
#include <algorithm>
#include <string>

#include "utils/logger.hpp"
#include "utils/error_codes.hpp"

namespace Folio {

class ThreadPool {
public:
    /**
     * @brief Constructs the ThreadPool and spawns background worker threads.
     * 
     * CPU Allocation Math & Heuristic:
     *   - If `threads == 0`, we query the operating system for total logical CPU cores (`std::thread::hardware_concurrency()`).
     *   - Formula: `threads = (cores > 2) ? (cores - 1) : 2;`
     *   - Why `cores - 1`? We always reserve 1 entire core exclusively for the main UI rendering thread!
     *     This guarantees the interactive canvas stays silky smooth at 60–120 FPS even when background
     *     threads are busy saving huge files or indexing search databases.
     * 
     * @param threads Number of worker threads to spawn (0 = auto-detect hardware).
     */
    explicit ThreadPool(size_t threads = 0)
        : stop(false) {
        
        // Step 1: Auto-calculate optimal worker count based on physical hardware
        if (threads == 0) {
            unsigned int hw = std::thread::hardware_concurrency();
            threads = (hw > 2) ? static_cast<size_t>(hw - 1) : 2u;
        }

        workers.reserve(threads);

        // Step 2: Spawn worker threads and set up their permanent loop
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this, i] {
                while (true) {
                    std::function<void()> task;

                    // --- Critical Section: Waiting for Work ---
                    {
                        // Acquire unique lock over the shared task queue
                        std::unique_lock<std::mutex> lock(this->queueMutex);

                        // Put thread to sleep until either:
                        // a) A new task is pushed into the queue (!tasks.empty())
                        // b) The pool is shutting down (stop == true)
                        // While sleeping, condition.wait releases the mutex and uses 0% CPU cycles.
                        this->condition.wait(lock, [this] {
                            return this->stop.load(std::memory_order_acquire) || !this->tasks.empty();
                        });

                        // If shutdown was requested and no tasks remain in queue, exit thread cleanly
                        if (this->stop.load(std::memory_order_acquire) && this->tasks.empty()) {
                            return;
                        }

                        // Pop the oldest task (FIFO order) from queue
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    } // Mutex is released here so other workers can grab tasks simultaneously!

                    // --- Task Execution & Sandbox ---
                    if (task) {
                        try {
                            task(); // Execute the actual work
                        } catch (const std::exception& ex) {
                            // Catch standard C++ exceptions so one bad task doesn't kill the entire application
                            LOG_ERROR_CODE(General, FolioErrorCode::ThreadPoolWorkerException,
                                "Worker [" + std::to_string(i) + "] caught unhandled exception: " + ex.what());
                        } catch (...) {
                            // Catch non-standard exceptions
                            LOG_ERROR_CODE(General, FolioErrorCode::ThreadPoolWorkerException,
                                "Worker [" + std::to_string(i) + "] caught non-standard unknown exception");
                        }
                    }
                }
            });
        }

        LOG_INFO(General, "Initialized worker thread pool with " + std::to_string(threads) + " threads");
    }

    /**
     * @brief Enqueues a callable task and returns a `std::future` to receive its return value.
     * 
     * Use Case:
     *   Use `Enqueue` when you need to know WHEN the task finishes, or when you need its RETURN VALUE.
     *   Example:
     *     std::future<bool> result = pool.Enqueue([]() { return LoadHeavyFile(); });
     *     // Later...
     *     if (result.get()) { ... }
     * 
     * Working Process:
     *   1. Packages the function `f` and arguments into a `std::packaged_task`.
     *   2. Extracts the `std::future` associated with the packaged task.
     *   3. Locks `queueMutex` and checks if the pool has already been stopped.
     *   4. Pushes the task into the queue.
     *   5. Rings the bell (`condition.notify_one()`) to wake up 1 sleeping worker thread.
     *   6. Returns the `future` to the caller.
     * 
     * @tparam F Callable function, lambda, or member function pointer.
     * @tparam Args Argument types to pass to the function.
     * @return std::future holding the eventual return value of the function.
     */
    template<class F, class... Args>
    auto Enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        // Wrap callable into packaged task so it can fulfill a future
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();

        {
            std::unique_lock<std::mutex> lock(queueMutex);

            // Rejection Guard: Do not accept new work if pool is already being shut down
            if (stop.load(std::memory_order_acquire)) {
                LOG_ERROR_CODE(General, FolioErrorCode::ThreadPoolShuttingDown, 
                    "Enqueue called on a stopped thread pool");
                throw std::runtime_error("Folio::ThreadPool: Enqueue called on a stopped thread pool.");
            }

            tasks.emplace([task]() { 
                (*task)(); 
            });
        }

        // Wake up a sleeping worker thread to pick up this new task
        condition.notify_one();
        return res;
    }

    /**
     * @brief Enqueues a "fire-and-forget" task without the overhead of creating a std::future.
     * 
     * Use Case:
     *   Use `EnqueueDetached` when you don't care about the return value and don't need to block waiting for it.
     *   Examples: Auto-saving a dirty page to disk, pruning a cache, or logging metrics in the background.
     * 
     * Working Process:
     *   1. Locks `queueMutex`.
     *   2. If shutting down, logs warning and drops task safely.
     *   3. Pushes raw `std::function<void()>` into the FIFO task queue.
     *   4. Rings the bell (`condition.notify_one()`) to wake up 1 sleeping worker thread.
     * 
     * @param task Void lambda or callable to execute asynchronously.
     */
    void EnqueueDetached(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);

            if (stop.load(std::memory_order_acquire)) {
                LOG_WARN_CODE(General, FolioErrorCode::ThreadPoolShuttingDown, 
                    "EnqueueDetached called on a stopped thread pool; dropping task");
                return;
            }

            tasks.push(std::move(task));
        }

        // Wake up a sleeping worker thread
        condition.notify_one();
    }

    /**
     * @brief Destructor: Ensures all worker threads are cleanly drained and joined before destroying the pool.
     */
    ~ThreadPool() {
        Shutdown();
    }

    /**
     * @brief Cleanly shuts down the thread pool.
     * 
     * Working Process:
     *   1. Atomically sets `stop = true`.
     *   2. Wakes up ALL sleeping worker threads simultaneously (`condition.notify_all()`).
     *   3. Drains remaining tasks in the queue.
     *   4. Calls `join()` on each OS worker thread, waiting for them to finish cleanly.
     *   5. Clears worker list and logs shutdown confirmation.
     */
    void Shutdown() {
        bool expected = false;
        if (stop.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            LOG_INFO(General, "Initiating shutdown; draining " + std::to_string(tasks.size()) + 
                              " pending tasks across " + std::to_string(workers.size()) + " workers");

            // Wake up all sleeping threads so they see stop == true
            condition.notify_all();

            // Wait for all threads to terminate
            for (std::thread& worker : workers) {
                if (worker.joinable()) {
                    worker.join();
                }
            }

            workers.clear();
            LOG_INFO(General, "All worker threads terminated successfully");
        }
    }

    /**
     * @brief Gets the total number of worker threads operating in this pool.
     */
    [[nodiscard]] size_t GetWorkerCount() const noexcept { 
        return workers.size(); 
    }

    /**
     * @brief Checks if the pool has been flagged for shutdown or is stopped.
     */
    [[nodiscard]] bool IsStopped() const noexcept { 
        return stop.load(std::memory_order_acquire); 
    }

private:
    std::vector<std::thread> workers;               ///< Pool of permanent worker threads
    std::queue<std::function<void()>> tasks;        ///< Thread-safe FIFO queue of pending tasks
    std::mutex queueMutex;                          ///< Mutex guarding tasks queue and stop flag
    std::condition_variable condition;              ///< Signals workers when work arrives or shutdown begins
    std::atomic<bool> stop;                         ///< Atomic flag indicating pool shutdown
};

/**
 * @brief Global singleton instance of the central ThreadPool for FolioNote.
 * 
 * Thread-safe Meyers singleton: initialized automatically on first access.
 */
inline ThreadPool& GetGlobalThreadPool() {
    static ThreadPool s_globalPool;
    return s_globalPool;
}

} // namespace Folio