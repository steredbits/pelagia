// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UTIL_LOG_FILE_H
#define PELAGIA_CORE_UTIL_LOG_FILE_H

// Log output to a file, in addition to the console. Used on PS5, where
// the application launched from the websrv tile has nobody reading stdout:
// <folder>/<name>.log holds the current run, <name>.1.log the
// previous one, etc. Each line is written and flushed immediately (a crash
// loses nothing). The lines received are already filtered by util/log (token and
// secrets masked).

#include <cstdio>
#include <string>

#include "util/log.h"

namespace util {

class LogFile {
 public:
  LogFile() {}
  ~LogFile() { close(); }
  LogFile(const LogFile&) = delete;
  LogFile& operator=(const LogFile&) = delete;

  // Shifts the files of previous runs (keeps `keep` files in
  // total, the current one included) then opens <dir>/<name>.log. The folder must
  // exist. False if the file cannot be created.
  bool open(const std::string& dir, const std::string& name, int keep);
  void close();
  bool is_open() const { return file_ != nullptr; }
  const std::string& path() const { return path_; }

  // "[  12.345] INFO  message" (seconds since opening).
  void write(LogLevel level, const char* line);

 private:
  std::FILE* file_ = nullptr;
  std::string path_;
  double start_s_ = 0;
};

// Installs the util/log sink: each line goes to `console` (stdout, stderr,
// or nullptr) in the usual format, and to `file` if it is open. `file` must
// live until log_set_sink(nullptr, nullptr).
void log_install_console_and_file(std::FILE* console, LogFile* file);

}  // namespace util

#endif  // PELAGIA_CORE_UTIL_LOG_FILE_H
