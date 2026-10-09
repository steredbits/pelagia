// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UI_FORMAT_H
#define PELAGIA_CORE_UI_FORMAT_H

// Formatting for display (pure functions, tested): durations,
// episode numbers, cleaned titles, server address typed with the gamepad.

#include <cstdint>
#include <string>

#include "api/jellyfin_models.h"
#include "ui/poster_cache.h"

namespace ui {

constexpr int64_t kTicksPerMs = 10000;  // 1 tick Jellyfin = 100 ns

// Playback position: "2:03", "1:02:03" (negative clamped to 0).
std::string format_clock(int64_t ms);
// Movie duration: "1 h 42", "2 h", "42 min", "< 1 min".
std::string format_runtime(int64_t ms);
// "S01E03" (empty if the numbers are unknown).
std::string episode_code(const api::MediaItem& item);
// Cleaned title (invisible characters removed, see util/text).
std::string display_title(const api::MediaItem& item);
// Sort key of an item: cleaned SortName if present, otherwise the name.
std::string item_sort_key(const api::MediaItem& item);

// Resume position (ms), 0 if none or if the item is marked "played".
int64_t resume_position_ms(const api::MediaItem& item);
// Progress 0..1 of the started playback (0 if none).
double progress_fraction(const api::MediaItem& item);
// Movie, episode or video with a duration: directly playable.
bool is_playable(const api::MediaItem& item);

// Poster of an item: its own, otherwise the series' (episodes, seasons).
PosterKey poster_key_for(const api::MediaItem& item, int width);

// Typed address -> URL: spaces removed, "http://" added without a scheme,
// trailing "/" removed. "192.168.1.10:8096" -> "http://192.168.1.10:8096".
std::string complete_server_address(const std::string& input);

}  // namespace ui

#endif  // PELAGIA_CORE_UI_FORMAT_H
