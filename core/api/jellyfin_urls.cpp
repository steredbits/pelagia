// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "api/jellyfin_urls.h"

#include <cstdio>

namespace api {

std::string url_encode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                          c == '_' || c == '~';
        if (unreserved) {
            out.push_back(static_cast<char>(c));
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out.append(buf);
        }
    }
    return out;
}

std::string normalize_server_url(const std::string& server_url) {
    std::string out = server_url;
    while (!out.empty() && out.back() == '/') {
        out.pop_back();
    }
    return out;
}

std::string build_auth_url(const std::string& server_url) {
    return normalize_server_url(server_url) + "/Users/AuthenticateByName";
}

namespace {
// Fields requested for lists: overview, year and SortName (local sort after
// cleaning invisible characters). UserData is always returned for a user.
const char kListFields[] = "Overview,ProductionYear,SortName";
}  // namespace

std::string build_current_user_url(const std::string& server_url) {
    return normalize_server_url(server_url) + "/Users/Me";
}

std::string build_logout_url(const std::string& server_url) {
    return normalize_server_url(server_url) + "/Sessions/Logout";
}

std::string build_resume_url(const std::string& server_url, const std::string& user_id,
                             int limit, bool legacy_route) {
    const std::string base = normalize_server_url(server_url);
    std::string url = legacy_route
                          ? base + "/Users/" + url_encode(user_id) + "/Items/Resume?"
                          : base + "/UserItems/Resume?userId=" + url_encode(user_id) + "&";
    url += "mediaTypes=Video&fields=" + std::string(kListFields);
    if (limit > 0) {
        url += "&limit=" + std::to_string(limit);
    }
    return url;
}

std::string build_views_url(const std::string& server_url, const std::string& user_id) {
    return normalize_server_url(server_url) + "/Users/" + url_encode(user_id) + "/Views";
}

std::string build_items_url(const std::string& server_url, const std::string& user_id,
                            const ItemsQuery& query) {
    std::string url = normalize_server_url(server_url) + "/Users/" +
                      url_encode(user_id) + "/Items?sortBy=" + url_encode(query.sort_by) +
                      (query.sort_descending ? "&sortOrder=Descending" : "&sortOrder=Ascending") +
                      "&fields=" + kListFields;
    if (query.limit > 0) {
        url += "&limit=" + std::to_string(query.limit);
    }
    if (!query.parent_id.empty()) {
        url += "&parentId=" + url_encode(query.parent_id);
    }
    if (!query.include_item_types.empty()) {
        url += "&includeItemTypes=" + url_encode(query.include_item_types);
    }
    if (query.recursive) {
        url += "&recursive=true";
    }
    if (query.collapse_box_set_items.has_value()) {
        url += *query.collapse_box_set_items ? "&collapseBoxSetItems=true"
                                             : "&collapseBoxSetItems=false";
    }
    return url;
}

std::string build_seasons_url(const std::string& server_url, const std::string& series_id,
                              const std::string& user_id) {
    return normalize_server_url(server_url) + "/Shows/" + url_encode(series_id) +
           "/Seasons?userId=" + url_encode(user_id);
}

std::string build_episodes_url(const std::string& server_url, const std::string& series_id,
                               const std::string& season_id, const std::string& user_id) {
    std::string url = normalize_server_url(server_url) + "/Shows/" +
                      url_encode(series_id) +
                      "/Episodes?userId=" + url_encode(user_id);
    if (!season_id.empty()) {
        url += "&seasonId=" + url_encode(season_id);
    }
    return url;
}

std::string build_image_url(const std::string& server_url, const std::string& item_id,
                            const std::string& image_tag, int max_width) {
    std::string url = normalize_server_url(server_url) + "/Items/" +
                      url_encode(item_id) + "/Images/Primary";
    char width[32];
    std::snprintf(width, sizeof(width), "?maxWidth=%d&quality=90", max_width);
    url += width;
    if (!image_tag.empty()) {
        url += "&tag=" + url_encode(image_tag);
    }
    return url;
}

std::string build_stream_url(const std::string& server_url, const std::string& item_id,
                             const std::string& access_token, const std::string& device_id,
                             long long start_time_ticks,
                             const std::string& play_session_id,
                             bool include_api_key, const TrackSelection& tracks) {
    std::string url = normalize_server_url(server_url) + "/Videos/" +
                      url_encode(item_id) +
                      "/stream.ts"
                      "?static=false"
                      "&videoCodec=h264"
                      "&audioCodec=aac"
                      "&maxWidth=1920"
                      "&maxHeight=1080"
                      "&audioChannels=2"
                      "&videoBitRate=8000000"
                      "&audioBitRate=192000"
                      "&deviceId=" + url_encode(device_id);
    if (include_api_key) {
        url += "&api_key=" + url_encode(access_token);
    }
    if (start_time_ticks > 0) {
        char ticks[32];
        std::snprintf(ticks, sizeof(ticks), "%lld", start_time_ticks);
        url += "&startTimeTicks=";
        url += ticks;
    }
    if (!play_session_id.empty()) {
        url += "&playSessionId=" + url_encode(play_session_id);
    }
    if (!tracks.media_source_id.empty()) {
        url += "&mediaSourceId=" + url_encode(tracks.media_source_id);
    }
    if (tracks.audio_index >= 0) {
        url += "&audioStreamIndex=" + std::to_string(tracks.audio_index);
    }
    if (tracks.subtitle_delivery == SubtitleDelivery::Encode && tracks.subtitle_index >= 0) {
        url += "&subtitleStreamIndex=" + std::to_string(tracks.subtitle_index) +
               "&subtitleMethod=Encode";
    }
    return url;
}

std::string build_subtitle_url(const std::string& server_url, const std::string& item_id,
                               const std::string& media_source_id, int stream_index) {
    return normalize_server_url(server_url) + "/Videos/" + url_encode(item_id) + "/" +
           url_encode(media_source_id.empty() ? item_id : media_source_id) + "/Subtitles/" +
           std::to_string(stream_index) + "/Stream.srt";
}

std::string build_item_url(const std::string& server_url, const std::string& user_id,
                           const std::string& item_id) {
    return normalize_server_url(server_url) + "/Users/" + url_encode(user_id) +
           "/Items/" + url_encode(item_id);
}

std::string build_active_encodings_url(const std::string& server_url,
                                       const std::string& device_id,
                                       const std::string& play_session_id) {
    return normalize_server_url(server_url) + "/Videos/ActiveEncodings?deviceId=" +
           url_encode(device_id) + "&playSessionId=" + url_encode(play_session_id);
}

std::string mask_api_key(const std::string& url) {
    const std::string key = "api_key=";
    std::string out = url;
    size_t pos = out.find(key);
    if (pos != std::string::npos) {
        size_t value_start = pos + key.size();
        size_t value_end = out.find('&', value_start);
        if (value_end == std::string::npos) {
            value_end = out.size();
        }
        out.replace(value_start, value_end - value_start, "********");
    }
    return out;
}

}  // namespace api
