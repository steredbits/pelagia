// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "api/track_labels.h"

#include "util/languages.h"

namespace api {

namespace {

std::string lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return s;
}

std::string upper(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    }
    return s;
}

void add(std::string* out, const std::string& part) {
    if (part.empty()) return;
    if (!out->empty()) *out += " · ";
    *out += part;
}

std::string language_part(const MediaStreamInfo& s) {
    std::string name = util::language_name(s.language);
    if (s.is_original) name += std::string(" (") + util::tr(util::Str::TrackOriginal) + ")";
    return name;
}

// The track title, unless it says nothing more than the language.
std::string title_part(const MediaStreamInfo& s) {
    if (s.title.empty()) return std::string();
    if (lower(s.title) == lower(util::language_name(s.language))) return std::string();
    return s.title;
}

}  // namespace

std::string audio_codec_name(const std::string& codec) {
    const std::string c = lower(codec);
    if (c == "eac3") return "E-AC3";
    if (c == "ac3") return "AC3";
    if (c == "dts" || c == "dca") return "DTS";
    if (c == "truehd") return "TrueHD";
    if (c == "aac") return "AAC";
    if (c == "mp3") return "MP3";
    if (c == "mp2") return "MP2";
    if (c == "flac") return "FLAC";
    if (c == "opus") return "Opus";
    if (c == "vorbis") return "Vorbis";
    if (c.compare(0, 4, "pcm_") == 0) return "PCM";
    return upper(codec);
}

std::string subtitle_format_name(const std::string& codec) {
    const std::string c = lower(codec);
    if (c == "subrip" || c == "srt") return "SRT";
    if (c == "ass" || c == "ssa") return "ASS";
    if (c == "webvtt" || c == "vtt") return "WebVTT";
    if (c == "mov_text" || c == "text" || c == "microdvd") return util::tr(util::Str::TrackText);
    if (c == "pgssub" || c == "hdmv_pgs_subtitle" || c == "pgs") return "PGS";
    if (c == "dvd_subtitle" || c == "dvdsub" || c == "vobsub") return "VobSub";
    if (c == "dvb_subtitle" || c == "dvbsub") return "DVB";
    return upper(codec);
}

std::string channels_label(const MediaStreamInfo& s) {
    if (s.channel_layout == "5.1" || s.channel_layout == "7.1" ||
        s.channel_layout == "5.1(side)" || s.channel_layout == "7.1(wide)") {
        return s.channel_layout.substr(0, 3);
    }
    switch (s.channels) {
        case 0: return std::string();
        case 1: return "Mono";
        case 2: return util::tr(util::Str::TrackStereo);
        case 6: return "5.1";
        case 8: return "7.1";
        default: return util::trf(util::Str::TrackChannels, s.channels);
    }
}

std::string audio_label(const MediaStreamInfo& s) {
    std::string out = language_part(s);
    std::string format = audio_codec_name(s.codec);
    const std::string channels = channels_label(s);
    if (!format.empty() && !channels.empty()) format += " " + channels;
    else if (format.empty()) format = channels;
    add(&out, format);
    add(&out, title_part(s));
    return out;
}

std::string subtitle_label(const MediaStreamInfo& s) {
    std::string out = language_part(s);
    add(&out, subtitle_format_name(s.codec));
    if (s.is_forced) add(&out, util::tr(util::Str::TrackForced));
    if (s.is_hearing_impaired) add(&out, "SDH");
    if (s.is_external) add(&out, util::tr(util::Str::TrackExternal));
    add(&out, title_part(s));
    return out;
}

}  // namespace api
