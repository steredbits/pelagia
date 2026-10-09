// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_API_TRACK_LABELS_H
#define PELAGIA_CORE_API_TRACK_LABELS_H

// English track labels for menus and logs: language
// ("French", "English (original)" when the server marks the track as
// original), format, channels, track title, "forced". Pure functions.

#include <string>

#include "api/jellyfin_models.h"

namespace api {

// "eac3" -> "E-AC3", "truehd" -> "TrueHD"; otherwise the code in uppercase.
std::string audio_codec_name(const std::string& codec);
// "subrip" -> "SRT", "PGSSUB" -> "PGS", "dvd_subtitle" -> "VobSub"...
std::string subtitle_format_name(const std::string& codec);
// "5.1", "7.1", "Stereo", "Mono" (empty if unknown).
std::string channels_label(const MediaStreamInfo& stream);

// "French · E-AC3 5.1" (+ "· title" if the track has one).
std::string audio_label(const MediaStreamInfo& stream);
// "French · SRT · forced"; image subtitles (PGS...): the format says so.
std::string subtitle_label(const MediaStreamInfo& stream);

}  // namespace api

#endif  // PELAGIA_CORE_API_TRACK_LABELS_H
