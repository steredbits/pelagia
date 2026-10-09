// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "api/track_selection.h"

#include <algorithm>

#include "util/languages.h"

namespace api {

namespace {

// Empty language preference = "any" (like the server).
bool matches_pref(const std::string& language, const std::string& pref) {
    return pref.empty() || util::same_language(language, pref);
}

// Equivalent of the server's GetStreamScore (preferred language first, then
// forced, default, available external, text, external).
long long stream_score(const MediaStreamInfo& s, bool language_match) {
    long long score = language_match ? 100 : 1;
    score = score * 10 + (s.is_forced ? 2 : 1);
    score = score * 10 + (s.is_default ? 2 : 1);
    score = score * 10 + (s.supports_external ? 2 : 1);
    score = score * 10 + (s.is_text ? 2 : 1);
    score = score * 10 + (s.is_external ? 2 : 1);
    return score;
}

// Forced subtitles of a preferred or undefined language, the preferred
// language first (BehaviorOnlyForced).
const MediaStreamInfo* first_forced(const std::vector<const MediaStreamInfo*>& sorted,
                                    const std::string& pref) {
    const MediaStreamInfo* undefined = nullptr;
    for (const MediaStreamInfo* s : sorted) {
        if (!s->is_forced) continue;
        if (matches_pref(s->language, pref)) return s;
        if (!undefined && util::language_undefined(s->language)) undefined = s;
    }
    return undefined;
}

int index_of(const MediaStreamInfo* s) {
    return s ? s->index : -1;
}

}  // namespace

const MediaStreamInfo* find_stream(const MediaSourceInfo& source, int index) {
    for (const MediaStreamInfo& s : source.streams) {
        if (s.index == index) return &s;
    }
    return nullptr;
}

std::vector<const MediaStreamInfo*> streams_of(const MediaSourceInfo& source, StreamKind kind) {
    std::vector<const MediaStreamInfo*> out;
    for (const MediaStreamInfo& s : source.streams) {
        if (s.kind == kind) out.push_back(&s);
    }
    return out;
}

const MediaSourceInfo* find_source(const MediaItem& item, const std::string& id) {
    for (const MediaSourceInfo& s : item.media_sources) {
        if (id.empty() || s.id == id) return &s;
    }
    return nullptr;
}

SubtitleDelivery subtitle_delivery_for(const MediaStreamInfo& stream) {
    if (stream.kind != StreamKind::Subtitle) return SubtitleDelivery::None;
    return stream.is_text && stream.supports_external ? SubtitleDelivery::Client
                                                      : SubtitleDelivery::Encode;
}

int select_default_audio(const MediaSourceInfo& source, const UserPreferences& prefs) {
    std::vector<const MediaStreamInfo*> audio = streams_of(source, StreamKind::Audio);
    if (audio.empty()) return -1;
    // "Original language": the item's original language is not known to the
    // client, only the track marked IsOriginal (recent versions) designates it.
    const bool original = prefs.audio_language == "OriginalLanguage";
    if (original) {
        for (const MediaStreamInfo* s : audio) {
            if (s->is_original) return s->index;
        }
    }
    const std::string pref = original ? std::string() : prefs.audio_language;
    std::stable_sort(audio.begin(), audio.end(),
                     [&pref](const MediaStreamInfo* a, const MediaStreamInfo* b) {
                         const bool ma = !pref.empty() && util::same_language(a->language, pref);
                         const bool mb = !pref.empty() && util::same_language(b->language, pref);
                         return stream_score(*a, ma) > stream_score(*b, mb);
                     });
    if (prefs.play_default_audio) {
        for (const MediaStreamInfo* s : audio) {
            if (s->is_default) return s->index;
        }
    }
    return audio.front()->index;
}

int select_default_subtitle(const MediaSourceInfo& source, const UserPreferences& prefs,
                            const std::string& audio_language) {
    if (prefs.subtitle_mode == SubtitleMode::None) return -1;
    const std::string& pref = prefs.subtitle_language;
    std::vector<const MediaStreamInfo*> sorted = streams_of(source, StreamKind::Subtitle);
    // Server order: external > default > full track of the language > forced of the
    // language > forced undefined > forced (each criterion breaks ties of the previous one).
    auto key = [&pref](const MediaStreamInfo* s, int rank) {
        switch (rank) {
            case 0: return s->is_external;
            case 1: return s->is_default;
            case 2: return !s->is_forced && matches_pref(s->language, pref);
            case 3: return s->is_forced && matches_pref(s->language, pref);
            case 4: return s->is_forced && util::language_undefined(s->language);
            default: return s->is_forced;
        }
    };
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&key](const MediaStreamInfo* a, const MediaStreamInfo* b) {
                         for (int rank = 0; rank < 6; ++rank) {
                             const bool ka = key(a, rank);
                             const bool kb = key(b, rank);
                             if (ka != kb) return ka;
                         }
                         return false;
                     });
    switch (prefs.subtitle_mode) {
        case SubtitleMode::Default:
            for (const MediaStreamInfo* s : sorted) {
                if (s->is_external || s->is_default || s->is_forced) return s->index;
            }
            return -1;
        case SubtitleMode::Smart: {
            // Audio in a preferred subtitle language: same as "forced only".
            const bool audio_is_preferred =
                !pref.empty() && !audio_language.empty() && util::same_language(audio_language, pref);
            if (!audio_is_preferred) {
                for (const MediaStreamInfo* s : sorted) {
                    if (matches_pref(s->language, pref)) return s->index;
                }
                return -1;
            }
            return index_of(first_forced(sorted, pref));
        }
        case SubtitleMode::Always:
            for (const MediaStreamInfo* s : sorted) {
                if (!s->is_forced && matches_pref(s->language, pref)) return s->index;
            }
            return index_of(first_forced(sorted, pref));
        case SubtitleMode::OnlyForced:
            return index_of(first_forced(sorted, pref));
        case SubtitleMode::None:
            break;
    }
    return -1;
}

