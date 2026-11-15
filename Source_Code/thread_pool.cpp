#include "thread_pool.h"

QueueFull::QueueFull() : std::runtime_error("ThreadPool task queue is full.") {}

ThreadPool::ThreadPool(std::size_t workerCount, std::size_t queueCapacity)
    : queueCapacity_(queueCapacity) {
    if (workerCount == 0) {
        throw std::invalid_argument("ThreadPool requires at least one worker.");
    }
    if (queueCapacity == 0) {
        throw std::invalid_argument("ThreadPool queue capacity must be positive.");
    }

    workers_.reserve(workerCount);
    try {
        for (std::size_t i = 0; i < workerCount; ++i) {
            workers_.emplace_back([this]() { workerLoop(); });
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            stopping_ = true;
        }
        workAvailable_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        throw;
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::shutdown() {
    // Serializes concurrent shutdown calls so each std::thread is joined once.
    std::lock_guard<std::mutex> shutdownLock(shutdownMutex_);
    {
        std::lock_guard<std::mutex> queueLock(queueMutex_);
        stopping_ = true;
    }
    workAvailable_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

std::size_t ThreadPool::workerCount() const noexcept {
    return workers_.size();
}

std::size_t ThreadPool::queueCapacity() const noexcept {
    return queueCapacity_;
}

std::size_t ThreadPool::queuedTaskCount() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return tasks_.size();
}

bool ThreadPool::acceptingTasks() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return !stopping_;
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            workAvailable_.wait(lock, [this]() { return stopping_ || !tasks_.empty(); });
            if (stopping_ && tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }

        // packaged_task stores user exceptions in its future. This catch also
        // prevents an unexpected wrapper exception from terminating a worker.
        try {
            task();
        } catch (...) {
        }
    }
}
