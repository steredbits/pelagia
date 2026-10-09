// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Track selection: pure fallback (port of MediaStreamSelector), server
// default tracks, manual choices and subtitle delivery. No rendering.

#include "api/track_selection.h"

#include "test_framework.h"

namespace {

using api::MediaSourceInfo;
using api::MediaStreamInfo;
using api::StreamKind;
using api::SubtitleDelivery;
using api::SubtitleMode;
using api::UserPreferences;

MediaStreamInfo track(int index, StreamKind kind, const char* codec, const char* lang,
                      bool is_default = false, bool forced = false) {
    MediaStreamInfo s;
    s.index = index;
    s.kind = kind;
    s.codec = codec;
    s.language = lang;
    s.is_default = is_default;
    s.is_forced = forced;
    return s;
}

MediaStreamInfo audio(int index, const char* lang, bool is_default = false) {
    return track(index, StreamKind::Audio, "eac3", lang, is_default);
}

MediaStreamInfo text_sub(int index, const char* lang, bool is_default = false, bool forced = false) {
    MediaStreamInfo s = track(index, StreamKind::Subtitle, "subrip", lang, is_default, forced);
    s.is_text = true;
    s.supports_external = true;
    return s;
}

MediaStreamInfo image_sub(int index, const char* lang, bool is_default = false, bool forced = false) {
    return track(index, StreamKind::Subtitle, "PGSSUB", lang, is_default, forced);
}

UserPreferences prefs(const char* audio_lang, const char* sub_lang, SubtitleMode mode,
                      bool play_default = false) {
    UserPreferences p;
    p.audio_language = audio_lang;
    p.subtitle_language = sub_lang;
    p.subtitle_mode = mode;
    p.play_default_audio = play_default;
    return p;
}

// Real movie ("1917", Jellyfin 12.1.0): HEVC video, English TrueHD (1)
// and French E-AC3 (2) audio, two French PGS (3, 4) none of them forced.
// Profile: SubtitleMode Default, PlayDefaultAudioTrack, no preferred language.
MediaSourceInfo movie_1917() {
    MediaSourceInfo src;
    src.id = "src1";
    src.streams.push_back(track(0, StreamKind::Video, "hevc", ""));
    src.streams.push_back(track(1, StreamKind::Audio, "truehd", "eng"));
    src.streams.push_back(track(2, StreamKind::Audio, "eac3", "fre", true));
    src.streams.push_back(image_sub(3, "fre"));
    src.streams.push_back(image_sub(4, "fre", true));
    return src;
}

// Typical subtitled movie: English (default) and French audio, full French
// subtitles (text), forced (text) and English.
MediaSourceInfo vostfr() {
    MediaSourceInfo src;
    src.id = "v";
    src.streams.push_back(track(0, StreamKind::Video, "h264", ""));
    src.streams.push_back(audio(1, "eng", true));
    src.streams.push_back(audio(2, "fre"));
    src.streams.push_back(text_sub(3, "eng"));
    src.streams.push_back(text_sub(4, "fre"));
    src.streams.push_back(text_sub(5, "fre", false, true));
    src.streams.push_back(image_sub(6, "fre"));
    return src;
}

void test_audio_selection() {
    const MediaSourceInfo src = vostfr();
    // Preferred language > default track > first track.
    CHECK_EQ(api::select_default_audio(src, prefs("fre", "", SubtitleMode::Default)), 2);
    CHECK_EQ(api::select_default_audio(src, prefs("fra", "", SubtitleMode::Default)), 2);
    CHECK_EQ(api::select_default_audio(src, prefs("eng", "", SubtitleMode::Default)), 1);
    // Without preference, the file's default track wins over the order.
    CHECK_EQ(api::select_default_audio(src, prefs("", "", SubtitleMode::Default)), 1);
    // PlayDefaultAudioTrack: the default track wins even against the preferred language.
    CHECK_EQ(api::select_default_audio(src, prefs("fre", "", SubtitleMode::Default, true)), 1);
    // Preferred language absent from the file: keep the default track.
    CHECK_EQ(api::select_default_audio(src, prefs("jpn", "", SubtitleMode::Default)), 1);
    // No default track nor preference: the first one.
    MediaSourceInfo plain;
    plain.streams.push_back(audio(5, "ger"));
    plain.streams.push_back(audio(6, "fre"));
    CHECK_EQ(api::select_default_audio(plain, prefs("", "", SubtitleMode::Default)), 5);
    // Without an audio track.
    MediaSourceInfo none;
    none.streams.push_back(track(0, StreamKind::Video, "h264", ""));
    CHECK_EQ(api::select_default_audio(none, prefs("fre", "", SubtitleMode::Default)), -1);
    // "Original language": the track marked IsOriginal, otherwise the default choice.
    MediaSourceInfo orig = vostfr();
    orig.streams[2].is_original = true;
    CHECK_EQ(api::select_default_audio(orig, prefs("OriginalLanguage", "", SubtitleMode::Default)), 2);
    CHECK_EQ(api::select_default_audio(src, prefs("OriginalLanguage", "", SubtitleMode::Default)), 1);
}

void test_subtitle_modes() {
    const MediaSourceInfo src = vostfr();
    // None: never subtitles.
    CHECK_EQ(api::select_default_subtitle(src, prefs("", "fre", SubtitleMode::None), "eng"), -1);
    // Default: only external / default / forced (none default here, forced no. 5 wins).
    CHECK_EQ(api::select_default_subtitle(src, prefs("", "fre", SubtitleMode::Default), "eng"), 5);
    // Always: full in the preferred language, even with the audio in that language.
    CHECK_EQ(api::select_default_subtitle(src, prefs("", "fre", SubtitleMode::Always), "fre"), 4);
    CHECK_EQ(api::select_default_subtitle(src, prefs("", "eng", SubtitleMode::Always), "fre"), 3);
    // OnlyForced: the forced one of the preferred language.
    CHECK_EQ(api::select_default_subtitle(src, prefs("", "fre", SubtitleMode::OnlyForced), "eng"), 5);
    CHECK_EQ(api::select_default_subtitle(src, prefs("", "eng", SubtitleMode::OnlyForced), "eng"), -1);
    // Smart, audio in another language: full subtitles of the preferred language (subbed).
    CHECK_EQ(api::select_default_subtitle(src, prefs("", "fre", SubtitleMode::Smart), "eng"), 4);
    // Smart, audio in the preferred language (dubbed): only the forced ones.
    CHECK_EQ(api::select_default_subtitle(src, prefs("", "fre", SubtitleMode::Smart), "fre"), 5);
    // Smart, preferred language absent from the file: none.
    CHECK_EQ(api::select_default_subtitle(src, prefs("", "jpn", SubtitleMode::Smart), "eng"), -1);
}

void test_subtitle_order_and_forced() {
    // A default subtitle comes before a full one of the preferred language (Default).
    MediaSourceInfo src;
    src.streams.push_back(text_sub(3, "fre"));
    src.streams.push_back(text_sub(4, "eng", true));
    CHECK_EQ(api::select_default_subtitle(src, prefs("", "fre", SubtitleMode::Default), "fre"), 4);
    // External first.
    MediaStreamInfo ext = text_sub(7, "eng");
    ext.is_external = true;
    src.streams.push_back(ext);
    CHECK_EQ(api::select_default_subtitle(src, prefs("", "fre", SubtitleMode::Default), "fre"), 7);

    // Forced: preferred language before undefined language; foreign language ignored.
    MediaSourceInfo forced;
    forced.streams.push_back(text_sub(1, "und", false, true));
    forced.streams.push_back(text_sub(2, "ger", false, true));
    forced.streams.push_back(text_sub(3, "fre", false, true));
    CHECK_EQ(api::select_default_subtitle(forced, prefs("", "fre", SubtitleMode::OnlyForced), "eng"), 3);
    forced.streams.pop_back();
    CHECK_EQ(api::select_default_subtitle(forced, prefs("", "fre", SubtitleMode::OnlyForced), "eng"), 1);
    forced.streams.erase(forced.streams.begin());
    CHECK_EQ(api::select_default_subtitle(forced, prefs("", "fre", SubtitleMode::OnlyForced), "eng"), -1);

    // Empty language preference: "any".
    MediaSourceInfo any;
    any.streams.push_back(text_sub(1, "jpn"));
    any.streams.push_back(text_sub(2, "fre"));
    CHECK_EQ(api::select_default_subtitle(any, prefs("", "", SubtitleMode::Smart), "eng"), 1);
    CHECK_EQ(api::select_default_subtitle(any, prefs("", "", SubtitleMode::Always), "eng"), 1);
}

void test_delivery() {
    CHECK(api::subtitle_delivery_for(text_sub(1, "fre")) == SubtitleDelivery::Client);
    CHECK(api::subtitle_delivery_for(image_sub(2, "fre")) == SubtitleDelivery::Encode);
    MediaStreamInfo no_ext = text_sub(3, "fre");
    no_ext.supports_external = false;
    CHECK(api::subtitle_delivery_for(no_ext) == SubtitleDelivery::Encode);
    CHECK(api::subtitle_delivery_for(audio(4, "fre")) == SubtitleDelivery::None);
}

void test_default_selection() {
    // Real case: 1917. The server chose audio no. 2 and PGS no. 4; we keep them
    // (the local fallback only keeps the PGS marked default: same result).
    const UserPreferences owner = prefs("", "", SubtitleMode::Default, true);
    MediaSourceInfo src = movie_1917();
    src.server_defaults = true;
    src.default_audio_index = 2;
    src.default_subtitle_index = 4;
    api::TrackSelection sel = api::default_selection(src, owner);
    CHECK_EQ(sel.media_source_id, "src1");
    CHECK_EQ(sel.audio_index, 2);
    CHECK_EQ(sel.subtitle_index, 4);
    CHECK(sel.subtitle_delivery == SubtitleDelivery::Encode);  // PGS : incrustation

    MediaSourceInfo fallback = movie_1917();
    api::TrackSelection local = api::default_selection(fallback, owner);
    CHECK_EQ(local.audio_index, 2);
    CHECK_EQ(local.subtitle_index, 4);
    CHECK(local.subtitle_delivery == SubtitleDelivery::Encode);

    // The server says "no subtitles" (index absent): we do not add any.
    MediaSourceInfo none = movie_1917();
    none.server_defaults = true;
    none.default_audio_index = 1;
    sel = api::default_selection(none, owner);
    CHECK_EQ(sel.audio_index, 1);
    CHECK_EQ(sel.subtitle_index, -1);
    CHECK(sel.subtitle_delivery == SubtitleDelivery::None);

    // Server index unknown to the source: fall back on the local computation.
    MediaSourceInfo bogus = movie_1917();
    bogus.server_defaults = true;
    bogus.default_audio_index = 42;
    bogus.default_subtitle_index = 42;
    sel = api::default_selection(bogus, owner);
    CHECK_EQ(sel.audio_index, 2);
    CHECK_EQ(sel.subtitle_index, -1);

    // Text track: delivery by the client.
    MediaSourceInfo texts = vostfr();
    texts.server_defaults = true;
    texts.default_audio_index = 1;
    texts.default_subtitle_index = 4;
    sel = api::default_selection(texts, owner);
    CHECK(sel.subtitle_delivery == SubtitleDelivery::Client);
}

void test_manual_changes() {
    const MediaSourceInfo src = vostfr();
    api::TrackSelection sel;
    sel.media_source_id = "v";
    sel.audio_index = 1;
    sel.subtitle_index = 4;
    sel.subtitle_delivery = SubtitleDelivery::Client;

    // Default mode: changing the audio keeps the subtitles.
    api::TrackSelection next = api::with_audio(src, prefs("", "fre", SubtitleMode::Default), sel, 2);
    CHECK_EQ(next.audio_index, 2);
    CHECK_EQ(next.subtitle_index, 4);
    // Smart mode: going back to the dubbed audio removes the full subtitles (the forced one remains).
    next = api::with_audio(src, prefs("", "fre", SubtitleMode::Smart), sel, 2);
    CHECK_EQ(next.audio_index, 2);
    CHECK_EQ(next.subtitle_index, 5);
    // Subtitles: "None", an image track (burned in), an unknown track.
    next = api::with_subtitle(src, sel, -1);
    CHECK_EQ(next.subtitle_index, -1);
    CHECK(next.subtitle_delivery == SubtitleDelivery::None);
    next = api::with_subtitle(src, sel, 6);
    CHECK_EQ(next.subtitle_index, 6);
    CHECK(next.subtitle_delivery == SubtitleDelivery::Encode);
    next = api::with_subtitle(src, sel, 99);
    CHECK_EQ(next.subtitle_index, -1);
    CHECK(sel != next);
    CHECK(sel == sel);
}

void test_reopen_rules() {
    api::TrackSelection a;
    a.media_source_id = "v";
    a.audio_index = 1;
    a.subtitle_index = 4;
    a.subtitle_delivery = SubtitleDelivery::Client;
    // Subtitles rendered by the client: never a reopening.
    api::TrackSelection b = a;
    b.subtitle_index = 6;
    CHECK(!api::stream_must_reopen(a, b));
    b.subtitle_index = -1;
    b.subtitle_delivery = SubtitleDelivery::None;
    CHECK(!api::stream_must_reopen(a, b));
    // Audio or source: yes.
    b = a;
    b.audio_index = 2;
    CHECK(api::stream_must_reopen(a, b));
    b = a;
    b.media_source_id = "autre";
    CHECK(api::stream_must_reopen(a, b));
    // Burn-in: yes when it appears, changes or disappears; no if unchanged.
    api::TrackSelection burned = a;
    burned.subtitle_index = 8;
    burned.subtitle_delivery = SubtitleDelivery::Encode;
    CHECK(api::stream_must_reopen(a, burned));
    CHECK(api::stream_must_reopen(burned, a));
    api::TrackSelection other = burned;
    other.subtitle_index = 3;
    CHECK(api::stream_must_reopen(burned, other));
    CHECK(!api::stream_must_reopen(burned, burned));
    api::TrackSelection none = a;
    none.subtitle_index = -1;
    none.subtitle_delivery = SubtitleDelivery::None;
    CHECK(api::stream_must_reopen(burned, none));
    CHECK(api::stream_must_reopen(none, burned));
}

}  // namespace

int main() {
    test_reopen_rules();
    test_audio_selection();
    test_subtitle_modes();
    test_subtitle_order_and_forced();
    test_delivery();
    test_default_selection();
    test_manual_changes();
    return testfw::test_failures();
}
