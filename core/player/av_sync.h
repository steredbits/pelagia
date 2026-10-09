// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_PLAYER_AV_SYNC_H
#define PELAGIA_CORE_PLAYER_AV_SYNC_H

// Decision on presenting a video frame relative to the master clock.
// Pure functions, no state or dependency: directly testable.

#include <cstdint>

namespace player {

enum class FrameAction {
  Present,  // afficher maintenant
  Wait,     // too early: wait
  Drop,     // too late: drop the frame
};

struct SyncPolicy {
  int64_t present_early_ms = 15;  // tolerated lead margin to display
  int64_t drop_late_ms = 80;      // lateness beyond which the frame is dropped
};

// `clock_ms` < 0 means the clock is not set: display (first frame).
// `wait_ms` (optional) receives the suggested delay before retrying in case
// of Wait.
FrameAction decide_frame(int64_t frame_pts_ms, int64_t clock_ms,
                         const SyncPolicy& policy, int64_t* wait_ms);

// Clamps a seek target to [0, duration_ms]. duration_ms <= 0 means
// unknown duration: only clamp on the left.
int64_t clamp_seek_target(int64_t target_ms, int64_t duration_ms);

}  // namespace player

#endif  // PELAGIA_CORE_PLAYER_AV_SYNC_H
