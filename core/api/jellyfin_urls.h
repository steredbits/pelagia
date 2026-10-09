// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_API_JELLYFIN_URLS_H
#define PELAGIA_CORE_API_JELLYFIN_URLS_H

// Construction of Jellyfin API URLs: pure functions, no network,
// unit tested. Parameter names follow the official OpenAPI
// spec (the server's ASP.NET binding is case-insensitive).

#include <string>

#include "api/jellyfin_models.h"
#include "api/track_selection.h"

namespace api {

// Encodes a URL component (everything except RFC 3986 unreserved characters).
std::string url_encode(const std::string& value);

// Removes the possible trailing '/' of the server URL ("http://x:8096/" -> "http://x:8096").
std::string normalize_server_url(const std::string& server_url);

std::string build_auth_url(const std::string& server_url);
// User of the current token (restoring a saved session).
std::string build_current_user_url(const std::string& server_url);
// POST: ends the session and revokes the token on the server side.
std::string build_logout_url(const std::string& server_url);
// "Continue watching" (started movies and episodes). legacy_route: route
// /Users/{id}/Items/Resume (the one used by the client's other calls); otherwise the
// recent route /UserItems/Resume?userId= (fallback if the first answers 404).
std::string build_resume_url(const std::string& server_url, const std::string& user_id,
                             int limit, bool legacy_route);
std::string build_views_url(const std::string& server_url, const std::string& user_id);
std::string build_items_url(const std::string& server_url, const std::string& user_id,
                            const ItemsQuery& query);
std::string build_seasons_url(const std::string& server_url, const std::string& series_id,
                              const std::string& user_id);
std::string build_episodes_url(const std::string& server_url, const std::string& series_id,
                               const std::string& season_id, const std::string& user_id);

// Primary poster; tag comes from ImageTags.Primary (may be empty).
std::string build_image_url(const std::string& server_url, const std::string& item_id,
                            const std::string& image_tag, int max_width);

// Tracks in the stream URL (tracks): mediaSourceId if known, audioStreamIndex
// if an audio track is chosen, subtitleStreamIndex + subtitleMethod=Encode
// only for subtitles burned in by the server (SubtitleDelivery::Encode);
// subtitles rendered by the client (or none) add nothing: without
// subtitleStreamIndex the server burns nothing in and can keep the video in
// direct streaming. Names checked in the server's VideosController (10.11 and master).
//
// v1 stream URL: forced H.264 1080p + AAC stereo transcoding, container
// MPEG-TS progressif (static=false). mediaSourceId volontairement omis :
// the server takes the default source (validated on 12.1 with single-version sources).
// start_time_ticks > 0 adds startTimeTicks (server-side seek);
// a non-empty play_session_id adds playSessionId - essential so that a
// new startTimeTicks is not ignored in favor of the transcoding job
// already running on the same deviceId (seen on Jellyfin 12.1).
std::string build_stream_url(const std::string& server_url, const std::string& item_id,
                             const std::string& access_token, const std::string& device_id,
                             long long start_time_ticks = 0,
                             const std::string& play_session_id = "",
                             bool include_api_key = true,
                             const TrackSelection& tracks = TrackSelection());

// Text subtitles of a track as SRT (the server converts ASS/SSA and extracts
// tracks embedded in the file): route of the server's SubtitleController,
// source = id of the MediaSource (empty: the item).
std::string build_subtitle_url(const std::string& server_url, const std::string& item_id,
                               const std::string& media_source_id, int stream_index);

// An item with UserData (resume position) and MediaSources.
std::string build_item_url(const std::string& server_url, const std::string& user_id,
                           const std::string& item_id);

// DELETE: stops the transcoding job of a playback session. On 12.1,
// playSessionId is mandatory (HTTP 400 otherwise).
std::string build_active_encodings_url(const std::string& server_url,
                                       const std::string& device_id,
                                       const std::string& play_session_id);

// Masks the api_key value in a URL meant for display/logs.
std::string mask_api_key(const std::string& url);

}  // namespace api

#endif  // PELAGIA_CORE_API_JELLYFIN_URLS_H
