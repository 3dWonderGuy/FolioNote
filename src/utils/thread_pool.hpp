#pragma once

/**
 * =========================================================================================
 * @file thread_pool.hpp
 * @brief High-performance asynchronous worker thread pool for the Folio core engine.
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
    explicit ThreadPool(size_t threads = 0)
        : stop(false) {
        
        if (threads == 0) {
            unsigned int hw = std::thread::hardware_concurrency();
            threads = (hw > 2) ? static_cast<size_t>(hw - 1) : 2u;
        }

        workers.reserve(threads);

        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this, i] {
                while (true) {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(this->queueMutex);

                        this->condition.wait(lock, [this] {
                            return this->stop.load(std::memory_order_acquire) || !this->tasks.empty();
                        });

                        if (this->stop.load(std::memory_order_acquire) && this->tasks.empty()) {
                            return;
                        }

                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }

                    if (task) {
                        try {
                            task();
                        } catch (const std::exception& ex) {
                            LOG_ERROR(General, FormatError(FolioErrorCode::ThreadPoolWorkerException, 
                                "Worker [" + std::to_string(i) + "] caught unhandled exception: " + ex.what()));
                        } catch (...) {
                            LOG_ERROR(General, FormatError(FolioErrorCode::ThreadPoolWorkerException, 
                                "Worker [" + std::to_string(i) + "] caught non-standard unknown exception"));
                        }
                    }
                }
            });
        }

        LOG_INFO(General, "Initialized worker thread pool with " + std::to_string(threads) + " threads");
    }

    template<class F, class... Args>
    auto Enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();

        {
            std::unique_lock<std::mutex> lock(queueMutex);

            if (stop.load(std::memory_order_acquire)) {
                LOG_ERROR(General, FormatError(FolioErrorCode::ThreadPoolShuttingDown, 
                    "Enqueue called on a stopped thread pool"));
                throw std::runtime_error("Folio::ThreadPool: Enqueue called on a stopped thread pool.");
            }

            tasks.emplace([task]() { 
                (*task)(); 
            });
        }

        condition.notify_one();
        return res;
    }

    void EnqueueDetached(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);

            if (stop.load(std::memory_order_acquire)) {
                LOG_ERROR(General, FormatError(FolioErrorCode::ThreadPoolShuttingDown, 
                    "EnqueueDetached called on a stopped thread pool; dropping task"));
                return;
            }

            tasks.push(std::move(task));
        }

        condition.notify_one();
    }

    ~ThreadPool() {
        Shutdown();
    }

    void Shutdown() {
        bool expected = false;
        if (stop.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            LOG_INFO(General, "Initiating shutdown; draining " + std::to_string(tasks.size()) + 
                              " pending tasks across " + std::to_string(workers.size()) + " workers");

            condition.notify_all();

            for (std::thread& worker : workers) {
                if (worker.joinable()) {
                    worker.join();
                }
            }

            workers.clear();
            LOG_INFO(General, "All worker threads terminated successfully");
        }
    }

    [[nodiscard]] size_t GetWorkerCount() const noexcept { 
        return workers.size(); 
    }

    [[nodiscard]] bool IsStopped() const noexcept { 
        return stop.load(std::memory_order_acquire); 
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
};

inline ThreadPool& GetGlobalThreadPool() {
    static ThreadPool s_globalPool;
    return s_globalPool;
}

} // namespace Folio