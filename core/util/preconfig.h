// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UTIL_PRECONFIG_H
#define PELAGIA_CORE_UTIL_PRECONFIG_H

// Optional pre-configuration file (pelagia.conf), placed next to the
// application. On PS5, several locations are tried in order: the
// USB stick the application was launched from (/mnt/usbN/homebrew/Pelagia/), then
// /data/homebrew/Pelagia/ (FTP). The file is never written.
// A single key: server=<address>, to avoid typing it on the on-screen
// keyboard at first launch. No credentials: a user, password or
// token key is ignored (with a warning that does not show its value).
//
//   # commentaire
//   server=http://192.168.1.10:8096

#include <string>
#include <vector>

#include "util/file_store.h"

namespace util {

// Server address read from the file content ("" if absent).
std::string preconfig_server(const std::string& text);

// Path of pelagia.conf on USB stick number `slot`:
// /mnt/usb<slot>/homebrew/<app_dir>/pelagia.conf (lanceur websrv).
// (Inline: the platform layer uses it without depending on the link
// order of the core.)
inline std::string usb_preconfig_path(int slot, const std::string& app_dir) {
  return "/mnt/usb" + std::to_string(slot) + "/homebrew/" + app_dir + "/pelagia.conf";
}

// First file of `paths` (in order) that exists and contains a
// server= address; "" if none. `source` receives the chosen path. A missing,
// unreadable file or one without server= moves on to the next.
std::string find_preconfig_server(const std::vector<std::string>& paths, FileStore* files,
                                  std::string* source);

// Address to impose at startup: the command line's if it is
// given; otherwise the file's, but only without a saved session
// (the preset never replaces a session opened on another
// server, otherwise the sign-in screen would come back at every launch).
std::string choose_start_server(const std::string& command_line, bool has_saved_session,
                                const std::string& preconfig);

}  // namespace util

#endif  // PELAGIA_CORE_UTIL_PRECONFIG_H
