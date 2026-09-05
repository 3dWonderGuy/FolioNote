#pragma once
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

namespace Folio {

/**
 * @brief Lightweight, robust thread pool for asynchronous background I/O, compression, and worker tasks.
 */
class ThreadPool {
public:
    explicit ThreadPool(size_t threads = std::max(2u, std::thread::hardware_concurrency() > 2 ? std::thread::hardware_concurrency() - 1 : 2u))
        : stop(false) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queueMutex);
                        this->condition.wait(lock, [this] {
                            return this->stop.load() || !this->tasks.empty();
                        });

                        if (this->stop.load() && this->tasks.empty()) {
                            return;
                        }

                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
        }
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
            if (stop.load()) {
                throw std::runtime_error("Enqueue on stopped ThreadPool");
            }
            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    void EnqueueDetached(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            if (stop.load()) return;
            tasks.push(std::move(task));
        }
        condition.notify_one();
    }

    ~ThreadPool() {
        Shutdown();
    }

    void Shutdown() {
        bool expected = false;
        if (stop.compare_exchange_strong(expected, true)) {
            condition.notify_all();
            for (std::thread& worker : workers) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
            workers.clear();
        }
    }

    [[nodiscard]] size_t GetWorkerCount() const noexcept { return workers.size(); }
    [[nodiscard]] bool IsStopped() const noexcept { return stop.load(); }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
};

// Global default thread pool instance for app-wide async tasks
inline ThreadPool& GetGlobalThreadPool() {
    static ThreadPool s_globalPool;
    return s_globalPool;
}

} // namespace Folio
