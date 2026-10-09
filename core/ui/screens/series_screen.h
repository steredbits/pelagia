// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UI_SCREENS_SERIES_SCREEN_H
#define PELAGIA_CORE_UI_SCREENS_SERIES_SCREEN_H

// Series: seasons on the left, episodes of the selected season on the right.
// Up/down within the column, left/right changes column; changing
// season loads its episodes (a stale result is ignored). Cross on an
// episode: its page.

#include <string>
#include <vector>

#include "ui/focus.h"
#include "ui/screen.h"
#include "ui/screens/screens.h"

namespace ui {

class SeriesScreen final : public Screen {
 public:
  explicit SeriesScreen(const api::MediaItem& series) : series_(series) {}

  const char* name() const override { return "series"; }
  void enter(App& app) override;
  void update(App& app) override;
  void handle(App& app, const platform::Input& in) override;
  void draw(App& app, Painter& p) override;
  bool busy() const override { return loading_seasons_ || loading_episodes_; }

  const std::vector<api::MediaItem>& seasons() const { return seasons_; }
  const std::vector<api::MediaItem>& episodes() const { return episodes_; }
  int column() const { return column_; }
  int season_focus() const { return season_focus_.index(); }
  int episode_focus() const { return episode_focus_.index(); }
  // Season whose episodes are displayed.
  int shown_season() const { return shown_season_; }

 private:
  void load_seasons(App& app);
  void load_episodes(App& app, int season);

  api::MediaItem series_;
  std::vector<api::MediaItem> seasons_;
  std::vector<api::MediaItem> episodes_;
  ListFocus season_focus_;
  ListFocus episode_focus_;
  int column_ = 0;  // 0: seasons, 1: episodes
  int shown_season_ = -1;
  int requested_season_ = -1;
  bool loading_seasons_ = false;
  bool loading_episodes_ = false;
  std::string error_;
  uint64_t loaded_version_ = 0;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_SCREENS_SERIES_SCREEN_H
