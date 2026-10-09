// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Tests of the master clock with injected (simulated) monotonic time.

#include "player/clock.h"

#include "test_framework.h"

namespace {

struct FakeTime {
  uint64_t now = 0;
  static uint64_t fn(void* ctx) { return static_cast<FakeTime*>(ctx)->now; }
};

}  // namespace

int main() {
  FakeTime t;
  player::MasterClock clock;
  clock.init(&FakeTime::fn, &t);

  // Not set at the start.
  CHECK(!clock.valid());
  CHECK_EQ(clock.now(), -1);

  // Setting then interpolation on the monotonic time.
  t.now = 100;
  clock.set(5000);
  CHECK(clock.valid());
  CHECK_EQ(clock.now(), 5000);
  t.now = 600;
  CHECK_EQ(clock.now(), 5500);

  // Pause: the position freezes, time goes on.
  clock.pause(true);
  CHECK(clock.paused());
  t.now = 2600;
  CHECK_EQ(clock.now(), 5500);

  // Resume: restarts from the frozen point, without a jump.
  clock.pause(false);
  CHECK_EQ(clock.now(), 5500);
  t.now = 2700;
  CHECK_EQ(clock.now(), 5600);

  // Double pause / double resume: no effect.
  clock.pause(false);
  CHECK_EQ(clock.now(), 5600);

  // Re-setting (drift correction by the audio).
  clock.set(9000);
  t.now = 2800;
  CHECK_EQ(clock.now(), 9100);

  // Invalidation (seek): no position until set() is called again.
  clock.invalidate();
  CHECK(!clock.valid());
  CHECK_EQ(clock.now(), -1);
  clock.set(60000);
  CHECK_EQ(clock.now(), 60000);

  return testfw::test_failures();
}
