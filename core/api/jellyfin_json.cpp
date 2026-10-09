// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "api/jellyfin_json.h"

#include <cjson/cJSON.h>

namespace api {

namespace {

// Copies a JSON string to out; leaves out untouched if absent or not a string.
void read_string(const cJSON* obj, const char* key, std::string* out) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(v) && v->valuestring) {
        *out = v->valuestring;
    }
}

void read_int(const cJSON* obj, const char* key, int* out) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(v)) {
        *out = v->valueint;
    }
}

void read_int64(const cJSON* obj, const char* key, long long* out) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(v)) {
        *out = static_cast<long long>(v->valuedouble);
    }
}

void read_bool(const cJSON* obj, const char* key, bool* out) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(v)) {
        *out = cJSON_IsTrue(v);
    }
}

// Enumeration serialized as text ("Smart"); a number is also accepted
// (server enumeration order: Default, Always, OnlyForced, None, Smart).
SubtitleMode parse_subtitle_mode(const cJSON* v) {
    if (cJSON_IsNumber(v)) {
        switch (v->valueint) {
            case 1: return SubtitleMode::Always;
            case 2: return SubtitleMode::OnlyForced;
            case 3: return SubtitleMode::None;
            case 4: return SubtitleMode::Smart;
            default: return SubtitleMode::Default;
        }
    }
    if (cJSON_IsString(v) && v->valuestring) {
        const std::string name = v->valuestring;
        if (name == "Always") return SubtitleMode::Always;
        if (name == "OnlyForced") return SubtitleMode::OnlyForced;
        if (name == "None") return SubtitleMode::None;
        if (name == "Smart") return SubtitleMode::Smart;
    }
    return SubtitleMode::Default;
}

void parse_preferences(const cJSON* user, UserPreferences* out) {
    const cJSON* cfg = cJSON_GetObjectItemCaseSensitive(user, "Configuration");
    if (!cJSON_IsObject(cfg)) {
        return;
    }
    read_string(cfg, "AudioLanguagePreference", &out->audio_language);
    read_bool(cfg, "PlayDefaultAudioTrack", &out->play_default_audio);
    read_string(cfg, "SubtitleLanguagePreference", &out->subtitle_language);
    out->subtitle_mode = parse_subtitle_mode(cJSON_GetObjectItemCaseSensitive(cfg, "SubtitleMode"));
    read_bool(cfg, "RememberAudioSelections", &out->remember_audio);
    read_bool(cfg, "RememberSubtitleSelections", &out->remember_subtitles);
}

StreamKind parse_stream_kind(const cJSON* obj) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, "Type");
    if (cJSON_IsString(v) && v->valuestring) {
        const std::string type = v->valuestring;
        if (type == "Audio") return StreamKind::Audio;
        if (type == "Subtitle") return StreamKind::Subtitle;
        if (type == "Video") return StreamKind::Video;
    }
    return StreamKind::Other;
}

void parse_media_stream(const cJSON* obj, MediaStreamInfo* out) {
    out->kind = parse_stream_kind(obj);
    read_int(obj, "Index", &out->index);
    read_string(obj, "Codec", &out->codec);
    read_string(obj, "Language", &out->language);
    read_string(obj, "Title", &out->title);
    read_string(obj, "DisplayTitle", &out->display_title);
    read_string(obj, "ChannelLayout", &out->channel_layout);
    read_int(obj, "Channels", &out->channels);
    read_bool(obj, "IsDefault", &out->is_default);
    read_bool(obj, "IsForced", &out->is_forced);
    read_bool(obj, "IsExternal", &out->is_external);
    read_bool(obj, "IsHearingImpaired", &out->is_hearing_impaired);
    read_bool(obj, "IsOriginal", &out->is_original);
    read_bool(obj, "IsTextSubtitleStream", &out->is_text);
    read_bool(obj, "SupportsExternalStream", &out->supports_external);
}

void parse_media_source(const cJSON* obj, MediaSourceInfo* out) {
    read_string(obj, "Id", &out->id);
    read_string(obj, "Container", &out->container);
    const cJSON* audio = cJSON_GetObjectItemCaseSensitive(obj, "DefaultAudioStreamIndex");
    const cJSON* sub = cJSON_GetObjectItemCaseSensitive(obj, "DefaultSubtitleStreamIndex");
    out->server_defaults = cJSON_IsNumber(audio) || cJSON_IsNumber(sub);
    if (cJSON_IsNumber(audio)) out->default_audio_index = audio->valueint;
    if (cJSON_IsNumber(sub)) out->default_subtitle_index = sub->valueint;
    const cJSON* streams = cJSON_GetObjectItemCaseSensitive(obj, "MediaStreams");
    const cJSON* entry = nullptr;
    cJSON_ArrayForEach(entry, streams) {
        MediaStreamInfo info;
        parse_media_stream(entry, &info);
        if (info.index >= 0 && info.kind != StreamKind::Other) {
            out->streams.push_back(info);
        }
    }
}

