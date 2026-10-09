// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "ui/tasks.h"

#include "util/log.h"

namespace ui {

ThreadPool::ThreadPool(int threads, std::string name) : name_(std::move(name)) {
  for (int i = 0; i < threads; ++i) {
    threads_.emplace_back(&ThreadPool::run, this);
  }
}

ThreadPool::~ThreadPool() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
    if (!queue_.empty()) {
      LOG_DEBUG("%s: %zu task(s) dropped at shutdown", name_.c_str(), queue_.size());
    }
    queue_.clear();
  }
  cv_.notify_all();
  for (std::thread& t : threads_) {
    t.join();
  }
}

void ThreadPool::post(Task task) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (quit_) {
      return;
    }
    queue_.push_back(std::move(task));
  }
  cv_.notify_one();
}

size_t ThreadPool::pending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size() + running_;
}

void ThreadPool::run() {
  for (;;) {
    Task task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return quit_ || !queue_.empty(); });
      if (quit_) {
        return;
      }
      task = std::move(queue_.front());
      queue_.pop_front();
      ++running_;
    }
    task();
    std::lock_guard<std::mutex> lock(mutex_);
    --running_;
  }
}

void ManualRunner::post(Task task) {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.push_back(std::move(task));
}

size_t ManualRunner::pending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

size_t ManualRunner::run_all() {
  size_t count = 0;
  for (;;) {
    Task task;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (queue_.empty()) {
        return count;
      }
      task = std::move(queue_.front());
      queue_.pop_front();
    }
    task();
    ++count;
  }
}

size_t ManualRunner::run_all_reversed() {
  std::deque<Task> batch;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    batch.swap(queue_);
  }
  for (auto it = batch.rbegin(); it != batch.rend(); ++it) {
    (*it)();
  }
  return batch.size() + run_all();
}

void ManualRunner::drop_all() {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.clear();
}

void MainQueue::post(Task task) {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.push_back(std::move(task));
}

size_t MainQueue::drain() {
  std::deque<Task> ready;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ready.swap(queue_);
  }
  for (Task& task : ready) {
    task();
  }
  return ready.size();
}

size_t MainQueue::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

}  // namespace ui
