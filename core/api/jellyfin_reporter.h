// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_API_JELLYFIN_REPORTER_H
#define PELAGIA_CORE_API_JELLYFIN_REPORTER_H

// Sends playback reports to Jellyfin (/Sessions/Playing, /Progress,
// /Stopped) from a dedicated thread: a slow server must never block
// the display. Pending progress reports are merged (only the most
// recent one counts); Started and Stopped are never lost.
//
// Without these reports, NowPlayingItem/PlayMethod stay empty on the server and
// the resume position is never saved (seen on a real server).

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "api/http_client.h"
#include "api/jellyfin_client.h"
#include "player/playback_monitor.h"

namespace api {

// JSON body of a report (PlaybackStartInfo / ProgressInfo / StopInfo).
std::string build_playback_report_body(const player::PlaybackReport& report,
                                       const std::string& item_id,
                                       const std::string& media_source_id);

// Report URL: /Sessions/Playing, /Sessions/Playing/Progress or /Stopped.
std::string build_playback_report_url(const std::string& server_url,
                                      player::ReportKind kind);

class PlaybackReporter {
public:
    // transport: short timeouts (a lost report does not matter, a
    // long wait would delay shutdown).
    PlaybackReporter(const JellyfinClient& client, HttpTransport& transport,
                     std::string item_id, std::string media_source_id);
    ~PlaybackReporter();

    void start();
    void submit(const player::PlaybackReport& report);

    // Waits for everything queued (including Stopped) to be sent, at most
    // timeout_ms, then stops the thread. True if everything was sent.
    bool finish(int timeout_ms);

    int sent_count() const;
    int failed_count() const;

private:
    void run();
    void send(const player::PlaybackReport& report);

    const JellyfinClient& client_;
    HttpTransport& transport_;
    std::string item_id_;
    std::string media_source_id_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<player::PlaybackReport> queue_;
    bool sending_ = false;
    bool quit_ = false;
    int sent_ = 0;
    int failed_ = 0;
    std::thread thread_;
};

}  // namespace api

#endif  // PELAGIA_CORE_API_JELLYFIN_REPORTER_H
