// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "player/clock.h"

namespace player {

void MasterClock::init(TimeFn fn, void* ctx) {
  time_fn_ = fn;
  time_ctx_ = ctx;
  valid_ = false;
  paused_ = false;
}

uint64_t MasterClock::mono_now() const {
  return time_fn_ ? time_fn_(time_ctx_) : 0;
}

void MasterClock::set(int64_t media_ms) {
  base_media_ms_ = media_ms;
  base_mono_ms_ = mono_now();
  valid_ = true;
}

int64_t MasterClock::now() const {
  if (!valid_) {
    return -1;
  }
  if (paused_) {
    return base_media_ms_;
  }
  return base_media_ms_ + static_cast<int64_t>(mono_now() - base_mono_ms_);
}

void MasterClock::pause(bool paused) {
  if (paused == paused_) {
    return;
  }
  // Freezes the current position before changing state, so that now()
  // restarts from the right point on resume.
  if (valid_) {
    base_media_ms_ = now();
    base_mono_ms_ = mono_now();
  }
  paused_ = paused;
}

void MasterClock::invalidate() {
  valid_ = false;
}

}  // namespace player
