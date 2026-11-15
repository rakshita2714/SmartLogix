#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// Raised when submit() is called while the bounded waiting queue is full.
class QueueFull : public std::runtime_error {
public:
    QueueFull();
};

// A fixed-size worker pool with a bounded queue of waiting tasks.
// shutdown() stops accepting new tasks, drains accepted tasks, and joins workers.
class ThreadPool {
public:
    ThreadPool(std::size_t workerCount, std::size_t queueCapacity);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template <typename Function, typename... Args>
    auto submit(Function&& function, Args&&... args)
        -> std::future<typename std::invoke_result<Function, Args...>::type> {
        using Result = typename std::invoke_result<Function, Args...>::type;
        auto task = std::make_shared<std::packaged_task<Result()>>(
            std::bind(std::forward<Function>(function), std::forward<Args>(args)...));
        std::future<Result> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (stopping_) {
                throw std::runtime_error("Cannot submit a task to a stopped ThreadPool.");
            }
            if (tasks_.size() >= queueCapacity_) {
                throw QueueFull();
            }
            tasks_.emplace_back([task]() { (*task)(); });
        }
        workAvailable_.notify_one();
        return result;
    }

    void shutdown();

    std::size_t workerCount() const noexcept;
    std::size_t queueCapacity() const noexcept;
    std::size_t queuedTaskCount() const;
    bool acceptingTasks() const;

private:
    void workerLoop();

    const std::size_t queueCapacity_;
    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> tasks_;
    mutable std::mutex queueMutex_;
    std::mutex shutdownMutex_;
    std::condition_variable workAvailable_;
    bool stopping_ = false;
};

#endif
