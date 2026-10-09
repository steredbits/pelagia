// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_API_JELLYFIN_STREAM_H
#define PELAGIA_CORE_API_JELLYFIN_STREAM_H

// Jellyfin playback stream for the player's network source: progressive TS
// stream URL (startTimeTicks + playSessionId), authentication, and stopping
// a session's transcoding job (DELETE /Videos/ActiveEncodings).

#include <cstdint>
#include <mutex>
#include <string>

#include "api/http_client.h"
#include "api/jellyfin_client.h"
#include "player/stream_locator.h"

namespace api {

// Authentication of the stream request. Header (player default): MediaBrowser
// header only, the token is in no URL; accepted by Jellyfin 12.1
// (tested on a real server). Query: api_key in the URL, Both: both, as a fallback
// if a server or proxy rejected the header. The client never switches on its own.
enum class StreamAuthMode { Both, Query, Header };

bool parse_stream_auth_mode(const std::string& text, StreamAuthMode* out);
const char* stream_auth_mode_name(StreamAuthMode mode);

// Stream request (pure function, tested): 1 ms = 10,000 ticks.
player::StreamRequest build_stream_request(const JellyfinClient& client,
                                           const std::string& item_id, int64_t start_ms,
                                           const std::string& play_session_id,
                                           StreamAuthMode mode,
                                           const TrackSelection& tracks = TrackSelection());

class JellyfinStreamLocator final : public player::StreamLocator {
public:
    // control: transport of control calls (DELETE), with short
    // timeouts - they run in the demux thread, during a seek.
    JellyfinStreamLocator(const JellyfinClient& client, HttpTransport& control,
                          std::string item_id, StreamAuthMode mode,
                          const TrackSelection& tracks = TrackSelection());

    // Tracks requested at the next stream openings (UI thread;
    // locate() runs on the demux thread). A change only takes effect
    // at the next reopening: the caller triggers a seek to the current
    // position (same mechanism as a seek, new session).
    void set_tracks(const TrackSelection& tracks);
    TrackSelection tracks() const;

    std::string new_session_id() override;
    bool locate(int64_t start_ms, const std::string& session_id,
                player::StreamRequest* out) override;
    void release(const std::string& session_id) override;

private:
    const JellyfinClient& client_;
    HttpTransport& control_;
    std::string item_id_;
    StreamAuthMode mode_;
    mutable std::mutex tracks_mutex_;
    TrackSelection tracks_;
};

}  // namespace api

#endif  // PELAGIA_CORE_API_JELLYFIN_STREAM_H
