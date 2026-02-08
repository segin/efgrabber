/*
 * src/thread_pool.cpp - Implementation of the thread pool
 * Copyright (c) 2026 Kirn Gill II
 * SPDX-License-Identifier: MIT
 * See LICENSE file for full license text.
 */

#include "efgrabber/thread_pool.h"
#include <iostream>

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
        try {
            task();
        } catch (const std::exception& e) {
            std::cerr << "[ThreadPool] Task exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[ThreadPool] Task unknown exception" << std::endl;
        }
        active_tasks_--;
        completed_tasks_++;

        completion_condition_.notify_all();
    }
}

void ThreadPool::submit_detached(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_) {
            // Silently ignore submissions to stopped pool instead of throwing
            return;
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
