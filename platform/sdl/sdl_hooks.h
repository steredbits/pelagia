// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_PLATFORM_SDL_HOOKS_H
#define PELAGIA_PLATFORM_SDL_HOOKS_H

// Adaptation points of the shared SDL layer (platform/sdl), provided by
// each platform that uses it (platform/linux, platform/ps5). Everything
// else (rendering, audio, input) is common.

#include "platform.h"

namespace platform_sdl {

// Configuration actually applied, from the one requested by
// the application (Linux: unchanged; PS5: full screen 1920x1080 imposed).
platform::Config adjust_config(const platform::Config& requested);

// True if the system keyboard provides text input (SDL_StartTextInput):
// Linux yes; PS5 no, the application only uses its on-screen keyboard there (see
// text_input.h).
bool system_text_input();

// End of the process (platform::finish_process): Linux does nothing, PS5 exits
// directement.
void finish_process(int status);

// Called at the start of platform::init(), before SDL (PS5: logs to file and
// stdout).
void before_init();

}  // namespace platform_sdl

#endif  // PELAGIA_PLATFORM_SDL_HOOKS_H
