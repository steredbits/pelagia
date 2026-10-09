// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Tests of the open gamepad tracking (all accepted, layout per
// gamepad): case of the PS5 SDL driver, one joystick per logged-in user.

#include "controller_set.h"

#include "test_framework.h"

using Set = platform_sdl::ControllerSet<int>;

int main() {
  // Two gamepads (two users logged in on PS5): both are kept.
  Set set;
  CHECK(set.add(3, 30, true));
  CHECK(set.add(7, 70, false));
  CHECK_EQ(set.size(), 2u);
  CHECK(set.contains(3) && set.contains(7));

  // Layout specific to each gamepad; unknown = standard layout.
  CHECK(set.playstation(3));
  CHECK(!set.playstation(7));
  CHECK(!set.playstation(42));

  // "Added" event for an already open gamepad: ignored.
  CHECK(!set.add(3, 99, false));
  CHECK_EQ(set.size(), 2u);
  CHECK(set.playstation(3));

  // Removal: the right handle is returned, the other gamepad stays active.
  int handle = 0;
  CHECK(set.remove(3, &handle));
  CHECK_EQ(handle, 30);
  CHECK(!set.contains(3));
  CHECK(set.contains(7));
  CHECK_EQ(set.size(), 1u);

  // Removal of an unknown or already removed gamepad: no effect.
  handle = -1;
  CHECK(!set.remove(3, &handle));
  CHECK_EQ(handle, -1);
  CHECK(!set.remove(42, &handle));

  // Reconnection (new SDL instance id): accepted.
  CHECK(set.add(8, 80, true));
  CHECK_EQ(set.size(), 2u);
  CHECK_EQ(set.entries()[1].handle, 80);

  set.clear();
  CHECK_EQ(set.size(), 0u);
  CHECK(!set.contains(7));

  return testfw::test_failures();
}
