// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "api/jellyfin_client.h"

#include "api/jellyfin_json.h"
#include "api/jellyfin_urls.h"
#include "util/app_info.h"
#include "util/log.h"
#include "util/version.h"

namespace api {

namespace {


ApiResult make_error(ApiError error, long status, const char* message) {
    ApiResult r;
    r.error = error;
    r.http_status = status;
    r.message = message;
    return r;
}

ApiResult check_response(const HttpResponse& resp) {
    if (!resp.ok) {
        ApiResult r;
        r.error = ApiError::Transport;
        r.message = resp.error;
        return r;
    }
    if (resp.status < 200 || resp.status >= 300) {
        // The HTTP status is carried by http_status: do not repeat it here.
        return make_error(ApiError::Http, resp.status,
                          resp.status == 401 ? "authentication refused"
                                             : "statut HTTP inattendu");
    }
    return ApiResult{};
}

}  // namespace

JellyfinClient::JellyfinClient(HttpTransport& transport, const std::string& server_url,
                               const std::string& device_id)
    : transport_(transport),
      server_url_(normalize_server_url(server_url)),
      device_id_(device_id) {}

std::string JellyfinClient::authorization_header() const {
    // Jellyfin identification header (MediaBrowser scheme); Token is
    // added once the session is open.
    std::string value = std::string("MediaBrowser Client=\"") + util::kAppName +
                        "\", Device=\"" + util::kDeviceName + "\", DeviceId=\"" +
                        device_id_ + "\", Version=\"" + util::kVersion + "\"";
    if (!session_.access_token.empty()) {
        value += ", Token=\"" + session_.access_token + "\"";
    }
    return value;
}

std::vector<HttpHeader> JellyfinClient::auth_headers() const {
    return {{"Authorization", authorization_header()}};
}

ApiResult JellyfinClient::authenticate(const std::string& username,
                                       const std::string& password) {
    session_ = AuthSession{};

    std::string body = build_auth_body(username, password);
    HttpResponse resp = transport_.post(build_auth_url(server_url_), auth_headers(),
                                        body, "application/json");
    // No error log here: the caller decides how to report it
    // (otherwise the same failure is logged twice).
    ApiResult result = check_response(resp);
    if (!result.ok()) {
        return result;
    }

    if (!parse_auth_response(resp.body, &session_)) {
        session_ = AuthSession{};
        return make_error(ApiError::Parse, resp.status,
                          "unreadable authentication response");
    }

    // The token must not appear in any log, in any form.
    util::log_add_secret(session_.access_token);
    util::log_add_secret(url_encode(session_.access_token));
    LOG_INFO("Authenticated as %s", session_.user_name.c_str());
    return ApiResult{};
}

ApiResult JellyfinClient::restore_session(const std::string& access_token) {
    session_ = AuthSession{};
    if (access_token.empty()) {
        return make_error(ApiError::NotAuthenticated, 0, "no saved token");
    }
    util::log_add_secret(access_token);
    util::log_add_secret(url_encode(access_token));
    session_.access_token = access_token;
    std::string body;
    ApiResult result = get_json(build_current_user_url(server_url_), &body);
    if (result.ok() && !parse_user_response(body, &session_)) {
        result = make_error(ApiError::Parse, 200, "unreadable Users/Me response");
    }
    if (!result.ok()) {
        session_ = AuthSession{};
        return result;
    }
    session_.access_token = access_token;
    LOG_INFO("Session resumed for %s", session_.user_name.c_str());
    return ApiResult{};
}

ApiResult JellyfinClient::logout() {
    if (!is_authenticated()) {
        return ApiResult{};
    }
    HttpResponse resp =
        transport_.post(build_logout_url(server_url_), auth_headers(), "", "application/json");
    session_ = AuthSession{};
    return check_response(resp);
}

ApiResult JellyfinClient::get_json(const std::string& url, std::string* body_out) {
    if (!is_authenticated()) {
        return make_error(ApiError::NotAuthenticated, 0,
                          "appeler authenticate() d'abord");
    }
    HttpResponse resp = transport_.get(url, auth_headers());
    ApiResult result = check_response(resp);
    if (result.ok()) {
        *body_out = resp.body;
    }
    return result;
}

ApiResult JellyfinClient::fetch_libraries(std::vector<Library>* out) {
    std::string body;
    ApiResult result = get_json(build_views_url(server_url_, session_.user_id), &body);
    if (!result.ok()) {
        return result;
    }
    if (!parse_views_response(body, out)) {
        return make_error(ApiError::Parse, 200, "unreadable Views response");
    }
    return ApiResult{};
}

ApiResult JellyfinClient::fetch_items(const ItemsQuery& query, ItemList* out) {
    std::string body;
    ApiResult result =
        get_json(build_items_url(server_url_, session_.user_id, query), &body);
    if (!result.ok()) {
        return result;
    }
    if (!parse_items_response(body, out)) {
        return make_error(ApiError::Parse, 200, "unreadable Items response");
    }
    return ApiResult{};
}

ApiResult JellyfinClient::fetch_seasons(const std::string& series_id, ItemList* out) {
    std::string body;
    ApiResult result =
        get_json(build_seasons_url(server_url_, series_id, session_.user_id), &body);
    if (!result.ok()) {
        return result;
    }
    if (!parse_items_response(body, out)) {
        return make_error(ApiError::Parse, 200, "unreadable Seasons response");
    }
    return ApiResult{};
}

ApiResult JellyfinClient::fetch_episodes(const std::string& series_id,
                                         const std::string& season_id, ItemList* out) {
    std::string body;
    ApiResult result = get_json(
        build_episodes_url(server_url_, series_id, season_id, session_.user_id), &body);
    if (!result.ok()) {
        return result;
    }
    if (!parse_items_response(body, out)) {
        return make_error(ApiError::Parse, 200, "unreadable Episodes response");
    }
    return ApiResult{};
}

ApiResult JellyfinClient::fetch_resume(int limit, ItemList* out) {
    std::string body;
    ApiResult result =
        get_json(build_resume_url(server_url_, session_.user_id, limit, true), &body);
    if (!result.ok() && result.http_status == 404) {
        // Route moved in a recent server version.
        result = get_json(build_resume_url(server_url_, session_.user_id, limit, false), &body);
    }
    if (!result.ok()) {
        return result;
    }
    if (!parse_items_response(body, out)) {
        return make_error(ApiError::Parse, 200, "unreadable Resume response");
    }
    return ApiResult{};
}

ApiResult JellyfinClient::fetch_image(const std::string& item_id, const std::string& image_tag,
                                      int max_width, std::string* bytes) {
    if (!is_authenticated()) {
        return make_error(ApiError::NotAuthenticated, 0, "appeler authenticate() d'abord");
    }
    HttpResponse resp = transport_.get(
        build_image_url(server_url_, item_id, image_tag, max_width), auth_headers());
    ApiResult result = check_response(resp);
    if (result.ok()) {
        if (resp.body.empty()) {
            return make_error(ApiError::Parse, resp.status, "image vide");
        }
        bytes->swap(resp.body);
    }
    return result;
}

ApiResult JellyfinClient::fetch_subtitle(const std::string& item_id,
                                         const std::string& media_source_id, int stream_index,
                                         std::string* srt) {
    if (!is_authenticated()) {
        return make_error(ApiError::NotAuthenticated, 0, "appeler authenticate() d'abord");
    }
    HttpResponse resp = transport_.get(
        build_subtitle_url(server_url_, item_id, media_source_id, stream_index), auth_headers());
    ApiResult result = check_response(resp);
    if (result.ok()) {
        srt->swap(resp.body);
    }
    return result;
}

std::string JellyfinClient::image_url(const MediaItem& item, int max_width) const {
    return build_image_url(server_url_, item.id, item.primary_image_tag, max_width);
}

std::string JellyfinClient::stream_url(const std::string& item_id,
                                       long long start_time_ticks,
                                       const std::string& play_session_id,
                                       bool include_api_key,
                                       const TrackSelection& tracks) const {
    return build_stream_url(server_url_, item_id, session_.access_token, device_id_,
                            start_time_ticks, play_session_id, include_api_key, tracks);
}

ApiResult JellyfinClient::fetch_item(const std::string& item_id, MediaItem* out) {
    std::string body;
    ApiResult result =
        get_json(build_item_url(server_url_, session_.user_id, item_id), &body);
    if (!result.ok()) {
        return result;
    }
    if (!parse_item_response(body, out)) {
        return make_error(ApiError::Parse, 200, "unreadable Item response");
    }
    return ApiResult{};
}

}  // namespace api
