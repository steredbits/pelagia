// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Tests of the presentation decision (A/V sync) and seek clamping.

#include "player/av_sync.h"

#include "test_framework.h"

using player::clamp_seek_target;
using player::decide_frame;
using player::FrameAction;
using player::SyncPolicy;

int main() {
  const SyncPolicy policy;  // early=15 ms, late=80 ms

  // Clock not set: we present (first frame).
  CHECK(decide_frame(1000, -1, policy, nullptr) == FrameAction::Present);

  // Within the window: presentation.
  CHECK(decide_frame(1000, 1000, policy, nullptr) == FrameAction::Present);
  CHECK(decide_frame(1015, 1000, policy, nullptr) == FrameAction::Present);
  CHECK(decide_frame(920, 1000, policy, nullptr) == FrameAction::Present);

  // Too early: wait, with a suggested delay.
  int64_t wait = 0;
  CHECK(decide_frame(1016, 1000, policy, &wait) == FrameAction::Wait);
  CHECK_EQ(wait, 1);
  CHECK(decide_frame(1500, 1000, policy, &wait) == FrameAction::Wait);
  CHECK_EQ(wait, 485);

  // Trop tard : abandon.
  CHECK(decide_frame(919, 1000, policy, nullptr) == FrameAction::Drop);
  CHECK(decide_frame(0, 10000, policy, nullptr) == FrameAction::Drop);

  // Seek clamped to [0, duration].
  CHECK_EQ(clamp_seek_target(-100, 60000), 0);
  CHECK_EQ(clamp_seek_target(0, 60000), 0);
  CHECK_EQ(clamp_seek_target(30000, 60000), 30000);
  CHECK_EQ(clamp_seek_target(999999, 60000), 60000);
  // Unknown duration: clamped on the left only.
  CHECK_EQ(clamp_seek_target(-5, 0), 0);
  CHECK_EQ(clamp_seek_target(123456, 0), 123456);

  return testfw::test_failures();
}
