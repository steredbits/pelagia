// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UI_TRACK_MENU_H
#define PELAGIA_CORE_UI_TRACK_MENU_H

// Choice of audio and subtitle tracks presented to the user (page and
// player): localized labels, "None", mention of subtitles burned in
// by the server, current track checked. Pure logic, no rendering.

#include <functional>
#include <string>
#include <vector>

#include "api/jellyfin_models.h"
#include "api/track_selection.h"
#include "ui/app.h"

namespace ui {

struct TrackChoice {
  std::string label;
  int index = -1;        // Jellyfin index of the track; -1 = "None"
  bool selected = false;
};

// There is something to choose (at least one audio or subtitle track).
bool has_track_choices(const api::MediaSourceInfo& source);

// Audio tracks of the source, the current track checked.
std::vector<TrackChoice> audio_choices(const api::MediaSourceInfo& source,
                                       const api::TrackSelection& tracks);
// "None" then the subtitles (those the server must burn in say so).
std::vector<TrackChoice> subtitle_choices(const api::MediaSourceInfo& source,
                                          const api::TrackSelection& tracks);

// Value displayed on the page / in the player.
std::string audio_summary(const api::MediaSourceInfo& source, const api::TrackSelection& tracks);
std::string subtitle_summary(const api::MediaSourceInfo& source,
                             const api::TrackSelection& tracks);

// Menu items for App::open_menu; also returns the index of the current track.
std::vector<MenuItem> track_menu_items(const std::vector<TrackChoice>& choices,
                                       std::function<void(App&, int)> on_pick, int* initial);

}  // namespace ui

#endif  // PELAGIA_CORE_UI_TRACK_MENU_H
