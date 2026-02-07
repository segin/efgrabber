#include "efgrabber/thread_pool.h"

namespace efgrabber {

ThreadPool::ThreadPool(size_t num_threads) {
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&ThreadPool::worker_thread, this);
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::worker_thread() {
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this] {
                return stop_ || !tasks_.empty();
            });

            if (stop_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        active_tasks_++;
        task();
        active_tasks_--;
        completed_tasks_++;

        completion_condition_.notify_all();
    }
}

void ThreadPool::submit_detached(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_) {
            throw std::runtime_error("Cannot submit to stopped thread pool");
        }
        tasks_.emplace(std::move(task));
        total_tasks_++;
    }
    condition_.notify_one();
}

size_t ThreadPool::queue_size() const {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

void ThreadPool::wait_all() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    completion_condition_.wait(lock, [this] {
        return tasks_.empty() && active_tasks_ == 0;
    });
}

void ThreadPool::shutdown() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_) return;
        stop_ = true;
    }

    condition_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

} // namespace efgrabber
