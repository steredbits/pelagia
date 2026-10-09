// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Linux specifics of the platform layer: the rest is shared
// (platform/sdl for rendering, audio and input; platform/posix for
// files). XDG folders:
//   configuration: $XDG_CONFIG_HOME/pelagia (default ~/.config/pelagia)
//   cache        : $XDG_CACHE_HOME/pelagia  (default ~/.cache/pelagia)
//   optional pre-configuration: <configuration>/pelagia.conf

#include "platform.h"
#include "sdl_hooks.h"

#include <cstdlib>
#include <string>

namespace platform_sdl {

platform::Config adjust_config(const platform::Config& requested) { return requested; }

bool system_text_input() { return true; }

void finish_process(int) {}  // main retourne normalement

void before_init() {}  // logs: stderr (util/log default)

}  // namespace platform_sdl

namespace platform {

namespace {

std::string xdg_dir(const char* variable, const char* fallback) {
  const char* value = std::getenv(variable);
  if (value && value[0] == '/') {
    return std::string(value) + "/pelagia";
  }
  const char* home = std::getenv("HOME");
  return std::string(home && home[0] ? home : "/tmp") + "/" + fallback + "/pelagia";
}

}  // namespace

std::string config_dir() { return xdg_dir("XDG_CONFIG_HOME", ".config"); }
std::string cache_dir() { return xdg_dir("XDG_CACHE_HOME", ".cache"); }
std::vector<std::string> preconfig_files() { return {config_dir() + "/pelagia.conf"}; }

}  // namespace platform
