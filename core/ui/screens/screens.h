// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UI_SCREENS_SCREENS_H
#define PELAGIA_CORE_UI_SCREENS_SCREENS_H

// Screen factories and rules shared between them.

#include <memory>
#include <string>

#include "api/jellyfin_client.h"
#include "api/jellyfin_models.h"
#include "ui/screen.h"
#include "ui/session_store.h"

namespace ui {

std::unique_ptr<Screen> make_login_screen(const std::string& server, const std::string& message);
std::unique_ptr<Screen> make_splash_screen(const SavedSession& session);
std::unique_ptr<Screen> make_home_screen();
std::unique_ptr<Screen> make_about_screen();
std::unique_ptr<Screen> make_grid_screen(const api::Library& library);
std::unique_ptr<Screen> make_detail_screen(const api::MediaItem& item);
std::unique_ptr<Screen> make_series_screen(const api::MediaItem& series);
std::unique_ptr<Screen> make_player_screen(const api::MediaItem& item, int64_t start_ms,
                                           const api::TrackSelection& tracks);
// Screen of an item chosen from a list: series -> seasons, otherwise page.
std::unique_ptr<Screen> make_item_screen(const api::MediaItem& item);

// User-readable error message (without needless technical detail).
std::string describe_error(const api::ApiResult& result);

// Results of the screens' asynchronous tasks.
struct ItemsResult {
  api::ApiResult result;
  api::ItemList list;
};
struct ItemResult {
  api::ApiResult result;
  api::MediaItem item;
};
struct LibrariesResult {
  api::ApiResult result;
  std::vector<api::Library> libraries;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_SCREENS_SCREENS_H
