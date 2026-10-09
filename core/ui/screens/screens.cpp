// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "ui/screens/screens.h"

#include "ui/screens/about_screen.h"
#include "ui/screens/detail_screen.h"
#include "ui/screens/grid_screen.h"
#include "ui/screens/home_screen.h"
#include "ui/screens/login_screen.h"
#include "ui/screens/player_screen.h"
#include "ui/screens/series_screen.h"
#include "ui/screens/splash_screen.h"
#include "util/i18n.h"

namespace ui {

std::unique_ptr<Screen> make_login_screen(const std::string& server, const std::string& message) {
  return std::unique_ptr<Screen>(new LoginScreen(server, message));
}

std::unique_ptr<Screen> make_splash_screen(const SavedSession& session) {
  return std::unique_ptr<Screen>(new SplashScreen(session));
}

std::unique_ptr<Screen> make_home_screen() { return std::unique_ptr<Screen>(new HomeScreen()); }

std::unique_ptr<Screen> make_about_screen() {
  return std::unique_ptr<Screen>(new AboutScreen());
}

std::unique_ptr<Screen> make_grid_screen(const api::Library& library) {
  return std::unique_ptr<Screen>(new GridScreen(library));
}

std::unique_ptr<Screen> make_detail_screen(const api::MediaItem& item) {
  return std::unique_ptr<Screen>(new DetailScreen(item));
}

std::unique_ptr<Screen> make_series_screen(const api::MediaItem& series) {
  return std::unique_ptr<Screen>(new SeriesScreen(series));
}

std::unique_ptr<Screen> make_player_screen(const api::MediaItem& item, int64_t start_ms,
                                           const api::TrackSelection& tracks) {
  return std::unique_ptr<Screen>(new PlayerScreen(item, start_ms, tracks));
}

std::unique_ptr<Screen> make_item_screen(const api::MediaItem& item) {
  if (item.type == "Series") {
    return make_series_screen(item);
  }
  return make_detail_screen(item);
}

std::string describe_error(const api::ApiResult& result) {
  switch (result.error) {
    case api::ApiError::None:
      return "";
    case api::ApiError::Transport:
      return util::trf(util::Str::ErrServerUnreachable, result.message.c_str());
    case api::ApiError::Http:
      if (result.http_status == 401) {
        return util::tr(util::Str::ErrBadCredentials);
      }
      return util::trf(util::Str::ErrServerError, result.http_status);
    case api::ApiError::Parse:
      return util::tr(util::Str::ErrUnexpected);
    case api::ApiError::NotAuthenticated:
      return util::tr(util::Str::ErrSessionClosed);
  }
  return util::tr(util::Str::ErrUnknown);
}

}  // namespace ui
