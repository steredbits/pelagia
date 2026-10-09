// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "player/av_sync.h"

namespace player {

FrameAction decide_frame(int64_t frame_pts_ms, int64_t clock_ms,
                         const SyncPolicy& policy, int64_t* wait_ms) {
  if (wait_ms) {
    *wait_ms = 0;
  }
  if (clock_ms < 0) {
    return FrameAction::Present;
  }
  const int64_t drift = frame_pts_ms - clock_ms;
  if (drift > policy.present_early_ms) {
    if (wait_ms) {
      *wait_ms = drift - policy.present_early_ms;
    }
    return FrameAction::Wait;
  }
  if (drift < -policy.drop_late_ms) {
    return FrameAction::Drop;
  }
  return FrameAction::Present;
}

int64_t clamp_seek_target(int64_t target_ms, int64_t duration_ms) {
  if (target_ms < 0) {
    return 0;
  }
  if (duration_ms > 0 && target_ms > duration_ms) {
    return duration_ms;
  }
  return target_ms;
}

}  // namespace player
