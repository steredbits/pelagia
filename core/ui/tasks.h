// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UI_TASKS_H
#define PELAGIA_CORE_UI_TASKS_H

// UI asynchrony: no network, disk or decoding operation runs in the
// display loop.
//
//   display loop --post()--> TaskRunner (worker threads)
//          ^                                  |
//          +------ MainQueue::drain() <--post-+ (result to apply)
//
// Results are applied on the main thread, at the start of each
// frame: the UI state is never touched by two threads. A result
// arriving after the screen that requested it was closed is ignored thanks to a
// lifetime token (Lifetime), checked on the main thread.

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ui {

using Task = std::function<void()>;

// Runs tasks outside the main thread.
class TaskRunner {
 public:
  virtual ~TaskRunner() {}
  virtual void post(Task task) = 0;
  // Tasks waiting or being executed.
  virtual size_t pending() const = 0;
};

// Thread pool; on destruction, tasks not yet started are
// dropped and running tasks are finished (join).
class ThreadPool final : public TaskRunner {
 public:
  ThreadPool(int threads, std::string name);
  ~ThreadPool() override;
  void post(Task task) override;
  size_t pending() const override;

 private:
  void run();

  std::string name_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Task> queue_;
  size_t running_ = 0;
  bool quit_ = false;
  std::vector<std::thread> threads_;
};

// Manual deterministic execution (tests): run_all() runs the tasks
// on the calling thread, including those posted during execution.
class ManualRunner final : public TaskRunner {
 public:
  void post(Task task) override;
  size_t pending() const override;
  size_t run_all();
  // Runs the pending tasks from the most recent to the oldest
  // (out-of-order answers), then those posted meanwhile, in order.
  size_t run_all_reversed();
  // Drops the pending tasks (simulates a screen closed before execution).
  void drop_all();

 private:
  mutable std::mutex mutex_;
  std::deque<Task> queue_;
};

// Results to apply on the main thread. post() is thread-safe;
// drain() is only called from the main thread.
class MainQueue {
 public:
  void post(Task task);
  // Runs the tasks that arrived so far (not those posted meanwhile).
  size_t drain();
  size_t size() const;

 private:
  mutable std::mutex mutex_;
  std::deque<Task> queue_;
};

// Lifetime token: expires when its owner (a screen) is destroyed. An
// asynchronous result checks alive() on the main thread before
// touching its target.
class Lifetime {
 public:
  using Token = std::shared_ptr<const std::atomic<bool>>;
  Lifetime() : alive_(std::make_shared<std::atomic<bool>>(true)) {}
  ~Lifetime() { *alive_ = false; }
  Lifetime(const Lifetime&) = delete;
  Lifetime& operator=(const Lifetime&) = delete;
  Token token() const { return alive_; }
  static bool alive(const Token& token) { return token && token->load(); }

 private:
  std::shared_ptr<std::atomic<bool>> alive_;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_TASKS_H
