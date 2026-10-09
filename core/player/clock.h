// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_PLAYER_CLOCK_H
#define PELAGIA_CORE_PLAYER_CLOCK_H

// Master clock of the player. It is set by the audio (or free-running
// if there is no audio track) and interpolates between two settings with an
// injected monotonic time - never platform::ticks_ms() directly, to
// stay testable with a simulated clock.

#include <cstdint>

namespace player {

// Monotonic time in milliseconds. `ctx` is opaque (no std::function:
// a function pointer is enough and stays portable without exceptions).
using TimeFn = uint64_t (*)(void* ctx);

class MasterClock {
 public:
  void init(TimeFn fn, void* ctx);

  // Sets the clock: "at this instant, the media position is media_ms".
  void set(int64_t media_ms);

  // Current media position in ms, or -1 if the clock is not set
  // (before the first frame, or just after a seek).
  int64_t now() const;

  bool valid() const { return valid_; }

  // Freezes / releases the clock. The position does not advance during pause.
  void pause(bool paused);
  bool paused() const { return paused_; }

  // Shifts the clock (after a seek): no longer valid until set() is
  // called again with the first position of the new generation.
  void invalidate();

 private:
  uint64_t mono_now() const;

  TimeFn time_fn_ = nullptr;
  void* time_ctx_ = nullptr;
  int64_t base_media_ms_ = 0;
  uint64_t base_mono_ms_ = 0;
  bool valid_ = false;
  bool paused_ = false;
};

}  // namespace player

#endif  // PELAGIA_CORE_PLAYER_CLOCK_H
