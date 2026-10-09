// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Tests of the bounded queue: limits, serial, abort (no blocking), flush.

#include "player/bounded_queue.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "test_framework.h"

using player::BoundedQueue;

static void test_limits() {
  BoundedQueue<int> q(2, 1000);
  CHECK(q.try_push(1, 10, 0));
  CHECK(q.try_push(2, 10, 0));
  CHECK(!q.try_push(3, 10, 0));  // limit on the count

  BoundedQueue<int> qb(100, 10);
  CHECK(qb.try_push(1, 6, 0));
  CHECK(!qb.try_push(2, 6, 0));  // limit in bytes
  BoundedQueue<int>::Entry e;
  CHECK(qb.pop(&e));
  CHECK_EQ(e.item, 1);
  CHECK_EQ(e.bytes, 6u);
  CHECK(qb.try_push(2, 6, 0));

  // An entry bigger than the limit passes if the queue is empty
  // (otherwise the pipeline would block forever).
  BoundedQueue<int> qo(4, 10);
  CHECK(qo.try_push(1, 100, 0));
  CHECK(!qo.try_push(2, 1, 0));
}

static void test_serial_passthrough() {
  BoundedQueue<int> q(4, 1000);
  q.try_push(42, 1, 7);
  BoundedQueue<int>::Entry e;
  CHECK(q.pop(&e));
  CHECK_EQ(e.item, 42);
  CHECK_EQ(e.serial, 7);
}

static void test_abort_unblocks_push() {
  BoundedQueue<int> q(1, 1000);
  CHECK(q.try_push(1, 1, 0));
  std::atomic<bool> returned{false};
  std::atomic<bool> push_result{true};
  std::thread t([&] {
    push_result = q.push(2, 1, 0);  // file pleine : bloque
    returned = true;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(!returned.load());
  q.abort();
  t.join();
  CHECK(returned.load());
  CHECK(!push_result.load());
}

static void test_abort_unblocks_pop() {
  BoundedQueue<int> q(4, 1000);
  std::atomic<bool> returned{false};
  std::atomic<bool> pop_result{true};
  std::thread t([&] {
    BoundedQueue<int>::Entry e;
    pop_result = q.pop(&e);  // file vide : bloque
    returned = true;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(!returned.load());
  q.abort();
  t.join();
  CHECK(returned.load());
  CHECK(!pop_result.load());
}

static void test_flush_releases_and_unblocks() {
  BoundedQueue<int> q(2, 1000);
  q.try_push(1, 1, 0);
  q.try_push(2, 1, 0);
  std::atomic<bool> pushed{false};
  std::thread t([&] {
    q.push(3, 1, 1);  // blocks until the flush
    pushed = true;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  int released = 0;
  q.flush([&](BoundedQueue<int>::Entry*) { ++released; });
  t.join();
  CHECK(pushed.load());
  CHECK_EQ(released, 2);
  CHECK_EQ(q.size(), 1u);  // the entry pushed after the flush
}

static void test_stress_producer_consumer() {
  BoundedQueue<int> q(8, 1u << 20);
  const int kCount = 20000;
  long long sum = 0;
  std::thread producer([&] {
    for (int i = 0; i < kCount; ++i) {
      q.push(i, 4, 0);
    }
    q.push(-1, 0, 0);  // fin
  });
  std::thread consumer([&] {
    BoundedQueue<int>::Entry e;
    while (q.pop(&e) && e.item != -1) {
      sum += e.item;
    }
  });
  producer.join();
  consumer.join();
  CHECK_EQ(sum, static_cast<long long>(kCount) * (kCount - 1) / 2);
  CHECK_EQ(q.size(), 0u);
}

// Duration totals (reserve) and adjustable limits.
static void test_duration_and_limits() {
  BoundedQueue<int> q(2, 1000);
  CHECK(q.try_push(1, 1, 0, 40));
  CHECK(q.try_push(2, 1, 0, 21));
  CHECK_EQ(q.duration_ms(), 61);
  CHECK(!q.try_push(3, 1, 0, 40));  // limit on the count
  q.set_limits(10, 1000);           // deeper queue (network)
  CHECK(q.try_push(3, 1, 0, 40));
  CHECK_EQ(q.duration_ms(), 101);
  BoundedQueue<int>::Entry e;
  CHECK(q.pop(&e));
  CHECK_EQ(e.duration_ms, 40);
  CHECK_EQ(q.duration_ms(), 61);
  q.flush([](BoundedQueue<int>::Entry*) {});
  CHECK_EQ(q.duration_ms(), 0);
  CHECK(q.try_push(4, 1, 1));       // default duration: 0
  CHECK_EQ(q.duration_ms(), 0);
}

int main() {
  test_limits();
  test_duration_and_limits();
  test_serial_passthrough();
  test_abort_unblocks_push();
  test_abort_unblocks_pop();
  test_flush_releases_and_unblocks();
  test_stress_producer_consumer();
  return testfw::test_failures();
}
