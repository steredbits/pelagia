// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// PS5 specifics of the platform layer: the rest is shared
// (platform/sdl: rendering, audio, input; platform/posix: files).
// Application installed as a websrv homebrew (finding on the console):
//   /data/homebrew/Pelagia/eboot.elf, sce_sys/icon0.png  (uploaded over FTP)
//     or /mnt/usbN/homebrew/Pelagia/ (exFAT USB stick, websrv launcher)
//   pelagia.conf (optional, server=...): read from the USB stick then from
//     /data/homebrew/Pelagia/; never written
//   /data/homebrew/Pelagia/data/                        (session, token : 0600)
//   /data/homebrew/Pelagia/data/cache/                  (affiches)
//   /data/homebrew/Pelagia/logs/pelagia.log            (+ .1.log, .2.log)
// Everything that is written goes under /data, even when launched from the stick (ps5_paths.h).
// No call to the PS5 SDK here: SDL (PacBrew) takes care of it.

#include "platform.h"
#include "ps5_paths.h"
#include "sdl_hooks.h"
#include "util/log.h"
#include "util/log_file.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace {

constexpr int kLogFilesKept = 3;  // current run + 2 previous ones

util::LogFile g_log_file;

}  // namespace

namespace platform_sdl {

// Console screen: always full screen 1920x1080 (only mode of the
// PS5 SDL driver; the requested window size makes no sense here).
platform::Config adjust_config(const platform::Config& requested) {
  platform::Config config = requested;
  config.window_width = platform::kLogicalWidth;
  config.window_height = platform::kLogicalHeight;
  config.fullscreen = true;
  return config;
}

// A single keyboard: the application's own. SDL_StartTextInput would also open
// the console's system keyboard (see text_input.h).
bool system_text_input() { return false; }

// End of the process. Finding on the console: after "Pelagia finished",
// back to the menu with a crash error although main had finished (the log
// stops there, a minimal payload closed cleanly). No reproducible
// defect on Linux (ASan/UBSan, all threads joined, no
// leaks), so the crash comes from what runs after main: static
// destructors, atexit handlers (OpenSSL of curl/ffmpeg, libc++) or process
// teardown. Measure: logs flushed, file closed, then _Exit, which skips these
// steps. If the error persists despite this, the last "Shutdown: ..." line of the
// log tells where to look.
void finish_process(int status) {
  LOG_INFO("Shutdown: end of main, direct exit (_Exit %d)", status);
  std::fflush(stdout);
  g_log_file.close();
  std::fflush(nullptr);
  std::_Exit(status);
}

// Logs: stdout (relayed by websrv with /hbldr?pipe=1, or displayed by the
// launcher page) and a file in logs/, readable afterwards over FTP.
void before_init() {
  // Defense in depth: no SDL on-screen keyboard, even if some code
  // appelait SDL_StartTextInput.
  SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
  if (g_log_file.is_open()) {
    return;
  }
  const std::string dir = ps5_paths::log_dir();
  const bool ok = platform::make_dirs(dir) && g_log_file.open(dir, "pelagia", kLogFilesKept);
  util::log_install_console_and_file(stdout, ok ? &g_log_file : nullptr);
  char cwd[256] = "";
  if (!getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
  LOG_INFO("Dossier courant : %s", cwd[0] ? cwd : "(unknown)");  // launch diagnostics
  if (ok) {
    LOG_INFO("Logs: stdout and %s", g_log_file.path().c_str());
  } else {
    LOG_WARN("Logs: cannot create a file in %s, stdout only", dir.c_str());
  }
}

}  // namespace platform_sdl

namespace platform {

std::string config_dir() { return ps5_paths::config_dir(); }
std::string cache_dir() { return ps5_paths::cache_dir(); }
std::vector<std::string> preconfig_files() { return ps5_paths::preconfig_files(); }

}  // namespace platform
