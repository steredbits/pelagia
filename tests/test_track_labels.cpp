// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Track labels.

#include "api/track_labels.h"

#include "test_framework.h"
#include "util/i18n.h"

namespace {

api::MediaStreamInfo stream(api::StreamKind kind, const char* codec, const char* lang) {
    api::MediaStreamInfo s;
    s.kind = kind;
    s.codec = codec;
    s.language = lang;
    return s;
}

}  // namespace

namespace {

// Same labels in English (default language).
void check_english() {
    api::MediaStreamInfo a = stream(api::StreamKind::Audio, "eac3", "fre");
    a.channels = 6;
    a.channel_layout = "5.1";
    CHECK_EQ(api::audio_label(a), "French · E-AC3 5.1");
    a.language = "eng";
    a.is_original = true;
    CHECK_EQ(api::audio_label(a), "English (original) · E-AC3 5.1");
    api::MediaStreamInfo stereo = stream(api::StreamKind::Audio, "aac", "jpn");
    stereo.channels = 2;
    CHECK_EQ(api::audio_label(stereo), "Japanese · AAC Stereo");
    stereo.channels = 3;
    CHECK_EQ(api::audio_label(stereo), "Japanese · AAC 3 channels");
    api::MediaStreamInfo sub = stream(api::StreamKind::Subtitle, "subrip", "fre");
    sub.is_forced = true;
    sub.is_external = true;
    CHECK_EQ(api::subtitle_label(sub), "French · SRT · forced · external");
    CHECK_EQ(api::subtitle_format_name("mov_text"), "Text");
}

}  // namespace

int main() {
    check_english();
    // The rest of the test checks the French labels.
    util::ScopedLanguage french(util::Lang::Fr);
    // Audio: language, format and channels.
    api::MediaStreamInfo a = stream(api::StreamKind::Audio, "eac3", "fre");
    a.channels = 6;
    a.channel_layout = "5.1";
    CHECK_EQ(api::audio_label(a), "Français · E-AC3 5.1");
    a.codec = "truehd";
    a.language = "eng";
    a.channels = 8;
    a.channel_layout = "7.1";
    CHECK_EQ(api::audio_label(a), "Anglais · TrueHD 7.1");
    a.is_original = true;
    CHECK_EQ(api::audio_label(a), "Anglais (VO) · TrueHD 7.1");
    // Stereo without a channel layout, then without any channel.
    api::MediaStreamInfo stereo = stream(api::StreamKind::Audio, "aac", "jpn");
    stereo.channels = 2;
    CHECK_EQ(api::audio_label(stereo), "Japonais · AAC Stéréo");
    stereo.channels = 0;
    CHECK_EQ(api::audio_label(stereo), "Japonais · AAC");
    // Track title: added if it brings something, otherwise ignored.
    api::MediaStreamInfo commentary = stream(api::StreamKind::Audio, "ac3", "eng");
    commentary.channels = 2;
    commentary.title = "Commentaire du réalisateur";
    CHECK_EQ(api::audio_label(commentary), "Anglais · AC3 Stéréo · Commentaire du réalisateur");
    commentary.title = "anglais";
    CHECK_EQ(api::audio_label(commentary), "Anglais · AC3 Stéréo");
    // Langue absente.
    CHECK_EQ(api::audio_label(stream(api::StreamKind::Audio, "dts", "")), "Langue inconnue · DTS");

    // Sous-titres.
    api::MediaStreamInfo s = stream(api::StreamKind::Subtitle, "subrip", "fre");
    CHECK_EQ(api::subtitle_label(s), "Français · SRT");
    s.is_forced = true;
    CHECK_EQ(api::subtitle_label(s), "Français · SRT · forcés");
    s.is_forced = false;
    s.is_hearing_impaired = true;
    s.is_external = true;
    CHECK_EQ(api::subtitle_label(s), "Français · SRT · SDH · externe");
    CHECK_EQ(api::subtitle_label(stream(api::StreamKind::Subtitle, "PGSSUB", "fre")),
             "Français · PGS");
    CHECK_EQ(api::subtitle_label(stream(api::StreamKind::Subtitle, "ass", "eng")), "Anglais · ASS");
    CHECK_EQ(api::subtitle_label(stream(api::StreamKind::Subtitle, "dvd_subtitle", "ger")),
             "Allemand · VobSub");

    CHECK_EQ(api::audio_codec_name("dca"), "DTS");
    CHECK_EQ(api::audio_codec_name("pcm_s16le"), "PCM");
    CHECK_EQ(api::audio_codec_name("xyz"), "XYZ");
    return testfw::test_failures();
}
