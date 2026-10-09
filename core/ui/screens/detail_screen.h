// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UI_SCREENS_DETAIL_SCREEN_H
#define PELAGIA_CORE_UI_SCREENS_DETAIL_SCREEN_H

// Page of a movie or an episode: poster, title, year, duration, overview,
// buttons "Resume from ..." / "Play from the beginning" (or "Play").
// The item is re-read at opening (up-to-date resume position), then when the player
// returns, once the stop report is sent.
//
// Tracks: below the buttons, "Audio" and "Subtitles" show the
// tracks preselected from the Jellyfin profile (the server's); down
// then cross open the list, "None" possible for subtitles. Playback
// starts with these tracks.

#include <string>
#include <vector>

#include "api/track_selection.h"
#include "ui/focus.h"
#include "ui/screen.h"
#include "ui/screens/screens.h"

namespace ui {

class DetailScreen final : public Screen {
 public:
  explicit DetailScreen(const api::MediaItem& item) : item_(item) {}

  const char* name() const override { return "detail"; }
  void enter(App& app) override { load(app); }
  void update(App& app) override;
  void handle(App& app, const platform::Input& in) override;
  void draw(App& app, Painter& p) override;
  bool busy() const override { return loading_; }

  const api::MediaItem& item() const { return item_; }
  // Labels of the displayed buttons (tests).
  std::vector<std::string> buttons() const;
  int focus() const { return focus_; }
  bool refreshed() const { return refreshed_; }
  // Selected line: 0 buttons, 1 audio, 2 subtitles (tests).
  enum Row { kButtons = 0, kAudioRow = 1, kSubtitleRow = 2 };
  int row() const { return row_; }
  bool has_track_rows() const;
  const api::TrackSelection& tracks() const { return tracks_; }
  std::string audio_text() const;
  std::string subtitle_text() const;

 private:
  void load(App& app);
  int64_t start_for(int button) const;
  const api::MediaSourceInfo* source() const;
  void choose_default_tracks(App& app);
  void open_audio_menu(App& app);
  void open_subtitle_menu(App& app);
  void draw_track_rows(Painter& p, int y);

  api::MediaItem item_;
  int focus_ = 0;
  int row_ = kButtons;
  api::TrackSelection tracks_;
  bool loading_ = false;
  bool refreshed_ = false;  // item re-read from the server at least once
  std::string error_;
  uint64_t loaded_version_ = 0;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_SCREENS_DETAIL_SCREEN_H
