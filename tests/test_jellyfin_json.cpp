// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Parsing tests of Jellyfin JSON responses, with mocked responses
// modeled on the official OpenAPI spec (AuthenticationResult,
// BaseItemDtoQueryResult).

#include "api/jellyfin_json.h"

#include <string>

#include "test_framework.h"

namespace {

// Response of POST /Users/AuthenticateByName (useful fields only).
const char kAuthResponse[] = R"json({
  "User": {
    "Name": "alice",
    "ServerId": "f4f0b7f1a0c34a3a9a1e3f1b2c3d4e5f",
    "Id": "e8837bc1ad67520e8cd2f629e3155721",
    "HasPassword": true
  },
  "SessionInfo": { "Id": "sess1" },
  "AccessToken": "0123456789abcdef0123456789abcdef",
  "ServerId": "f4f0b7f1a0c34a3a9a1e3f1b2c3d4e5f"
})json";

// Response of GET /Users/{userId}/Views.
const char kViewsResponse[] = R"json({
  "Items": [
    {
      "Name": "Films",
      "ServerId": "f4f0b7f1a0c34a3a9a1e3f1b2c3d4e5f",
      "Id": "f137a2dd21bbc1b99aa5c0f6bf02a805",
      "Type": "CollectionFolder",
      "CollectionType": "movies"
    },
    {
      "Name": "Séries",
      "Id": "767bffe4f11c93ef34b805451a696a4e",
      "Type": "CollectionFolder",
      "CollectionType": "tvshows"
    }
  ],
  "TotalRecordCount": 2,
  "StartIndex": 0
})json";

// Response of GET /Users/{userId}/Items (movies).
const char kMoviesResponse[] = R"json({
  "Items": [
    {
      "Name": "Big Buck Bunny",
      "Id": "aa0d7de1f96b45cd9ea468bfd17e835c",
      "Type": "Movie",
      "ProductionYear": 2008,
      "RunTimeTicks": 5964800000,
      "Overview": "Un lapin géant se venge.",
      "ImageTags": { "Primary": "c90d4f4c6e0b2f5d" }
    },
    {
      "Name": "Sintel",
      "Id": "bb1e8ef2a07c56de0fb579c0e28f946d",
      "Type": "Movie",
      "ProductionYear": 2010,
      "RunTimeTicks": 8880000000
    }
  ],
  "TotalRecordCount": 42,
  "StartIndex": 0
})json";

// Response of GET /Shows/{seriesId}/Episodes.
const char kEpisodesResponse[] = R"json({
  "Items": [
    {
      "Name": "Pilote",
      "Id": "cc2f9f03b18d67ef1a68ad1f39fa57ae",
      "Type": "Episode",
      "SeriesName": "Ma Série",
      "IndexNumber": 1,
      "ParentIndexNumber": 1,
      "RunTimeTicks": 25200000000
    }
  ],
  "TotalRecordCount": 1,
  "StartIndex": 0
})json";

// Profile preferences (User.Configuration) as returned by the server
// of a real server (Jellyfin 12.1.0): no audio language, text SubtitleMode.
const char kUserWithConfiguration[] = R"json({
  "Name": "pelagia-test",
  "Id": "e8837bc1ad67520e8cd2f629e3155721",
  "ServerId": "f4f0b7f1a0c34a3a9a1e3f1b2c3d4e5f",
  "Configuration": {
    "PlayDefaultAudioTrack": true,
    "SubtitleLanguagePreference": "",
    "DisplayMissingEpisodes": false,
    "SubtitleMode": "Default",
    "RememberAudioSelections": true,
    "RememberSubtitleSelections": true
  }
})json";

