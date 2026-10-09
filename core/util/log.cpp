// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "util/log.h"

#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace util {

namespace {

const char kMask[] = "********";
// Below this, a "secret" would mask ordinary words.
constexpr size_t kMinSecretLength = 8;

std::atomic<LogLevel> g_level{LogLevel::Info};
std::mutex g_mutex;  // secrets, sink, and writing of a line in one piece
std::vector<std::string> g_secrets;
LogSink g_sink = nullptr;
void* g_sink_ctx = nullptr;

bool equals_ignore_case(const std::string& text, size_t pos, const char* word) {
    for (size_t i = 0; word[i] != '\0'; ++i) {
        if (pos + i >= text.size() ||
            std::tolower(static_cast<unsigned char>(text[pos + i])) !=
                std::tolower(static_cast<unsigned char>(word[i]))) {
            return false;
        }
    }
    return true;
}

// Masks the value that follows each occurrence of `key` (case-
// insensitive) up to the first character of `stops`.
void mask_after(std::string* text, const char* key, const char* stops) {
    const size_t key_len = std::strlen(key);
    size_t pos = 0;
    while (pos < text->size()) {
        size_t found = std::string::npos;
        for (size_t i = pos; i + key_len <= text->size(); ++i) {
            if (equals_ignore_case(*text, i, key)) {
                found = i;
                break;
            }
        }
        if (found == std::string::npos) {
            return;
        }
        const size_t start = found + key_len;
        size_t end = start;
        while (end < text->size() && std::strchr(stops, (*text)[end]) == nullptr) {
            ++end;
        }
        if (end > start && text->compare(start, end - start, kMask) != 0) {
            text->replace(start, end - start, kMask);
        }
        pos = start + sizeof(kMask) - 1;
    }
}

std::string redact_locked(const std::string& line) {
    std::string out = line;
    for (const std::string& secret : g_secrets) {
        size_t pos = 0;
        while ((pos = out.find(secret, pos)) != std::string::npos) {
            out.replace(pos, secret.size(), kMask);
            pos += sizeof(kMask) - 1;
        }
    }
    // Authentication patterns, even for an unregistered secret.
    mask_after(&out, "api_key=", "& \t\r\n\"'");
    mask_after(&out, "apikey=", "& \t\r\n\"'");
    mask_after(&out, "Token=\"", "\"");
    mask_after(&out, "X-Emby-Token: ", " \t\r\n");
    mask_after(&out, "X-MediaBrowser-Token: ", " \t\r\n");
    return out;
}

void emit_locked(LogLevel level, const std::string& line) {
    const std::string safe = redact_locked(line);
    if (g_sink) {
        g_sink(level, safe.c_str(), g_sink_ctx);
        return;
    }
    std::fprintf(stderr, "[%s] %s\n", log_level_name(level), safe.c_str());
}

}  // namespace

void log_set_level(LogLevel level) {
    g_level = level;
}

LogLevel log_get_level() {
    return g_level;
}

const char* log_level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }
    return "?";
}

void log_write(LogLevel level, const char* fmt, ...) {
    if (level < g_level.load()) {
        return;
    }
    char stack_buf[1024];
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    const int needed = std::vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
    va_end(args);
    std::string line;
    if (needed < 0) {
        line = fmt;
    } else if (static_cast<size_t>(needed) < sizeof(stack_buf)) {
        line.assign(stack_buf, static_cast<size_t>(needed));
    } else {
        line.resize(static_cast<size_t>(needed) + 1);
        std::vsnprintf(&line[0], line.size(), fmt, copy);
        line.resize(static_cast<size_t>(needed));
    }
    va_end(copy);
    std::lock_guard<std::mutex> lock(g_mutex);
    emit_locked(level, line);
}

void log_write_line(LogLevel level, const std::string& line) {
    if (level < g_level.load()) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    emit_locked(level, line);
}

void log_add_secret(const std::string& secret) {
    if (secret.size() < kMinSecretLength) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const std::string& known : g_secrets) {
        if (known == secret) {
            return;
        }
    }
    g_secrets.push_back(secret);
}

void log_clear_secrets() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_secrets.clear();
}

std::string log_redact(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return redact_locked(line);
}

void log_set_sink(LogSink sink, void* ctx) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink = sink;
    g_sink_ctx = ctx;
}

}  // namespace util
