// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UI_SCREENS_GRID_SCREEN_H
#define PELAGIA_CORE_UI_SCREENS_GRID_SCREEN_H

// Poster grid of a library (7 columns), sorted by cleaned title.
// The full title, year and duration of the selected element are
// repeated under the header (poster captions are truncated).

#include "ui/focus.h"
#include "ui/screen.h"
#include "ui/screens/screens.h"

namespace ui {

class GridScreen final : public Screen {
 public:
  explicit GridScreen(const api::Library& library) : library_(library) {}

  const char* name() const override { return "grid"; }
  void enter(App& app) override { load(app); }
  void update(App& app) override;
  void handle(App& app, const platform::Input& in) override;
  void draw(App& app, Painter& p) override;
  bool busy() const override { return loading_; }

  const std::vector<api::MediaItem>& items() const { return items_; }
  int focus() const { return focus_.index(); }
  const api::Library& library() const { return library_; }

 private:
  void load(App& app);

  api::Library library_;
  std::vector<api::MediaItem> items_;
  GridFocus focus_{7};
  bool loading_ = false;
  bool loaded_ = false;
  std::string error_;
  uint64_t loaded_version_ = 0;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_SCREENS_GRID_SCREEN_H