// Item with tracks (excerpt of "1917", Jellyfin 12.1.0): HEVC, English TrueHD,
// French E-AC3, two French PGS; defaults computed by the server.
const char kItemWithStreams[] = R"json({
  "Name": "1917",
  "Id": "04c122101d00f63d8eceb4857bd1a552",
  "Type": "Movie",
  "MediaSources": [{
    "Id": "04c122101d00f63d8eceb4857bd1a552",
    "Container": "mkv",
    "DefaultAudioStreamIndex": 2,
    "DefaultSubtitleStreamIndex": 4,
    "MediaStreams": [
      {"Index": 0, "Type": "Video", "Codec": "hevc", "IsDefault": true},
      {"Index": 1, "Type": "Audio", "Codec": "truehd", "Language": "eng", "Channels": 8,
       "ChannelLayout": "7.1", "Title": "TrueHD Atmos", "DisplayTitle": "English - TRUEHD - 7.1",
       "IsDefault": false, "IsForced": false, "IsOriginal": false},
      {"Index": 2, "Type": "Audio", "Codec": "eac3", "Language": "fre", "Channels": 6,
       "ChannelLayout": "5.1", "IsDefault": true},
      {"Index": 3, "Type": "Subtitle", "Codec": "PGSSUB", "Language": "fre",
       "IsTextSubtitleStream": false, "SupportsExternalStream": false, "IsExternal": false},
      {"Index": 4, "Type": "Subtitle", "Codec": "PGSSUB", "Language": "fre", "IsDefault": true,
       "IsForced": false, "IsTextSubtitleStream": false},
      {"Index": 5, "Type": "Subtitle", "Codec": "subrip", "Language": "eng", "IsExternal": true,
       "IsTextSubtitleStream": true, "SupportsExternalStream": true, "IsHearingImpaired": true},
      {"Index": 6, "Type": "Attachment", "Codec": "ttf"},
      {"Type": "Audio", "Codec": "sans index"}
    ]
  }, {
    "Id": "autre", "MediaStreams": []
  }]
})json";

}  // namespace

