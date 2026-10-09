// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "player/retry_policy.h"

namespace player {

int RetryPolicy::delay_ms(int attempt) const {
  if (attempt < 1 || attempt > max_attempts) {
    return -1;
  }
  long long delay = initial_delay_ms;
  for (int i = 1; i < attempt && delay < max_delay_ms; ++i) {
    delay *= 2;
  }
  return delay > max_delay_ms ? max_delay_ms : static_cast<int>(delay);
}

}  // namespace player
