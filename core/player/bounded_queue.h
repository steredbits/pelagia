// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_PLAYER_BOUNDED_QUEUE_H
#define PELAGIA_CORE_PLAYER_BOUNDED_QUEUE_H

// Thread-safe bounded queue (limited in number of entries and in bytes), with
// a generation number ("serial") per entry: on seek, the global generation
// changes and consumers drop stale entries.
// No ffmpeg or platform dependency: entries are released by
// the caller (flush/drain), which makes the queue testable on its own.
// Each entry may carry a media duration: the queue keeps the total, which
// measures the available reserve (network buffering).

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

namespace player {

template <typename T>
class BoundedQueue {
 public:
  struct Entry {
    T item;
    size_t bytes;
    int serial;
    int64_t duration_ms;
  };

  BoundedQueue(size_t max_items, size_t max_bytes)
      : max_items_(max_items), max_bytes_(max_bytes) {}

  // Pushes, blocking while the queue is full.
  // Returns false if abort() was called (the caller keeps ownership).
  bool push(T item, size_t bytes, int serial, int64_t duration_ms = 0) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_full_.wait(lock, [this, bytes] { return aborted_ || has_room(bytes); });
    if (aborted_) {
      return false;
    }
    append(static_cast<T&&>(item), bytes, serial, duration_ms);
    return true;
  }

  // Pushes without blocking. false if full or aborted.
  bool try_push(T item, size_t bytes, int serial, int64_t duration_ms = 0) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (aborted_ || !has_room(bytes)) {
      return false;
    }
    append(static_cast<T&&>(item), bytes, serial, duration_ms);
    return true;
  }

  // Pops, blocking while the queue is empty. false if abort().
  bool pop(Entry* out) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [this] { return aborted_ || !entries_.empty(); });
    if (entries_.empty()) {
      return false;  // aborted and empty
    }
    take_front(out);
    return true;
  }

  // Pops without blocking. false if empty.
  bool try_pop(Entry* out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (entries_.empty()) {
      return false;
    }
    take_front(out);
    return true;
  }

  // Empties the queue, passing each entry to `release` (resources released
  // by the caller: av_packet_free, av_frame_free, ...).
  template <typename ReleaseFn>
  void flush(ReleaseFn release) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Entry& e : entries_) {
      release(&e);
    }
    entries_.clear();
    bytes_ = 0;
    duration_ms_ = 0;
    not_full_.notify_all();
  }

  // Changes the limits (e.g. deeper queues for a network source).
  void set_limits(size_t max_items, size_t max_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_items_ = max_items;
    max_bytes_ = max_bytes;
    not_full_.notify_all();
  }

  // Unblocks all waiting push/pop calls, permanently.
  void abort() {
    std::lock_guard<std::mutex> lock(mutex_);
    aborted_ = true;
    not_full_.notify_all();
    not_empty_.notify_all();
  }

  bool aborted() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return aborted_;
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
  }

  size_t bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bytes_;
  }

  // Accumulated media duration of the entries present (ms).
  int64_t duration_ms() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return duration_ms_;
  }

  bool full() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !has_room(0);
  }

 private:
  // An entry bigger than max_bytes_ is accepted if the queue is empty,
  // otherwise the pipeline would block on it forever.
  bool has_room(size_t incoming_bytes) const {
    if (entries_.empty()) {
      return true;
    }
    return entries_.size() < max_items_ && bytes_ + incoming_bytes <= max_bytes_;
  }

  void append(T&& item, size_t bytes, int serial, int64_t duration_ms) {
    entries_.push_back(Entry{static_cast<T&&>(item), bytes, serial, duration_ms});
    bytes_ += bytes;
    duration_ms_ += duration_ms;
    not_empty_.notify_one();
  }

  void take_front(Entry* out) {
    *out = static_cast<Entry&&>(entries_.front());
    entries_.pop_front();
    bytes_ -= out->bytes;
    duration_ms_ -= out->duration_ms;
    not_full_.notify_one();
  }

  size_t max_items_;
  size_t max_bytes_;
  mutable std::mutex mutex_;
  std::condition_variable not_full_;
  std::condition_variable not_empty_;
  std::deque<Entry> entries_;
  size_t bytes_ = 0;
  int64_t duration_ms_ = 0;
  bool aborted_ = false;
};

}  // namespace player

#endif  // PELAGIA_CORE_PLAYER_BOUNDED_QUEUE_H
