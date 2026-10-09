// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "api/jellyfin_stream.h"

#include "api/jellyfin_urls.h"
#include "util/log.h"
#include "util/session_id.h"

namespace api {

namespace {

constexpr long long kTicksPerMs = 10000;

}  // namespace

bool parse_stream_auth_mode(const std::string& text, StreamAuthMode* out) {
    if (text == "both") {
        *out = StreamAuthMode::Both;
    } else if (text == "query") {
        *out = StreamAuthMode::Query;
    } else if (text == "header") {
        *out = StreamAuthMode::Header;
    } else {
        return false;
    }
    return true;
}

const char* stream_auth_mode_name(StreamAuthMode mode) {
    switch (mode) {
        case StreamAuthMode::Both:
            return "both";
        case StreamAuthMode::Query:
            return "query";
        case StreamAuthMode::Header:
            return "header";
    }
    return "?";
}

player::StreamRequest build_stream_request(const JellyfinClient& client,
                                           const std::string& item_id, int64_t start_ms,
                                           const std::string& play_session_id,
                                           StreamAuthMode mode, const TrackSelection& tracks) {
    player::StreamRequest request;
    const bool with_query = mode != StreamAuthMode::Header;
    const bool with_header = mode != StreamAuthMode::Query;
    const long long ticks = start_ms > 0 ? start_ms * kTicksPerMs : 0;
    request.url = client.stream_url(item_id, ticks, play_session_id, with_query, tracks);
    if (with_header) {
        request.headers = "Authorization: " + client.authorization_header() + "\r\n";
    }
    request.log_url = mask_api_key(request.url);
    return request;
}

JellyfinStreamLocator::JellyfinStreamLocator(const JellyfinClient& client,
                                             HttpTransport& control, std::string item_id,
                                             StreamAuthMode mode, const TrackSelection& tracks)
    : client_(client),
      control_(control),
      item_id_(std::move(item_id)),
      mode_(mode),
      tracks_(tracks) {}

void JellyfinStreamLocator::set_tracks(const TrackSelection& tracks) {
    std::lock_guard<std::mutex> lock(tracks_mutex_);
    tracks_ = tracks;
}

TrackSelection JellyfinStreamLocator::tracks() const {
    std::lock_guard<std::mutex> lock(tracks_mutex_);
    return tracks_;
}

std::string JellyfinStreamLocator::new_session_id() {
    return util::make_session_id();
}

bool JellyfinStreamLocator::locate(int64_t start_ms, const std::string& session_id,
                                   player::StreamRequest* out) {
    if (!client_.is_authenticated()) {
        return false;
    }
    *out = build_stream_request(client_, item_id_, start_ms, session_id, mode_, tracks());
    return true;
}

void JellyfinStreamLocator::release(const std::string& session_id) {
    const HttpResponse resp = control_.del(
        build_active_encodings_url(client_.server_url(), client_.device_id(), session_id),
        {{"Authorization", client_.authorization_header()}});
    if (!resp.ok) {
        LOG_WARN("Stopping transcoding of session %.8s... failed: %s",
                 session_id.c_str(), resp.error.c_str());
    } else if (resp.status < 200 || resp.status >= 300) {
        LOG_WARN("Stopping transcoding of session %.8s...: HTTP %ld",
                 session_id.c_str(), resp.status);
    } else {
        LOG_DEBUG("Transcoding of session %.8s... stopped", session_id.c_str());
    }
}

}  // namespace api
