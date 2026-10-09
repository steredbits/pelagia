// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Tests of Jellyfin URL construction (pure functions, no network).

#include "api/jellyfin_urls.h"

#include <string>

#include "test_framework.h"

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

int main() {
    // Encodage URL.
    CHECK_EQ(api::url_encode("abc-123_~.ok"), "abc-123_~.ok");
    CHECK_EQ(api::url_encode("a b&c/é"), "a%20b%26c%2F%C3%A9");

    // Normalization of the server URL.
    CHECK_EQ(api::normalize_server_url("http://10.0.0.2:8096/"), "http://10.0.0.2:8096");
    CHECK_EQ(api::normalize_server_url("http://10.0.0.2:8096"), "http://10.0.0.2:8096");

    // Auth and views.
    CHECK_EQ(api::build_auth_url("http://srv:8096/"), "http://srv:8096/Users/AuthenticateByName");
    CHECK_EQ(api::build_views_url("http://srv:8096", "u1"), "http://srv:8096/Users/u1/Views");

    // Items: optional parameters.
    api::ItemsQuery query;
    query.parent_id = "lib42";
    query.include_item_types = "Movie";
    query.recursive = true;
    std::string items = api::build_items_url("http://srv:8096", "u1", query);
    CHECK(contains(items, "http://srv:8096/Users/u1/Items?"));
    CHECK(contains(items, "parentId=lib42"));
    CHECK(contains(items, "includeItemTypes=Movie"));
    CHECK(contains(items, "recursive=true"));

    api::ItemsQuery empty_query;
    std::string items_plain = api::build_items_url("http://srv:8096", "u1", empty_query);
    CHECK(!contains(items_plain, "parentId"));
    CHECK(!contains(items_plain, "includeItemTypes"));
    CHECK(!contains(items_plain, "recursive"));
    // collapseBoxSetItems: absent by default, sent only if set.
    CHECK(!contains(items_plain, "collapseBoxSetItems"));
    api::ItemsQuery no_boxsets;
    no_boxsets.collapse_box_set_items = false;
    CHECK(contains(api::build_items_url("http://srv:8096", "u1", no_boxsets),
                   "collapseBoxSetItems=false"));
    api::ItemsQuery with_boxsets;
    with_boxsets.collapse_box_set_items = true;
    CHECK(contains(api::build_items_url("http://srv:8096", "u1", with_boxsets),
                   "collapseBoxSetItems=true"));

    // Seasons / episodes.
    CHECK_EQ(api::build_seasons_url("http://srv:8096", "serie1", "u1"),
             "http://srv:8096/Shows/serie1/Seasons?userId=u1");
    std::string episodes = api::build_episodes_url("http://srv:8096", "serie1", "s2", "u1");
    CHECK(contains(episodes, "/Shows/serie1/Episodes?userId=u1"));
    CHECK(contains(episodes, "seasonId=s2"));

    // Image : tag optionnel.
    std::string image = api::build_image_url("http://srv:8096", "item9", "tag123", 400);
    CHECK(contains(image, "/Items/item9/Images/Primary?maxWidth=400"));
    CHECK(contains(image, "tag=tag123"));
    std::string image_no_tag = api::build_image_url("http://srv:8096", "item9", "", 400);
    CHECK(!contains(image_no_tag, "tag="));

    // Stream: H.264 1080p + AAC stereo transcoding required by v1.
    std::string stream = api::build_stream_url("http://srv:8096/", "item9", "tok", "dev1");
    CHECK(contains(stream, "http://srv:8096/Videos/item9/stream.ts?"));
    CHECK(contains(stream, "static=false"));
    CHECK(contains(stream, "videoCodec=h264"));
    CHECK(contains(stream, "audioCodec=aac"));
    CHECK(contains(stream, "maxWidth=1920"));
    CHECK(contains(stream, "maxHeight=1080"));
    CHECK(contains(stream, "audioChannels=2"));
    CHECK(contains(stream, "deviceId=dev1"));
    CHECK(contains(stream, "api_key=tok"));
    // No start position or playback session by default.
    CHECK(!contains(stream, "startTimeTicks"));
    CHECK(!contains(stream, "playSessionId"));

    // Server-side seek: optional startTimeTicks + playSessionId.
    std::string stream_seek =
        api::build_stream_url("http://srv:8096", "item9", "tok", "dev1",
                              6000000000LL, "play-42");
    CHECK(contains(stream_seek, "startTimeTicks=6000000000"));
    CHECK(contains(stream_seek, "playSessionId=play-42"));

    // Tracks: nothing by default (compatible with the base URL).
    CHECK(!contains(stream, "mediaSourceId"));
    CHECK(!contains(stream, "audioStreamIndex"));
    CHECK(!contains(stream, "subtitleStreamIndex"));
    CHECK(!contains(stream, "subtitleMethod"));

    // Source and audio chosen, without subtitles.
    api::TrackSelection tracks;
    tracks.media_source_id = "src 1";
    tracks.audio_index = 2;
    std::string with_audio = api::build_stream_url("http://srv:8096", "item9", "tok", "dev1", 0, "",
                                                   true, tracks);
    CHECK(contains(with_audio, "&mediaSourceId=src%201"));
    CHECK(contains(with_audio, "&audioStreamIndex=2"));
    CHECK(!contains(with_audio, "subtitle"));

    // Subtitles rendered by the client: nothing in the URL (the stream stays unchanged,
    // so direct streaming is possible).
    tracks.subtitle_index = 4;
    tracks.subtitle_delivery = api::SubtitleDelivery::Client;
    std::string client_subs = api::build_stream_url("http://srv:8096", "item9", "tok", "dev1", 0,
                                                    "", true, tracks);
    CHECK(!contains(client_subs, "subtitle"));
    CHECK_EQ(client_subs, with_audio);

    // Subtitles burned in by the server.
    tracks.subtitle_delivery = api::SubtitleDelivery::Encode;
    std::string burned = api::build_stream_url("http://srv:8096", "item9", "tok", "dev1",
                                               6000000000LL, "play-42", false, tracks);
    CHECK(contains(burned, "&subtitleStreamIndex=4&subtitleMethod=Encode"));
    CHECK(contains(burned, "&audioStreamIndex=2"));
    CHECK(contains(burned, "startTimeTicks=6000000000"));
    CHECK(!contains(burned, "api_key"));
    // Index at -1: no burn-in, even if the mode is Encode.
    tracks.subtitle_index = -1;
    CHECK(!contains(api::build_stream_url("http://srv:8096", "item9", "tok", "dev1", 0, "", true,
                                          tracks), "subtitle"));

    // SRT subtitles: item's source by default, encoded identifiers.
    CHECK_EQ(api::build_subtitle_url("http://srv:8096/", "item9", "src 1", 6),
             "http://srv:8096/Videos/item9/src%201/Subtitles/6/Stream.srt");
    CHECK_EQ(api::build_subtitle_url("http://srv:8096", "item9", "", 0),
             "http://srv:8096/Videos/item9/item9/Subtitles/0/Stream.srt");

    // Token masking for display/logs.
    CHECK_EQ(api::mask_api_key("http://s/v?api_key=secret123"),
             "http://s/v?api_key=********");
    CHECK_EQ(api::mask_api_key("http://s/v?api_key=secret123&x=1"),
             "http://s/v?api_key=********&x=1");
    CHECK_EQ(api::mask_api_key("http://s/v?x=1"), "http://s/v?x=1");
    std::string masked = api::mask_api_key(stream_seek);
    CHECK(!contains(masked, "tok"));
    CHECK(contains(masked, "api_key=********"));
    CHECK(contains(masked, "playSessionId=play-42"));

    return testfw::test_failures();
}