void parse_media_item(const cJSON* obj, MediaItem* out) {
    read_string(obj, "Id", &out->id);
    read_string(obj, "Name", &out->name);
    read_string(obj, "Type", &out->type);
    read_string(obj, "Overview", &out->overview);
    read_string(obj, "SeriesName", &out->series_name);
    read_string(obj, "SeriesId", &out->series_id);
    read_string(obj, "SeriesPrimaryImageTag", &out->series_primary_image_tag);
    read_string(obj, "SortName", &out->sort_name);
    read_int(obj, "ProductionYear", &out->production_year);
    read_int(obj, "IndexNumber", &out->index_number);
    read_int(obj, "ParentIndexNumber", &out->parent_index_number);
    read_int64(obj, "RunTimeTicks", &out->runtime_ticks);

    const cJSON* tags = cJSON_GetObjectItemCaseSensitive(obj, "ImageTags");
    if (cJSON_IsObject(tags)) {
        read_string(tags, "Primary", &out->primary_image_tag);
    }

    const cJSON* user_data = cJSON_GetObjectItemCaseSensitive(obj, "UserData");
    if (cJSON_IsObject(user_data)) {
        read_int64(user_data, "PlaybackPositionTicks", &out->playback_position_ticks);
        out->played = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(user_data, "Played"));
    }

    const cJSON* sources = cJSON_GetObjectItemCaseSensitive(obj, "MediaSources");
    if (cJSON_IsArray(sources) && cJSON_GetArraySize(sources) > 0) {
        read_string(cJSON_GetArrayItem(sources, 0), "Id", &out->media_source_id);
        out->media_sources.clear();  // out may be an already read item (re-read)
        const cJSON* source = nullptr;
        cJSON_ArrayForEach(source, sources) {
            MediaSourceInfo info;
            parse_media_source(source, &info);
            out->media_sources.push_back(info);
        }
    }
}

}  // namespace

bool parse_item_response(const std::string& json, MediaItem* out) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) {
        return false;
    }
    bool ok = cJSON_IsObject(root);
    if (ok) {
        parse_media_item(root, out);
        ok = !out->id.empty();
    }
    cJSON_Delete(root);
    return ok;
}

bool parse_auth_response(const std::string& json, AuthSession* out) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) {
        return false;
    }

    bool ok = false;
    read_string(root, "AccessToken", &out->access_token);
    read_string(root, "ServerId", &out->server_id);
    const cJSON* user = cJSON_GetObjectItemCaseSensitive(root, "User");
    if (cJSON_IsObject(user)) {
        read_string(user, "Id", &out->user_id);
        read_string(user, "Name", &out->user_name);
        parse_preferences(user, &out->preferences);
    }
    ok = !out->access_token.empty() && !out->user_id.empty();

    cJSON_Delete(root);
    return ok;
}

bool parse_user_response(const std::string& json, AuthSession* out) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) {
        return false;
    }
    read_string(root, "Id", &out->user_id);
    read_string(root, "Name", &out->user_name);
    read_string(root, "ServerId", &out->server_id);
    parse_preferences(root, &out->preferences);
    const bool ok = cJSON_IsObject(root) && !out->user_id.empty();
    cJSON_Delete(root);
    return ok;
}

bool parse_views_response(const std::string& json, std::vector<Library>* out) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) {
        return false;
    }

    const cJSON* items = cJSON_GetObjectItemCaseSensitive(root, "Items");
    bool ok = cJSON_IsArray(items);
    if (ok) {
        const cJSON* entry = nullptr;
        cJSON_ArrayForEach(entry, items) {
            Library lib;
            read_string(entry, "Id", &lib.id);
            read_string(entry, "Name", &lib.name);
            read_string(entry, "CollectionType", &lib.collection_type);
            if (!lib.id.empty()) {
                out->push_back(lib);
            }
        }
    }

    cJSON_Delete(root);
    return ok;
}

bool parse_items_response(const std::string& json, ItemList* out) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) {
        return false;
    }

    const cJSON* items = cJSON_GetObjectItemCaseSensitive(root, "Items");
    bool ok = cJSON_IsArray(items);
    if (ok) {
        const cJSON* entry = nullptr;
        cJSON_ArrayForEach(entry, items) {
            MediaItem item;
            parse_media_item(entry, &item);
            if (!item.id.empty()) {
                out->items.push_back(item);
            }
        }
        out->total_count = static_cast<int>(out->items.size());
        read_int(root, "TotalRecordCount", &out->total_count);
    }

    cJSON_Delete(root);
    return ok;
}

std::string build_auth_body(const std::string& username, const std::string& password) {
    std::string body;
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return body;
    }
    cJSON_AddStringToObject(root, "Username", username.c_str());
    cJSON_AddStringToObject(root, "Pw", password.c_str());
    char* printed = cJSON_PrintUnformatted(root);
    if (printed) {
        body = printed;
        cJSON_free(printed);
    }
    cJSON_Delete(root);
    return body;
}

}  // namespace api
