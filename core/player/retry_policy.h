// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_PLAYER_RETRY_POLICY_H
#define PELAGIA_CORE_PLAYER_RETRY_POLICY_H

// Recovery after a network drop: number of attempts and wait before
// each one (doubled at each failure, capped). Pure logic, tested.

namespace player {

struct RetryPolicy {
  int max_attempts = 0;  // 0 = no recovery (local file)
  int initial_delay_ms = 1000;
  int max_delay_ms = 15000;

  bool enabled() const { return max_attempts > 0; }

  // Wait before attempt n (1..max_attempts), -1 beyond.
  int delay_ms(int attempt) const;

  // Player values: 5 attempts after 1, 2, 4, 8 then 15 s (~30 s).
  static RetryPolicy network() {
    RetryPolicy p;
    p.max_attempts = 5;
    return p;
  }
};

}  // namespace player

#endif  // PELAGIA_CORE_PLAYER_RETRY_POLICY_H
