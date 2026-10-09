// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_API_JELLYFIN_MODELS_H
#define PELAGIA_CORE_API_JELLYFIN_MODELS_H

// Data structures of the Jellyfin client (subset of the API responses
// needed by v1).

#include <optional>
#include <string>
#include <vector>

namespace api {

// Subtitle mode of the profile (UserConfiguration.SubtitleMode).
enum class SubtitleMode { Default, Always, OnlyForced, None, Smart };

// Playback preferences of the Jellyfin profile (UserConfiguration). Default
// values = the server's for a new profile.
struct UserPreferences {
    std::string audio_language;     // empty = no preference (may be "OriginalLanguage")
    bool play_default_audio = false;
    std::string subtitle_language;  // empty = no preference
    SubtitleMode subtitle_mode = SubtitleMode::Default;
    bool remember_audio = true;
    bool remember_subtitles = true;
};

// Session opened after POST /Users/AuthenticateByName.
struct AuthSession {
    std::string user_id;
    std::string user_name;
    std::string access_token;
    std::string server_id;
    UserPreferences preferences;
};

// A library ("view"): Movies, TV shows, ...
struct Library {
    std::string id;
    std::string name;
    std::string collection_type;  // "movies", "tvshows", ... (may be empty)
};

enum class StreamKind { Other, Video, Audio, Subtitle };

// A track of MediaSources[].MediaStreams.
struct MediaStreamInfo {
    int index = -1;  // MediaStream.Index: value of audioStreamIndex / subtitleStreamIndex
    StreamKind kind = StreamKind::Other;
    std::string codec;           // "eac3", "truehd", "subrip", "ass", "PGSSUB"...
    std::string language;        // ISO 639-2, may be empty
    std::string title;           // Title (track title in the file)
    std::string display_title;   // DisplayTitle computed by the server
    std::string channel_layout;  // "5.1", "stereo"...
    int channels = 0;
    bool is_default = false;
    bool is_forced = false;
    bool is_external = false;
    bool is_hearing_impaired = false;
    bool is_original = false;  // absent from 10.x versions
    // Subtitles: IsTextSubtitleStream (text) and SupportsExternalStream
    // (the server can provide them separately).
    bool is_text = false;
    bool supports_external = false;
};

// A version of an item (MediaSources[]) with its tracks and the default tracks
// computed by the server for the user (preferences, remembered choices).
struct MediaSourceInfo {
    std::string id;
    std::string container;
    std::vector<MediaStreamInfo> streams;
    // Present in the server response: DefaultAudioStreamIndex and
    // DefaultSubtitleStreamIndex (-1 = explicit "none", absent = none).
    bool server_defaults = false;
    int default_audio_index = -1;
    int default_subtitle_index = -1;
};

// A media item: movie, series, season or episode.
struct MediaItem {
    std::string id;
    std::string name;
    std::string type;  // "Movie", "Series", "Season", "Episode"
    std::string overview;
    std::string series_name;        // episodes/seasons
    std::string series_id;          // episodes/seasons: parent series
    std::string series_primary_image_tag;  // series poster (episodes)
    std::string sort_name;          // SortName (may contain invisible characters)
    std::string primary_image_tag;  // ImageTags.Primary (empty if no poster)
    int production_year = 0;
    int index_number = -1;         // episode (or season) number
    int parent_index_number = -1;  // season number for an episode
    long long runtime_ticks = 0;   // duration in Jellyfin ticks (1 tick = 100 ns)
    // UserData: resume position saved by the server, and "played" state.
    long long playback_position_ticks = 0;
    bool played = false;
    std::string media_source_id;   // MediaSources[0].Id (playback reporting)
    std::vector<MediaSourceInfo> media_sources;  // tracks (GET of a single item only)
};

struct ItemList {
    std::vector<MediaItem> items;
    int total_count = 0;
};

// Parameters of GET /Users/{userId}/Items.
struct ItemsQuery {
    std::string parent_id;           // library/folder id (optional)
    std::string include_item_types;  // "Movie", "Series", ... (optionnel)
    bool recursive = false;
    // collapseBoxSetItems: false to get individual movies instead of
    // BoxSets (collections). Not sent when unset.
    std::optional<bool> collapse_box_set_items;
    // Sorting and paging (default: ascending SortName, no limit).
    std::string sort_by = "SortName";
    bool sort_descending = false;
    int limit = 0;  // 0 = no limit
};

}  // namespace api

#endif  // PELAGIA_CORE_API_JELLYFIN_MODELS_H
