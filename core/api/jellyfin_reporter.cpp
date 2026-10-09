// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "api/jellyfin_reporter.h"

#include <chrono>

#include <cjson/cJSON.h>

#include "api/jellyfin_urls.h"
#include "util/log.h"

namespace api {

namespace {

constexpr long long kTicksPerMs = 10000;

const char* kind_name(player::ReportKind kind) {
    switch (kind) {
        case player::ReportKind::Started:
            return "start";
        case player::ReportKind::Progress:
            return "progression";
        case player::ReportKind::Stopped:
            return "stop";
    }
    return "?";
}

}  // namespace

std::string build_playback_report_body(const player::PlaybackReport& report,
                                       const std::string& item_id,
                                       const std::string& media_source_id) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ItemId", item_id.c_str());
    const std::string& source =
        !report.media_source_id.empty() ? report.media_source_id : media_source_id;
    cJSON_AddStringToObject(root, "MediaSourceId", source.empty() ? item_id.c_str() : source.c_str());
    if (!report.session_id.empty()) {
        cJSON_AddStringToObject(root, "PlaySessionId", report.session_id.c_str());
    }
    const long long position = report.position_ms > 0 ? report.position_ms : 0;
    cJSON_AddNumberToObject(root, "PositionTicks",
                            static_cast<double>(position * kTicksPerMs));
    if (report.kind != player::ReportKind::Stopped) {
        cJSON_AddBoolToObject(root, "IsPaused", report.paused);
        cJSON_AddBoolToObject(root, "IsMuted", false);
        cJSON_AddBoolToObject(root, "CanSeek", true);
        cJSON_AddStringToObject(root, "PlayMethod", "Transcode");
        if (report.has_tracks) {
            // Current tracks (-1: subtitles off), remembered by the server.
            if (report.audio_index >= 0) {
                cJSON_AddNumberToObject(root, "AudioStreamIndex", report.audio_index);
            }
            cJSON_AddNumberToObject(root, "SubtitleStreamIndex", report.subtitle_index);
        }
    }
    char* text = cJSON_PrintUnformatted(root);
    std::string body = text ? text : "{}";
    cJSON_free(text);
    cJSON_Delete(root);
    return body;
}

std::string build_playback_report_url(const std::string& server_url,
                                      player::ReportKind kind) {
    std::string url = normalize_server_url(server_url) + "/Sessions/Playing";
    if (kind == player::ReportKind::Progress) {
        url += "/Progress";
    } else if (kind == player::ReportKind::Stopped) {
        url += "/Stopped";
    }
    return url;
}

PlaybackReporter::PlaybackReporter(const JellyfinClient& client, HttpTransport& transport,
                                   std::string item_id, std::string media_source_id)
    : client_(client),
      transport_(transport),
      item_id_(std::move(item_id)),
      media_source_id_(std::move(media_source_id)) {}

PlaybackReporter::~PlaybackReporter() {
    finish(0);
}

void PlaybackReporter::start() {
    if (!thread_.joinable()) {
        thread_ = std::thread(&PlaybackReporter::run, this);
    }
}

void PlaybackReporter::submit(const player::PlaybackReport& report) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (quit_) {
        return;
    }
    // A progress report replaces the previous one still pending: only the most
    // recent state matters to the server.
    if (report.kind == player::ReportKind::Progress && !queue_.empty() &&
        queue_.back().kind == player::ReportKind::Progress) {
        queue_.back() = report;
    } else {
        queue_.push_back(report);
    }
    wake_.notify_one();
}

bool PlaybackReporter::finish(int timeout_ms) {
    bool done = false;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        done = idle_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                              [this] { return queue_.empty() && !sending_; });
        quit_ = true;
        wake_.notify_all();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (!done) {
        LOG_WARN("Playback reports: server too slow, last state not sent");
    }
    return done;
}

int PlaybackReporter::sent_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sent_;
}

int PlaybackReporter::failed_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failed_;
}

void PlaybackReporter::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
        wake_.wait(lock, [this] { return quit_ || !queue_.empty(); });
        if (quit_) {
            break;  // finish() has already waited for what could be waited for
        }
        const player::PlaybackReport report = queue_.front();
        queue_.pop_front();
        sending_ = true;
        lock.unlock();
        send(report);
        lock.lock();
        sending_ = false;
        if (queue_.empty()) {
            idle_.notify_all();
        }
    }
    idle_.notify_all();
}

void PlaybackReporter::send(const player::PlaybackReport& report) {
    const HttpResponse resp = transport_.post(
        build_playback_report_url(client_.server_url(), report.kind),
        {{"Authorization", client_.authorization_header()}},
        build_playback_report_body(report, item_id_, media_source_id_), "application/json");
    const bool ok = resp.ok && resp.status >= 200 && resp.status < 300;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ok ? ++sent_ : ++failed_;
    }
    if (ok) {
        LOG_DEBUG("Playback report (%s) sent: %lld ms%s", kind_name(report.kind),
                  static_cast<long long>(report.position_ms), report.paused ? ", en pause" : "");
    } else if (!resp.ok) {
        LOG_WARN("Playback report (%s) not sent: %s", kind_name(report.kind),
                 resp.error.c_str());
    } else {
        LOG_WARN("Playback report (%s) refused: HTTP %ld", kind_name(report.kind),
                 resp.status);
    }
}

}  // namespace api
