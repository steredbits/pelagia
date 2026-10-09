// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Tests of the buffering decision (BufferingGate), without
// network or ffmpeg.

#include "player/buffering.h"

#include "test_framework.h"

using player::BufferingEvent;
using player::BufferingGate;
using player::BufferingPolicy;

static BufferingPolicy policy() {
  BufferingPolicy p;
  p.start_ms = 2000;
  p.resume_ms = 4000;
  p.low_water_ms = 250;
  return p;
}

// Disabled (local file): never suspended.
static void test_disabled() {
  BufferingGate gate;
  gate.reset_for_start();
  BufferingEvent ev;
  CHECK(!gate.update(0, false, false, &ev));
  CHECK(ev == BufferingEvent::None);
  CHECK(!gate.buffering());
  CHECK(!BufferingPolicy().enabled());
  CHECK(BufferingPolicy::network().enabled());
}

// Start: wait for the start reserve, no more.
static void test_priming() {
  BufferingGate gate(policy());
  gate.reset_for_start();
  BufferingEvent ev;
  CHECK(gate.buffering() && gate.priming());
  CHECK(gate.update(0, false, false, &ev));
  CHECK(gate.update(1999, false, false, &ev));
  CHECK(!gate.update(2000, false, false, &ev));
  CHECK(ev == BufferingEvent::Finished);
  CHECK(!gate.buffering());
}

// Shortage during playback, then resume with hysteresis.
static void test_rebuffering_hysteresis() {
  BufferingGate gate(policy());
  BufferingEvent ev;
  CHECK(!gate.update(5000, false, false, &ev));  // playing
  CHECK(!gate.update(250, false, false, &ev));   // at the threshold: we continue
  CHECK(gate.update(249, false, false, &ev));    // below the threshold: suspension
  CHECK(ev == BufferingEvent::Started);
  CHECK(!gate.priming());
  // The start reserve is not enough to resume after a shortage.
  CHECK(gate.update(2000, false, false, &ev));
  CHECK(ev == BufferingEvent::None);
  CHECK(gate.update(3999, false, false, &ev));
  CHECK(!gate.update(4000, false, false, &ev));
  CHECK(ev == BufferingEvent::Finished);
  // A drop above the low threshold does not suspend (no oscillation).
  CHECK(!gate.update(300, false, false, &ev));
}

// End of the source or full queues: always ready (otherwise a block).
static void test_ready_anyway() {
  BufferingGate gate(policy());
  BufferingEvent ev;
  gate.reset_for_start();
  CHECK(!gate.update(100, true, false, &ev));  // end of stream: nothing to wait for
  CHECK(ev == BufferingEvent::Finished);
  CHECK(!gate.update(0, true, false, &ev));    // end of the movie: no shortage
  gate.reset_for_start();
  CHECK(!gate.update(500, false, true, &ev));  // queues full before the threshold
  CHECK(gate.update(0, false, false, &ev));    // manque
  CHECK(!gate.update(1000, false, true, &ev)); // files pleines : reprise
}

// Seek: back to the start state even in the middle of playback.
static void test_reset_on_seek() {
  BufferingGate gate(policy());
  BufferingEvent ev;
  CHECK(!gate.update(5000, false, false, &ev));
  gate.reset_for_start();
  CHECK(gate.priming());
  CHECK(gate.update(1000, false, false, &ev));
  CHECK(!gate.update(2500, false, false, &ev));
}

int main() {
  test_disabled();
  test_priming();
  test_rebuffering_hysteresis();
  test_ready_anyway();
  test_reset_on_seek();
  return testfw::test_failures();
}
