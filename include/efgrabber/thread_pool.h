#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>
#include <memory>

namespace efgrabber {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    // Non-copyable, non-movable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Submit a task and get a future for the result
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))>;

    // Submit a task without caring about the result
    void submit_detached(std::function<void()> task);

    // Get pool status
    size_t queue_size() const;
    size_t active_tasks() const { return active_tasks_.load(); }
    size_t thread_count() const { return workers_.size(); }
    bool is_running() const { return !stop_.load(); }

    // Wait for all tasks to complete
    void wait_all();

    // Stop accepting new tasks and finish current ones
    void shutdown();

private:
    void worker_thread();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;

    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::condition_variable completion_condition_;

    std::atomic<bool> stop_{false};
    std::atomic<size_t> active_tasks_{0};
    std::atomic<size_t> total_tasks_{0};
    std::atomic<size_t> completed_tasks_{0};
};

// Template implementation
template<typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
    using return_type = decltype(f(args...));

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> result = task->get_future();

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_) {
            throw std::runtime_error("Cannot submit to stopped thread pool");
        }
        tasks_.emplace([task]() { (*task)(); });
        total_tasks_++;
    }

    condition_.notify_one();
    return result;
}

} // namespace efgrabber
