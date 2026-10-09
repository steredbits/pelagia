// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_PLATFORM_PS5_PS5_PATHS_H
#define PELAGIA_PLATFORM_PS5_PS5_PATHS_H

// Pelagia locations on the console (pure functions: tested on Linux).
//
// The application can be launched from /data/homebrew/Pelagia/ (FTP) or from
// a USB stick (/mnt/usbN/homebrew/Pelagia/, websrv launcher). In both cases
// everything that is written (session and token, poster cache, logs) goes under
// /data/homebrew/Pelagia/, never on the stick. Only pelagia.conf is read, in
// both places: the stick first, then /data.

#include <string>
#include <vector>

#include "util/preconfig.h"

namespace ps5_paths {

constexpr const char kAppDir[] = "Pelagia";  // folder name under homebrew/
constexpr const char kDataRoot[] = "/data/homebrew/Pelagia";
// websrv looks for homebrews in /mnt/usb<N>/homebrew/ (N from 0 to 7 here).
constexpr int kUsbSlots = 8;

inline std::string config_dir() { return std::string(kDataRoot) + "/data"; }
inline std::string cache_dir() { return std::string(kDataRoot) + "/data/cache"; }
inline std::string log_dir() { return std::string(kDataRoot) + "/logs"; }

// pelagia.conf files, by priority: USB sticks 0 to 7, then /data.
inline std::vector<std::string> preconfig_files() {
  std::vector<std::string> paths;
  for (int slot = 0; slot < kUsbSlots; ++slot) {
    paths.push_back(util::usb_preconfig_path(slot, kAppDir));
  }
  paths.push_back(std::string(kDataRoot) + "/pelagia.conf");
  return paths;
}

}  // namespace ps5_paths

#endif  // PELAGIA_PLATFORM_PS5_PS5_PATHS_H
