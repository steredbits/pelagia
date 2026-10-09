// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Tests of util/log_file: log file per run (rotation), lines
// flushed immediately, secrets masked as on the console.

#include <stdlib.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "test_framework.h"
#include "util/log.h"
#include "util/log_file.h"

static std::string read_file(const std::string& path) {
  std::ifstream in(path);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static bool exists(const std::string& path) { return access(path.c_str(), F_OK) == 0; }

static bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

// A "run": opens the file, writes a marked line, closes.
static void run_once(const std::string& dir, const std::string& marker) {
  util::LogFile f;
  CHECK(f.open(dir, "app", 3));
  util::log_install_console_and_file(nullptr, &f);
  LOG_INFO("%s", marker.c_str());
  // Readable right away, before closing (crash: nothing lost).
  CHECK(contains(read_file(dir + "/app.log"), marker));
  util::log_set_sink(nullptr, nullptr);
}

int main() {
  char tmpl[] = "/tmp/pelagia-logfile-XXXXXX";
  const char* base = mkdtemp(tmpl);
  CHECK(base != nullptr);
  const std::string dir = base;

  // Format: relative timestamp, level, message; startup header.
  run_once(dir, "premiere");
  const std::string first = read_file(dir + "/app.log");
  CHECK(contains(first, "=== Pelagia, start "));
  CHECK(contains(first, "INFO  premiere"));

  // Rotation: 3 files at most (current + 2 previous), from the most recent to the
  // oldest.
  run_once(dir, "deuxieme");
  run_once(dir, "troisieme");
  run_once(dir, "quatrieme");
  CHECK(contains(read_file(dir + "/app.log"), "quatrieme"));
  CHECK(contains(read_file(dir + "/app.1.log"), "troisieme"));
  CHECK(contains(read_file(dir + "/app.2.log"), "deuxieme"));
  CHECK(!exists(dir + "/app.3.log"));
  CHECK(!contains(read_file(dir + "/app.log"), "troisieme"));

  // Secrets masked in the file as on the console.
  {
    util::LogFile f;
    CHECK(f.open(dir, "secret", 1));
    util::log_install_console_and_file(nullptr, &f);
    util::log_add_secret("JetonTresSecret12345");
    LOG_INFO("token=JetonTresSecret12345 Token=\"JetonTresSecret12345\"");
    util::log_set_sink(nullptr, nullptr);
    util::log_clear_secrets();
    const std::string text = read_file(dir + "/secret.log");
    CHECK(!contains(text, "JetonTresSecret12345"));
    CHECK(contains(text, "********"));
  }

  // Folder missing: clean failure, no write attempted.
  {
    util::LogFile f;
    CHECK(!f.open(dir + "/absent", "app", 2));
    CHECK(!f.is_open());
    f.write(util::LogLevel::Info, "ignored");
  }

  std::remove((dir + "/app.log").c_str());
  std::remove((dir + "/app.1.log").c_str());
  std::remove((dir + "/app.2.log").c_str());
  std::remove((dir + "/secret.log").c_str());
  rmdir(dir.c_str());
  return testfw::test_failures();
}
