// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Tests of the util/log module: levels, names, secret filtering, sink, and
// ffmpeg log bridge.

#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/log.h>
}

#include "player/av_log_bridge.h"
#include "test_framework.h"
#include "util/log.h"

static bool str_eq(const char* a, const char* b) {
    return std::strcmp(a, b) == 0;
}

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

struct Capture {
    std::vector<std::string> lines;
    static void sink(util::LogLevel, const char* line, void* ctx) {
        static_cast<Capture*>(ctx)->lines.push_back(line);
    }
    std::string all() const {
        std::string out;
        for (const std::string& l : lines) {
            out += l + "\n";
        }
        return out;
    }
};

int main() {
    using util::LogLevel;

    CHECK_EQ(util::log_get_level(), LogLevel::Info);

    util::log_set_level(LogLevel::Error);
    CHECK_EQ(util::log_get_level(), LogLevel::Error);
    util::log_set_level(LogLevel::Debug);
    CHECK_EQ(util::log_get_level(), LogLevel::Debug);

    CHECK(str_eq(util::log_level_name(LogLevel::Debug), "DEBUG"));
    CHECK(str_eq(util::log_level_name(LogLevel::Info), "INFO"));
    CHECK(str_eq(util::log_level_name(LogLevel::Warn), "WARN"));
    CHECK(str_eq(util::log_level_name(LogLevel::Error), "ERROR"));

    // log_write must not crash, whatever the filtered level.
    util::log_set_level(LogLevel::Error);
    LOG_DEBUG("filtered line %d", 42);
    LOG_ERROR("ligne visible %s", "ok");

    // --- Secret filtering ----------------------------------------------------
    const std::string token = "0123456789abcdef0123456789abcdef";
    util::log_add_secret(token);
    util::log_add_secret("court");  // ignored: would mask ordinary text
    CHECK_EQ(util::log_redact("token " + token + " fin"), "token ******** fin");
    CHECK_EQ(util::log_redact("un mot court reste"), "un mot court reste");

    // Patterns, even for a secret that was never registered.
    CHECK_EQ(util::log_redact("GET /stream.ts?a=1&api_key=autre123&b=2"),
             "GET /stream.ts?a=1&api_key=********&b=2");
    CHECK_EQ(util::log_redact("url ...&API_KEY=xyz"), "url ...&API_KEY=********");
    CHECK_EQ(util::log_redact("Authorization: MediaBrowser Client=\"J\", Token=\"abc\""),
             "Authorization: MediaBrowser Client=\"J\", Token=\"********\"");
    CHECK_EQ(util::log_redact("X-Emby-Token: abc def"), "X-Emby-Token: ******** def");
    CHECK_EQ(util::log_redact("api_key=********"), "api_key=********");  // already masked

    // Long lines (beyond the formatting buffer): filtered whole.
    util::log_set_level(LogLevel::Debug);
    Capture capture;
    util::log_set_sink(&Capture::sink, &capture);
    const std::string padding(3000, 'x');
    LOG_INFO("%s api_key=%s fin", padding.c_str(), token.c_str());
    LOG_DEBUG("Token=\"%s\"", token.c_str());
    CHECK_EQ(capture.lines.size(), 2u);
    CHECK(!contains(capture.all(), token));
    CHECK(contains(capture.all(), "api_key=******** fin"));
    CHECK(capture.lines.size() == 2 && capture.lines[0].size() > 3000);

    // --- Pont ffmpeg ---------------------------------------------------------
    // HTTP request as libavformat logs it at debug level, and a line
    // emitted in two calls with the token split between the two.
    capture.lines.clear();
    player::install_ffmpeg_log_bridge(true);
    av_log(nullptr, AV_LOG_DEBUG,
           "request: GET /Videos/x/stream.ts?api_key=%s HTTP/1.1\r\n"
           "Authorization: MediaBrowser Token=\"%s\"\r\n\n",
           token.c_str(), token.c_str());
    av_log(nullptr, AV_LOG_ERROR, "Server returned 401 for ...%s", token.substr(0, 16).c_str());
    av_log(nullptr, AV_LOG_ERROR, "%s (authorization failed)\n", token.substr(16).c_str());
    av_log(nullptr, AV_LOG_TRACE, "trace filtered by the level %s\n", token.c_str());
    CHECK(!contains(capture.all(), token));
    CHECK(!contains(capture.all(), token.substr(0, 16)));
    CHECK(contains(capture.all(), "api_key=********"));
    CHECK(contains(capture.all(), "Token=\"********\""));
    CHECK(contains(capture.all(), "Server returned 401 for ...********"));
    CHECK(!contains(capture.all(), "trace filtered"));

    util::log_set_sink(nullptr, nullptr);
    util::log_clear_secrets();
    CHECK(contains(util::log_redact(token), token));
    return testfw::test_failures();
}
