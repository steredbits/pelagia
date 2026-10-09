// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_PLATFORM_SDL_TEXT_INPUT_H
#define PELAGIA_PLATFORM_SDL_TEXT_INPUT_H

// Text input: who provides the keyboard? Pure logic (no SDL), tested.
//
// Linux: the physical keyboard types directly (SDL_TEXTINPUT) in addition to the
// application's on-screen keyboard.
// PS5: SDL_StartTextInput opens the console's system keyboard (IME,
// sceImeDialog) on top of the application's on-screen keyboard, hence two
// keyboards in a row. The PS5 SDL driver (SDL_ps5keyboard) converts the typed
// text with wcstombs, return value ignored and without explicit UTF-16
// conversion: accents are not guaranteed. The application only uses
// its on-screen keyboard there, and never starts SDL's IME.

namespace platform_sdl {

// True if SDL_StartTextInput must be called when the application wants
// text input. `system_text_input`: the platform provides a reliable
// system keyboard (sdl_hooks.h).
inline bool should_start_sdl_text_input(bool app_wants_text, bool system_text_input) {
  return app_wants_text && system_text_input;
}

}  // namespace platform_sdl

#endif  // PELAGIA_PLATFORM_SDL_TEXT_INPUT_H
