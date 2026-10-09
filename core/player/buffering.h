// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_PLAYER_BUFFERING_H
#define PELAGIA_CORE_PLAYER_BUFFERING_H

// Buffering decision (network source). Pure logic, no shared
// state or dependency: directly testable.
//
//   Start   --(reserve >= start_ms)-->   Playing
//   Playing --(reserve <  low_water_ms)--> Rebuffering
//   Rebuffering --(reserve >= resume_ms)--> Playing
//
// The resume threshold is higher than the entry one (hysteresis): a
// barely sufficient connection does not alternate playback and waiting
// every second. The end of the source (nothing more to wait for) and full
// queues (nothing more can be added) always mean "ready".

#include <cstdint>

namespace player {

struct BufferingPolicy {
  int64_t start_ms = 0;      // reserve required before starting / after a seek
  int64_t resume_ms = 0;     // reserve required to resume after a shortage
  int64_t low_water_ms = 0;  // below this, playback is suspended

  // All zero (default): no buffering (local file).
  bool enabled() const { return start_ms > 0 || resume_ms > 0 || low_water_ms > 0; }

  // Default network values of the player.
  static BufferingPolicy network() {
    BufferingPolicy p;
    p.start_ms = 2000;
    p.resume_ms = 4000;
    p.low_water_ms = 250;
    return p;
  }
};

enum class BufferingEvent { None, Started, Finished };

class BufferingGate {
 public:
  explicit BufferingGate(const BufferingPolicy& policy = BufferingPolicy())
      : policy_(policy) {}

  void set_policy(const BufferingPolicy& policy) { policy_ = policy; }

  // Opening or seek: playback waits for the start reserve.
  void reset_for_start();

  // buffered_ms: available reserve; input_done: the source is finished;
  // queues_full: cannot buffer more. Returns true if
  // playback must stay suspended. *event signals a transition.
  bool update(int64_t buffered_ms, bool input_done, bool queues_full,
              BufferingEvent* event);

  bool buffering() const { return state_ != State::Playing; }
  bool priming() const { return state_ == State::Priming; }

 private:
  enum class State { Priming, Playing, Rebuffering };

  BufferingPolicy policy_;
  State state_ = State::Playing;
};

}  // namespace player

#endif  // PELAGIA_CORE_PLAYER_BUFFERING_H
