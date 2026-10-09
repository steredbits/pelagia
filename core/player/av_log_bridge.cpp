// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "player/av_log_bridge.h"

#include <cstdarg>
#include <string>

extern "C" {
#include <libavutil/log.h>
}

#include "util/log.h"

namespace player {

namespace {

util::LogLevel map_level(int level) {
  if (level <= AV_LOG_ERROR) {
    return util::LogLevel::Error;
  }
  if (level <= AV_LOG_WARNING) {
    return util::LogLevel::Warn;
  }
  return util::LogLevel::Debug;  // info, verbose, debug, trace: detail
}

void bridge(void* avcl, int level, const char* fmt, va_list vl) {
  if (level > av_log_get_level()) {
    return;
  }
  // ffmpeg may emit a line in several calls: they are accumulated per
  // thread until the newline, to filter the whole line (a secret
  // split in two would escape masking).
  thread_local std::string pending;
  thread_local int print_prefix = 1;
  char part[2048];
  av_log_format_line2(avcl, level, fmt, vl, part, sizeof(part), &print_prefix);
  pending += part;
  if (pending.empty() || pending.back() != '\n') {
    return;
  }
  while (!pending.empty() && (pending.back() == '\n' || pending.back() == '\r')) {
    pending.pop_back();
  }
  if (!pending.empty()) {
    util::log_write_line(map_level(level), pending);
  }
  pending.clear();
}

}  // namespace

void install_ffmpeg_log_bridge(bool verbose) {
  av_log_set_level(verbose ? AV_LOG_DEBUG : AV_LOG_WARNING);
  av_log_set_callback(&bridge);
}

void ensure_ffmpeg_log_bridge() {
  av_log_set_callback(&bridge);
}

}  // namespace player
