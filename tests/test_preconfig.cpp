// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Tests of the pelagia.conf pre-configuration file (server= only) and
// of the choice of the address at startup.

#include <string>
#include <vector>

#include "test_framework.h"
#include "util/log.h"
#include "util/preconfig.h"

using util::choose_start_server;
using util::preconfig_server;

struct Capture {
  std::vector<std::string> lines;
  static void sink(util::LogLevel, const char* line, void* ctx) {
    static_cast<Capture*>(ctx)->lines.push_back(line);
  }
  std::string all() const {
    std::string out;
    for (const std::string& l : lines) out += l + "\n";
    return out;
  }
};

int main() {
  // Reading server=: spaces, comments, CRLF, last value wins.
  CHECK_EQ(preconfig_server("server=http://192.168.1.10:8096\n"), "http://192.168.1.10:8096");
  CHECK_EQ(preconfig_server("# Pelagia\n\n  server =  http://nas:8096  \r\n"), "http://nas:8096");
  CHECK_EQ(preconfig_server("server=a\nserver=b\n"), "b");
  CHECK_EQ(preconfig_server(""), "");
  CHECK_EQ(preconfig_server("# rien\n"), "");
  CHECK_EQ(preconfig_server("server=\n"), "");
  CHECK_EQ(preconfig_server("server=http://x:8096"), "http://x:8096");  // without a line ending

  // Credentials refused: ignored, value never logged.
  Capture cap;
  util::log_set_sink(&Capture::sink, &cap);
  CHECK_EQ(preconfig_server("user=moi\npassword=MotDePasse42\nserver=http://nas:8096\nbrouillon\n"),
           "http://nas:8096");
  util::log_set_sink(nullptr, nullptr);
  const std::string logs = cap.all();
  CHECK(logs.find("key \"user\" ignored") != std::string::npos);
  CHECK(logs.find("key \"password\" ignored") != std::string::npos);
  CHECK(logs.find("line ignored") != std::string::npos);
  CHECK(logs.find("MotDePasse42") == std::string::npos);
  CHECK(logs.find("moi") == std::string::npos);

  // Choice at startup: --server first; otherwise the preset without a session;
  // a saved session is never replaced by the preset.
  CHECK_EQ(choose_start_server("http://cli:8096", true, "http://conf:8096"), "http://cli:8096");
  CHECK_EQ(choose_start_server("http://cli:8096", false, "http://conf:8096"), "http://cli:8096");
  CHECK_EQ(choose_start_server("", false, "http://conf:8096"), "http://conf:8096");
  CHECK_EQ(choose_start_server("", true, "http://conf:8096"), "");
  CHECK_EQ(choose_start_server("", false, ""), "");

  return testfw::test_failures();
}
