// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "ui/screens/splash_screen.h"

#include "ui/app.h"
#include "ui/screens/screens.h"
#include "ui/theme.h"
#include "util/app_info.h"
#include "util/i18n.h"

namespace ui {

namespace {

struct RestoreResult {
  api::ApiResult result;
};

}  // namespace

void SplashScreen::restore(App& app) {
  checking_ = true;
  message_.clear();
  const SavedSession session = session_;
  app.run<RestoreResult>(
      lifetime(), [session](Backend& b, RestoreResult& r) { r.result = b.restore(session); },
      [this, &app](RestoreResult& r) {
        checking_ = false;
        if (r.result.ok()) {
          app.on_logged_in(session_);
        } else if (r.result.http_status == 401) {
          // Revoked or expired token: it will no longer be of use.
          app.sessions().clear();
          app.replace_all(make_login_screen(session_.server,
                                            util::tr(util::Str::SessionExpired)));
        } else {
          message_ = describe_error(r.result);
          focus_.set_count(2);
          focus_.set_index(0);
        }
      });
}

void SplashScreen::handle(App& app, const platform::Input& in) {
  if (checking_) {
    return;
  }
  switch (in.event) {
    case platform::InputEvent::Left:
    case platform::InputEvent::Up:
      focus_.move(Dir::Up);
      break;
    case platform::InputEvent::Right:
    case platform::InputEvent::Down:
      focus_.move(Dir::Down);
      break;
    case platform::InputEvent::Ok:
      if (focus_.index() == 0) {
        restore(app);
      } else {
        app.replace_all(make_login_screen(session_.server, ""));
      }
      break;
    case platform::InputEvent::Back:
    case platform::InputEvent::Menu:
      app.open_menu("Pelagia", {app.language_menu_item(),
                           MenuItem{util::tr(util::Str::QuitApp), [](App& a) { a.request_quit(); }}});
      break;
    default:
      break;
  }
}

void SplashScreen::draw(App& app, Painter& p) {
  (void)app;
  p.label_in("Pelagia", rect(0, 320, theme::kScreenW, 90), Weight::Bold, 72, theme::accent(),
             Align::Center);
  p.label_in(util::tr(util::Str::AboutTagline), rect(0, 410, theme::kScreenW, 50), Weight::Regular, theme::kBody,
             theme::text_dim(), Align::Center);
  if (checking_) {
    p.status(rect(0, 520, theme::kScreenW, 120), util::trf(util::Str::ConnectingTo, session_.server.c_str()), true,
             false);
    return;
  }
  p.status(rect(0, 480, theme::kScreenW, 80), message_, false, true);
  const int w = 420;
  const int x = (theme::kScreenW - 2 * w - theme::kGap) / 2;
  p.button(rect(x, 600, w, 80), util::tr(util::Str::Retry), focus_.index() == 0);
  p.button(rect(x + w + theme::kGap, 600, w, 80), util::tr(util::Str::ChangeServer), focus_.index() == 1);
  p.hints(util::tr(util::Str::SplashHint));
}

}  // namespace ui
