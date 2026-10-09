// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_API_JELLYFIN_CLIENT_H
#define PELAGIA_CORE_API_JELLYFIN_CLIENT_H

// Jellyfin REST client: authentication, libraries, items,
// poster and stream URLs. The HTTP transport is injected
// (HttpCurlTransport for real, a mock in the tests).

#include <string>
#include <vector>

#include "api/http_client.h"
#include "api/jellyfin_models.h"
#include "api/track_selection.h"

namespace api {

enum class ApiError {
    None = 0,
    Transport,         // no response (network, DNS, timeout)
    Http,              // response with status != 2xx (401 = credentials/token)
    Parse,             // 2xx response but unexpected JSON
    NotAuthenticated,  // call that requires a session before authenticate()
};

struct ApiResult {
    ApiError error = ApiError::None;
    long http_status = 0;
    std::string message;

    bool ok() const { return error == ApiError::None; }
};

class JellyfinClient {
public:
    // device_id identifies this client to the server (MediaBrowser header).
    JellyfinClient(HttpTransport& transport, const std::string& server_url,
                   const std::string& device_id = "pelagia");

    ApiResult authenticate(const std::string& username, const std::string& password);

    // Resumes a saved session (token only): GET /Users/Me validates the
    // token and returns the user. On failure the session stays closed
    // (401 = revoked or expired token).
    ApiResult restore_session(const std::string& access_token);
    // Revokes the token on the server side (POST /Sessions/Logout) then closes the
    // local session, even if the server does not answer.
    ApiResult logout();

    bool is_authenticated() const { return !session_.access_token.empty(); }
    const AuthSession& session() const { return session_; }
    const std::string& server_url() const { return server_url_; }

    ApiResult fetch_libraries(std::vector<Library>* out);
    ApiResult fetch_items(const ItemsQuery& query, ItemList* out);
    ApiResult fetch_seasons(const std::string& series_id, ItemList* out);
    ApiResult fetch_episodes(const std::string& series_id, const std::string& season_id,
                             ItemList* out);
    // Started movies and episodes, most recent first.
    ApiResult fetch_resume(int limit, ItemList* out);
    // Poster bytes (usually JPEG) scaled down to max_width.
    ApiResult fetch_image(const std::string& item_id, const std::string& image_tag,
                          int max_width, std::string* bytes);

    // URL construction (no network call). See build_stream_url for
    // the role of start_time_ticks and play_session_id.
    std::string image_url(const MediaItem& item, int max_width) const;
    std::string stream_url(const std::string& item_id, long long start_time_ticks = 0,
                           const std::string& play_session_id = "",
                           bool include_api_key = true,
                           const TrackSelection& tracks = TrackSelection()) const;

    // Text subtitles of a track, as SRT (transport timeout: the server
    // may have to extract the track from the file).
    ApiResult fetch_subtitle(const std::string& item_id, const std::string& media_source_id,
                             int stream_index, std::string* srt);

    // An item with its resume position (UserData) and its media source.
    ApiResult fetch_item(const std::string& item_id, MediaItem* out);

    // Value of the Authorization header (MediaBrowser scheme, with Token once
    // authenticated): also used for the stream request.
    std::string authorization_header() const;
    const std::string& device_id() const { return device_id_; }

private:
    std::vector<HttpHeader> auth_headers() const;
    ApiResult get_json(const std::string& url, std::string* body_out);

    HttpTransport& transport_;
    std::string server_url_;
    std::string device_id_;
    AuthSession session_;
};

}  // namespace api

#endif  // PELAGIA_CORE_API_JELLYFIN_CLIENT_H
