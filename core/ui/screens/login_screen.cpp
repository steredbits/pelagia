// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "ui/screens/login_screen.h"

#include "ui/app.h"
#include "ui/format.h"
#include "ui/screens/screens.h"
#include "ui/theme.h"
#include "util/app_info.h"
#include "util/text.h"
#include "util/i18n.h"

namespace ui {

namespace {

using platform::InputEvent;

const util::Str kLabels[] = {util::Str::ServerAddress, util::Str::Username, util::Str::Password};

struct LoginResult {
  api::ApiResult result;
  SavedSession session;
};

std::string masked(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < util::utf8_length(s); ++i) out += "\xE2\x80\xA2";  // •
  return out;
}

}  // namespace

LoginScreen::LoginScreen(const std::string& server, const std::string& message)
    : message_(message) {
  fields_[kServer] = server;
  focus_.set_count(4);
  // Known address: we start with the user name.
  focus_.set_index(server.empty() ? kServer : kUser);
}

bool LoginScreen::wants_text_input() const {
  return keyboard_.is_open() || focus_.index() != kSubmit;
}

void LoginScreen::edit_field(const platform::Input& in) {
  std::string& f = fields_[focus_.index()];
  if (in.event == InputEvent::Erase) {
    util::utf8_pop_back(&f);
  } else if (f.size() < 256) {
    util::utf8_append(&f, in.codepoint);
  }
}

void LoginScreen::handle(App& app, const platform::Input& in) {
  if (connecting_) {
    return;  // attempt in progress: nothing to modify
  }
  if (keyboard_.is_open()) {
    if (keyboard_.handle(in) == VirtualKeyboard::Result::Done) {
      focus_.move(Dir::Down);  // next field, then the button
    }
    return;
  }
  const bool on_field = focus_.index() != kSubmit;
  switch (in.event) {
    case InputEvent::Up: focus_.move(Dir::Up); break;
    case InputEvent::Down: focus_.move(Dir::Down); break;
    case InputEvent::Text:
    case InputEvent::Erase:
      if (on_field) edit_field(in);
      break;
    case InputEvent::Ok:
      if (on_field) {
        const int i = focus_.index();
        keyboard_.open(&fields_[i], util::tr(kLabels[i]), i == kPassword);
      } else {
        submit(app);
      }
      break;
    case InputEvent::PlayPause:
      submit(app);
      break;
    case InputEvent::Menu:
    case InputEvent::Back:
      app.open_menu("Pelagia", {app.language_menu_item(),
                           MenuItem{util::tr(util::Str::QuitApp), [](App& a) { a.request_quit(); }}});
      break;
    default:
      break;
  }
}

void LoginScreen::submit(App& app) {
  const std::string server = complete_server_address(fields_[kServer]);
  if (server.empty() || fields_[kUser].empty()) {
    message_ = server.empty() ? util::tr(util::Str::EnterServer) : util::tr(util::Str::EnterUsername);
    error_ = true;
    focus_.set_index(server.empty() ? kServer : kUser);
    return;
  }
  connecting_ = true;
  error_ = false;
  message_ = util::trf(util::Str::ConnectingTo, server.c_str());
  const std::string user = fields_[kUser];
  const std::string password = fields_[kPassword];
  app.run<LoginResult>(
      lifetime(),
      [server, user, password](Backend& b, LoginResult& r) {
        r.result = b.login(server, user, password, &r.session);
      },
      [this, &app](LoginResult& r) {
        connecting_ = false;
        if (r.result.ok()) {
          fields_[kPassword].clear();
          app.on_logged_in(r.session);
          return;
        }
        error_ = true;
        message_ = describe_error(r.result);
        if (r.result.http_status == 401) {
          fields_[kPassword].clear();
          focus_.set_index(kPassword);
        }
      });
}

void LoginScreen::draw(App& app, Painter& p) {
  (void)app;
  const int x = 560;
  const int w = 800;
  p.label("Pelagia", x, 110, Weight::Bold, theme::kTitle, theme::accent());
  p.label(util::tr(util::Str::AboutTagline), x, 190, Weight::Regular, theme::kBody,
          theme::text_dim());
  int y = 270;
  for (int i = 0; i < 3; ++i) {
    const bool focused = focus_.index() == i;
    p.label(util::tr(kLabels[i]), x, y, Weight::Regular, theme::kSmall, theme::text_dim());
    const Rect box = rect(x, y + 40, w, 72);
    p.fill(box, focused ? theme::surface_focus() : theme::surface());
    if (focused) p.border(box, 4, theme::focus_ring());
    const std::string value = i == kPassword ? masked(fields_[i]) : fields_[i];
    if (value.empty()) {
      p.label_in(i == kServer ? util::tr(util::Str::ServerPlaceholder) : "", rect(box.x + 20, box.y, box.w - 40, box.h), Weight::Regular,
                 theme::kBody, rgba(100, 106, 124));
    } else {
      p.label_in(value, rect(box.x + 20, box.y, box.w - 40, box.h), Weight::Regular,
                 theme::kBody, theme::text());
    }
    y += 140;
  }
  const Rect button = rect(x, y + 20, w, 80);
  p.button(button, connecting_ ? util::tr(util::Str::Connecting) : util::tr(util::Str::SignIn), focus_.index() == kSubmit);
  if (connecting_) {
    p.spinner(button.x + button.w - 50, button.y + button.h / 2, 18, theme::accent());
  }
  if (!message_.empty()) {
    p.label_in(message_, rect(x - 200, y + 130, w + 400, 44), Weight::Regular, theme::kBody,
               error_ ? theme::danger() : theme::text_dim(), Align::Center);
  }
  p.hints(util::tr(util::Str::LoginHint));
  keyboard_.draw(p);
}

}  // namespace ui