int main() {
    // Authentification.
    api::AuthSession session;
    CHECK(api::parse_auth_response(kAuthResponse, &session));
    CHECK_EQ(session.access_token, "0123456789abcdef0123456789abcdef");
    CHECK_EQ(session.user_id, "e8837bc1ad67520e8cd2f629e3155721");
    CHECK_EQ(session.user_name, "alice");
    CHECK_EQ(session.server_id, "f4f0b7f1a0c34a3a9a1e3f1b2c3d4e5f");

    // Authentication failures: invalid JSON or missing fields.
    api::AuthSession bad;
    CHECK(!api::parse_auth_response("pas du json", &bad));
    CHECK(!api::parse_auth_response("{}", &bad));
    CHECK(!api::parse_auth_response(R"({"User":{"Id":"x"}})", &bad));

    // Libraries.
    std::vector<api::Library> libraries;
    CHECK(api::parse_views_response(kViewsResponse, &libraries));
    CHECK_EQ(libraries.size(), 2u);
    CHECK_EQ(libraries[0].name, "Films");
    CHECK_EQ(libraries[0].collection_type, "movies");
    CHECK_EQ(libraries[1].id, "767bffe4f11c93ef34b805451a696a4e");
    std::vector<api::Library> none;
    CHECK(!api::parse_views_response(R"({"pas":"de champ Items"})", &none));

    // Films.
    api::ItemList movies;
    CHECK(api::parse_items_response(kMoviesResponse, &movies));
    CHECK_EQ(movies.items.size(), 2u);
    CHECK_EQ(movies.total_count, 42);
    CHECK_EQ(movies.items[0].name, "Big Buck Bunny");
    CHECK_EQ(movies.items[0].type, "Movie");
    CHECK_EQ(movies.items[0].production_year, 2008);
    CHECK_EQ(movies.items[0].runtime_ticks, 5964800000LL);
    CHECK_EQ(movies.items[0].primary_image_tag, "c90d4f4c6e0b2f5d");
    CHECK_EQ(movies.items[1].primary_image_tag, "");  // no ImageTags

    // Episodes.
    api::ItemList episodes;
    CHECK(api::parse_items_response(kEpisodesResponse, &episodes));
    CHECK_EQ(episodes.items.size(), 1u);
    CHECK_EQ(episodes.items[0].series_name, "Ma Série");
    CHECK_EQ(episodes.items[0].index_number, 1);
    CHECK_EQ(episodes.items[0].parent_index_number, 1);

    // Preferences: absent -> server defaults; present -> read.
    CHECK(session.preferences.audio_language.empty());
    CHECK(!session.preferences.play_default_audio);
    CHECK(session.preferences.subtitle_mode == api::SubtitleMode::Default);
    CHECK(session.preferences.remember_audio);
    api::AuthSession me;
    CHECK(api::parse_user_response(kUserWithConfiguration, &me));
    CHECK(me.preferences.play_default_audio);
    CHECK(me.preferences.audio_language.empty());
    CHECK(me.preferences.subtitle_language.empty());
    CHECK(me.preferences.subtitle_mode == api::SubtitleMode::Default);
    CHECK(me.preferences.remember_audio && me.preferences.remember_subtitles);
    const char* modes[] = {"Default", "Always", "OnlyForced", "None", "Smart"};
    const api::SubtitleMode expected[] = {api::SubtitleMode::Default, api::SubtitleMode::Always,
                                          api::SubtitleMode::OnlyForced, api::SubtitleMode::None,
                                          api::SubtitleMode::Smart};
    for (int i = 0; i < 5; ++i) {
        // Enumeration as text, then as number (server order).
        for (int numeric = 0; numeric < 2; ++numeric) {
            std::string json = std::string(R"({"Id":"u","Configuration":{"AudioLanguagePreference":"fre",)") +
                               R"("SubtitleLanguagePreference":"eng","RememberAudioSelections":false,"SubtitleMode":)" +
                               (numeric ? std::to_string(i) : std::string("\"") + modes[i] + "\"") + "}}";
            api::AuthSession u;
            CHECK(api::parse_user_response(json, &u));
            CHECK(u.preferences.subtitle_mode == expected[i]);
            CHECK_EQ(u.preferences.audio_language, "fre");
            CHECK_EQ(u.preferences.subtitle_language, "eng");
            CHECK(!u.preferences.remember_audio);
        }
    }

    // Item with tracks.
    api::MediaItem film;
    CHECK(api::parse_item_response(kItemWithStreams, &film));
    CHECK_EQ(film.media_source_id, "04c122101d00f63d8eceb4857bd1a552");
    CHECK_EQ(film.media_sources.size(), 2u);
    const api::MediaSourceInfo& src = film.media_sources[0];
    CHECK_EQ(src.container, "mkv");
    CHECK(src.server_defaults);
    CHECK_EQ(src.default_audio_index, 2);
    CHECK_EQ(src.default_subtitle_index, 4);
    CHECK_EQ(src.streams.size(), 6u);  // attachment and track without an index ignored
    CHECK(src.streams[1].kind == api::StreamKind::Audio);
    CHECK_EQ(src.streams[1].codec, "truehd");
    CHECK_EQ(src.streams[1].language, "eng");
    CHECK_EQ(src.streams[1].channels, 8);
    CHECK_EQ(src.streams[1].channel_layout, "7.1");
    CHECK_EQ(src.streams[1].title, "TrueHD Atmos");
    CHECK_EQ(src.streams[1].display_title, "English - TRUEHD - 7.1");
    CHECK(!src.streams[1].is_original);
    CHECK(src.streams[2].is_default);
    CHECK(src.streams[4].kind == api::StreamKind::Subtitle);
    CHECK(src.streams[4].is_default && !src.streams[4].is_forced && !src.streams[4].is_text);
    CHECK(src.streams[5].is_external && src.streams[5].is_text && src.streams[5].supports_external);
    CHECK(src.streams[5].is_hearing_impaired);
    CHECK(!film.media_sources[1].server_defaults);  // no announced defaults
    CHECK_EQ(film.media_sources[1].default_audio_index, -1);

    // A list item (without MediaStreams) has no tracks.
    CHECK(movies.items[0].media_sources.empty());

    // Body of the authentication request.
    std::string body = api::build_auth_body("alice", "s3cr\"et");
    CHECK(body.find("\"Username\":\"alice\"") != std::string::npos);
    CHECK(body.find("\"Pw\":\"s3cr\\\"et\"") != std::string::npos);

    return testfw::test_failures();
}
