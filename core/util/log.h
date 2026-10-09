// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UTIL_LOG_H
#define PELAGIA_CORE_UTIL_LOG_H

// Logging module of the core: levels + stdio output.
// Always log through this module (never raw printf), so that PS5
// logs stay usable (network/file redirection).
//
// Security: each line is filtered before being written. Registered secrets
// (Jellyfin token) are masked wherever they appear, as well as known
// patterns (api_key=..., Token="...", X-Emby-Token: ...). ffmpeg logs go
// through it too (player/av_log_bridge): they contain the stream URL.

#include <string>

namespace util {

enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

// Minimum level displayed (Info by default).
void log_set_level(LogLevel level);
LogLevel log_get_level();

// Short name of the level ("DEBUG", "INFO", "WARN", "ERROR").
const char* log_level_name(LogLevel level);

// Writes a formatted (printf-style) log line if level >= current level.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
void log_write(LogLevel level, const char* fmt, ...);

// Writes an already formatted line (ffmpeg bridge), filtered like the others.
void log_write_line(LogLevel level, const std::string& line);

// Secret to mask in all logs (ignored if too short, so as not to
// mask ordinary text). Thread-safe.
void log_add_secret(const std::string& secret);
void log_clear_secrets();

// Masks secrets and authentication patterns in a line.
std::string log_redact(const std::string& line);

// Redirects the (already filtered) lines to a function instead of stderr:
// tests, and later network/file output on PS5. nullptr = stderr.
using LogSink = void (*)(LogLevel level, const char* line, void* ctx);
void log_set_sink(LogSink sink, void* ctx);

}  // namespace util

#define LOG_DEBUG(...) ::util::log_write(::util::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...) ::util::log_write(::util::LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...) ::util::log_write(::util::LogLevel::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ::util::log_write(::util::LogLevel::Error, __VA_ARGS__)

#endif  // PELAGIA_CORE_UTIL_LOG_H
