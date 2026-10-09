// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Tests of the retry schedule after a network drop.

#include "player/retry_policy.h"

#include "test_framework.h"

using player::RetryPolicy;

int main() {
  // Disabled by default (local file).
  RetryPolicy none;
  CHECK(!none.enabled());
  CHECK_EQ(none.delay_ms(1), -1);

  // Player: 1, 2, 4, 8 then 15 s (cap), 5 attempts, ~30 s in total.
  const RetryPolicy net = RetryPolicy::network();
  CHECK(net.enabled());
  CHECK_EQ(net.max_attempts, 5);
  CHECK_EQ(net.delay_ms(1), 1000);
  CHECK_EQ(net.delay_ms(2), 2000);
  CHECK_EQ(net.delay_ms(3), 4000);
  CHECK_EQ(net.delay_ms(4), 8000);
  CHECK_EQ(net.delay_ms(5), 15000);
  CHECK_EQ(net.delay_ms(6), -1);
  CHECK_EQ(net.delay_ms(0), -1);

  // Cap reached then kept, without overflow.
  RetryPolicy long_run;
  long_run.max_attempts = 40;
  long_run.initial_delay_ms = 1000;
  long_run.max_delay_ms = 15000;
  CHECK_EQ(long_run.delay_ms(40), 15000);

  // Reduced values of the automated tests.
  RetryPolicy fast;
  fast.max_attempts = 5;
  fast.initial_delay_ms = 100;
  fast.max_delay_ms = 1600;
  CHECK_EQ(fast.delay_ms(5), 1600);

  return testfw::test_failures();
}
