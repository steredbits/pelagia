// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_API_TRACK_SELECTION_H
#define PELAGIA_CORE_API_TRACK_SELECTION_H

// Choice of the audio and subtitle tracks of a playback. Pure functions, no
// network, tested without rendering.
//
// The server already computes, for the user, DefaultAudioStreamIndex and
// DefaultSubtitleStreamIndex of each source (profile preferences, remembered
// choices, original language): this is what the official clients use,
// and it is taken as is. select_default_audio/subtitle are the fallback
// (response without these fields) and the computation of subtitles when the user
// changes the audio; they follow the server's MediaStreamSelector (Jellyfin
// 10.11 and master, checked in the source).
//
// What will change with PlaybackInfo + DeviceProfile: the server
// will provide the delivery method of each subtitle and the stream URL;
// subtitle_delivery_for() and the URL construction (jellyfin_urls) are
// the only parts concerned, the model and the selection stay.

#include <string>
#include <vector>

#include "api/jellyfin_models.h"

namespace api {

// How a subtitle reaches the screen.
enum class SubtitleDelivery {
    None,    // no subtitles
    Client,  // text downloaded separately, rendered by the client (stream unchanged)
    Encode,  // burned into the video by the server (forces video transcoding)
};

// Tracks chosen for a playback.
struct TrackSelection {
    std::string media_source_id;  // empty: the server's default source
    int audio_index = -1;         // -1: the server chooses (no parameter sent)
    int subtitle_index = -1;      // -1: no subtitles
    SubtitleDelivery subtitle_delivery = SubtitleDelivery::None;

    bool operator==(const TrackSelection& o) const {
        return media_source_id == o.media_source_id && audio_index == o.audio_index &&
               subtitle_index == o.subtitle_index && subtitle_delivery == o.subtitle_delivery;
    }
    bool operator!=(const TrackSelection& o) const { return !(*this == o); }
};

const MediaStreamInfo* find_stream(const MediaSourceInfo& source, int index);
std::vector<const MediaStreamInfo*> streams_of(const MediaSourceInfo& source, StreamKind kind);
// Source with the given id (empty: the first one), nullptr if absent.
const MediaSourceInfo* find_source(const MediaItem& item, const std::string& id);

// Delivery chosen for a subtitle track: text the server can provide
// separately -> client; otherwise (PGS/VobSub image, non-extractable text) -> burn-in.
SubtitleDelivery subtitle_delivery_for(const MediaStreamInfo& stream);

// Pure fallback (port of MediaStreamSelector). Return the track Index, -1 if none.
int select_default_audio(const MediaSourceInfo& source, const UserPreferences& prefs);
// audio_language: language of the chosen audio track (empty if unknown).
int select_default_subtitle(const MediaSourceInfo& source, const UserPreferences& prefs,
                            const std::string& audio_language);

// Default tracks of a source: server values if present and valid,
// otherwise the fallback above.
TrackSelection default_selection(const MediaSourceInfo& source, const UserPreferences& prefs);

// Selection for another audio choice: subtitles are recomputed when
// the profile is in "Smart" mode (which depends on the audio language), otherwise
// kept.
TrackSelection with_audio(const MediaSourceInfo& source, const UserPreferences& prefs,
                          const TrackSelection& current, int audio_index);
// Does the track change force the stream to be reopened? Yes for the source,
// the audio, and burned-in subtitles (before or after); no for
// subtitles rendered by the client: instant change, stream unchanged.
bool stream_must_reopen(const TrackSelection& before, const TrackSelection& after);

// Selection for another subtitle choice (-1: none).
TrackSelection with_subtitle(const MediaSourceInfo& source, const TrackSelection& current,
                             int subtitle_index);

}  // namespace api

#endif  // PELAGIA_CORE_API_TRACK_SELECTION_H
