// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UI_SCREENS_HOME_SCREEN_H
#define PELAGIA_CORE_UI_SCREENS_HOME_SCREEN_H

// Home: horizontal rows "Continue watching" (if not empty),
// "Libraries", then "Latest additions" of each library.
// Up/down changes row, left/right browses the row (position
// remembered per row). Options or circle: menu (refresh, sign out,
// quit). The rows reload when returning from a playback.

#include <string>
#include <vector>

#include "ui/focus.h"
#include "ui/screen.h"
#include "ui/screens/screens.h"

namespace ui {

class HomeScreen final : public Screen {
 public:
  enum class RowKind { Resume, Libraries, Latest };
  struct Row {
    RowKind kind;
    std::string title;
    int library = -1;  // Latest: library index
    std::vector<api::MediaItem> items;
  };

  const char* name() const override { return "home"; }
  void enter(App& app) override { load(app, true); }
  void resume(App& app) override;
  void handle(App& app, const platform::Input& in) override;
  void update(App& app) override;
  void draw(App& app, Painter& p) override;
  bool busy() const override { return pending_ > 0; }

  const std::vector<Row>& rows() const { return rows_; }
  int focused_row() const { return focus_.row(); }
  int focused_index() const { return focus_.index(); }
  bool loaded() const { return libraries_done_ && resume_done_; }
  const std::string& error() const { return error_; }

 private:
  void load(App& app, bool show_spinner);
  void rebuild_rows();
  void open_focused(App& app);
  void open_options(App& app);
  int row_height(const Row& row) const;

  std::vector<api::Library> libraries_;
  std::vector<api::MediaItem> resume_items_;
  std::vector<std::vector<api::MediaItem>> latest_;
  std::vector<bool> latest_done_;
  bool libraries_done_ = false;
  bool resume_done_ = false;
  int pending_ = 0;
  std::string error_;
  std::vector<Row> rows_;
  RowsFocus focus_;
  int scroll_y_ = 0;
  uint64_t loaded_version_ = 0;
  uint64_t language_version_ = 0;
  bool focus_settled_ = false;  // focus placed after the first complete load
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_SCREENS_HOME_SCREEN_H