namespace {

std::string language_of(const MediaSourceInfo& source, int index) {
    const MediaStreamInfo* s = find_stream(source, index);
    return s ? s->language : std::string();
}

TrackSelection build(const MediaSourceInfo& source, int audio, int subtitle) {
    TrackSelection sel;
    sel.media_source_id = source.id;
    sel.audio_index = audio;
    sel.subtitle_index = subtitle;
    const MediaStreamInfo* s = subtitle >= 0 ? find_stream(source, subtitle) : nullptr;
    sel.subtitle_delivery = s ? subtitle_delivery_for(*s) : SubtitleDelivery::None;
    if (!s) sel.subtitle_index = -1;
    return sel;
}

}  // namespace

TrackSelection default_selection(const MediaSourceInfo& source, const UserPreferences& prefs) {
    int audio = -1;
    int subtitle = -1;
    if (source.server_defaults) {
        // Index missing from the source: fall back rather than send an unknown track.
        const MediaStreamInfo* a = find_stream(source, source.default_audio_index);
        audio = a && a->kind == StreamKind::Audio ? a->index : select_default_audio(source, prefs);
        const MediaStreamInfo* s = find_stream(source, source.default_subtitle_index);
        subtitle = s && s->kind == StreamKind::Subtitle ? s->index : -1;
    } else {
        audio = select_default_audio(source, prefs);
        subtitle = select_default_subtitle(source, prefs, language_of(source, audio));
    }
    return build(source, audio, subtitle);
}

TrackSelection with_audio(const MediaSourceInfo& source, const UserPreferences& prefs,
                          const TrackSelection& current, int audio_index) {
    int subtitle = current.subtitle_index;
    if (prefs.subtitle_mode == SubtitleMode::Smart) {
        subtitle = select_default_subtitle(source, prefs, language_of(source, audio_index));
    }
    return build(source, audio_index, subtitle);
}

bool stream_must_reopen(const TrackSelection& before, const TrackSelection& after) {
    if (before.media_source_id != after.media_source_id || before.audio_index != after.audio_index) {
        return true;
    }
    const bool burned = before.subtitle_delivery == SubtitleDelivery::Encode ||
                        after.subtitle_delivery == SubtitleDelivery::Encode;
    return burned && (before.subtitle_delivery != after.subtitle_delivery ||
                      before.subtitle_index != after.subtitle_index);
}

TrackSelection with_subtitle(const MediaSourceInfo& source, const TrackSelection& current,
                             int subtitle_index) {
    return build(source, current.audio_index, subtitle_index);
}

}  // namespace api
