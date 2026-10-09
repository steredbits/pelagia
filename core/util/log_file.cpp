// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "util/log_file.h"

#include <chrono>
#include <ctime>

namespace util {

namespace {

double now_s() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string numbered(const std::string& base, int index) {
  return index == 0 ? base + ".log" : base + "." + std::to_string(index) + ".log";
}

struct TeeTarget {
  std::FILE* console = nullptr;
  LogFile* file = nullptr;
};
TeeTarget g_tee;  // protected by the util/log lock (the sink is called under the lock)

void tee_sink(LogLevel level, const char* line, void*) {
  if (g_tee.console) {
    std::fprintf(g_tee.console, "[%s] %s\n", log_level_name(level), line);
    std::fflush(g_tee.console);
  }
  if (g_tee.file) {
    g_tee.file->write(level, line);
  }
}

}  // namespace

bool LogFile::open(const std::string& dir, const std::string& name, int keep) {
  close();
  const std::string base = dir + "/" + name;
  if (keep < 1) keep = 1;
  // name.(keep-2).log -> name.(keep-1).log ... name.log -> name.1.log
  std::remove(numbered(base, keep - 1).c_str());
  for (int i = keep - 2; i >= 0; --i) {
    std::rename(numbered(base, i).c_str(), numbered(base, i + 1).c_str());
  }
  path_ = numbered(base, 0);
  file_ = std::fopen(path_.c_str(), "w");
  if (!file_) {
    return false;
  }
  start_s_ = now_s();
  const std::time_t t = std::time(nullptr);
  char stamp[64];
  std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S UTC", std::gmtime(&t));
  std::fprintf(file_, "=== Pelagia, start %s ===\n", stamp);
  std::fflush(file_);
  return true;
}

void LogFile::close() {
  if (file_) {
    std::fclose(file_);
    file_ = nullptr;
  }
}

void LogFile::write(LogLevel level, const char* line) {
  if (!file_) {
    return;
  }
  std::fprintf(file_, "[%8.3f] %-5s %s\n", now_s() - start_s_, log_level_name(level), line);
  std::fflush(file_);
}

void log_install_console_and_file(std::FILE* console, LogFile* file) {
  log_set_sink(nullptr, nullptr);  // no line written during the rotation
  g_tee.console = console;
  g_tee.file = file;
  log_set_sink(&tee_sink, nullptr);
}

}  // namespace util
