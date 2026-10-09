// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "player/buffering.h"

namespace player {

void BufferingGate::reset_for_start() {
  state_ = policy_.enabled() ? State::Priming : State::Playing;
}

bool BufferingGate::update(int64_t buffered_ms, bool input_done, bool queues_full,
                           BufferingEvent* event) {
  *event = BufferingEvent::None;
  if (!policy_.enabled()) {
    state_ = State::Playing;
    return false;
  }
  const bool was_buffering = buffering();
  const bool ready_anyway = input_done || queues_full;

  switch (state_) {
    case State::Priming:
      if (ready_anyway || buffered_ms >= policy_.start_ms) {
        state_ = State::Playing;
      }
      break;
    case State::Playing:
      if (!ready_anyway && buffered_ms < policy_.low_water_ms) {
        state_ = State::Rebuffering;
      }
      break;
    case State::Rebuffering:
      if (ready_anyway || buffered_ms >= policy_.resume_ms) {
        state_ = State::Playing;
      }
      break;
  }

  if (!was_buffering && buffering()) {
    *event = BufferingEvent::Started;
  } else if (was_buffering && !buffering()) {
    *event = BufferingEvent::Finished;
  }
  return buffering();
}

}  // namespace player
