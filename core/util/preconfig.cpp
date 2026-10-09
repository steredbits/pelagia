// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "util/preconfig.h"

#include <sstream>

#include "util/log.h"

namespace util {

namespace {

std::string trim(const std::string& s) {
  size_t b = 0;
  size_t e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
  return s.substr(b, e - b);
}

}  // namespace

std::string preconfig_server(const std::string& text) {
  std::istringstream in(text);
  std::string line;
  std::string server;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
      LOG_WARN("pelagia.conf: line ignored (expected format key=value)");
      continue;
    }
    const std::string key = trim(line.substr(0, eq));
    if (key == "server") {
      server = trim(line.substr(eq + 1));
    } else {
      // Value never displayed: it could be a password.
      LOG_WARN("pelagia.conf: key \"%s\" ignored (only server= is read, never credentials)",
               key.c_str());
    }
  }
  return server;
}

std::string find_preconfig_server(const std::vector<std::string>& paths, FileStore* files,
                                  std::string* source) {
  for (const std::string& path : paths) {
    std::string text;
    if (!files->read(path, &text)) continue;
    const std::string server = preconfig_server(text);
    if (server.empty()) {
      LOG_INFO("%s read, without a server= address: next location", path.c_str());
      continue;
    }
    if (source) *source = path;
    return server;
  }
  return "";
}

std::string choose_start_server(const std::string& command_line, bool has_saved_session,
                                const std::string& preconfig) {
  if (!command_line.empty()) return command_line;
  if (has_saved_session) return "";
  return preconfig;
}

}  // namespace util
