// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Text input policy of the SDL layer: a single keyboard on PS5.

#include "text_input.h"

#include "test_framework.h"

using platform_sdl::should_start_sdl_text_input;

int main() {
  // Linux: the physical keyboard types, SDL_StartTextInput is called.
  CHECK(should_start_sdl_text_input(true, true));
  CHECK(!should_start_sdl_text_input(false, true));

  // PS5: never, otherwise the system keyboard opens on top of the on-screen keyboard.
  CHECK(!should_start_sdl_text_input(true, false));
  CHECK(!should_start_sdl_text_input(false, false));
  return testfw::test_failures();
}
