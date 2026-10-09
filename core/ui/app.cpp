// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "ui/app.h"

#include <cstdio>

#include "ui/format.h"
#include "ui/playback_session.h"
#include "ui/screens/screens.h"
#include "ui/theme.h"
#include "util/log.h"

namespace ui {

namespace {

using platform::InputEvent;

struct LogoutResult {
  api::ApiResult result;
};

}  // namespace

App::App(const Deps& deps) : deps_(deps), painter_(deps.canvas, deps.text, deps.posters) {
  if (deps_.settings) {
    settings_ = deps_.settings->load();
    const util::Lang lang = util::resolve_language(settings_.language, deps_.system_locales);
    util::set_current_language(lang);
    std::string locales;
    for (const std::string& l : deps_.system_locales) locales += (locales.empty() ? "" : ", ") + l;
    LOG_INFO("Language: setting %s, system locales [%s], interface in %s",
             util::language_setting_code(settings_.language),
             locales.empty() ? "none" : locales.c_str(), util::language_code(lang));
  }
}

App::~App() {
  // Screens destroyed before the services they use; playbacks
  // being stopped are awaited (stop report bounded to a few seconds).
  pending_.clear();
  while (!stack_.empty()) {
    stack_.pop_back();
  }
  retiring_.clear();
}

void App::start(const std::string& server) {
  const std::string forced = complete_server_address(server);
  SavedSession saved;
  const bool has_saved = deps_.sessions->load(&saved);
  if (has_saved && (forced.empty() || forced == saved.server)) {
    server_ = saved.server;
    LOG_INFO("Saved session found for %s", server_.c_str());
    replace_all(make_splash_screen(saved));
  } else {
    server_ = !forced.empty() ? forced : saved.server;
    replace_all(make_login_screen(server_, ""));
  }
  apply_navigation();
}

void App::push(std::unique_ptr<Screen> screen) {
  pending_.push_back(PendingNav{NavOp::Push, std::move(screen)});
}

void App::pop() { pending_.push_back(PendingNav{NavOp::Pop, nullptr}); }

void App::pop_to_root() { pending_.push_back(PendingNav{NavOp::PopToRoot, nullptr}); }

void App::replace_all(std::unique_ptr<Screen> screen) {
  pending_.push_back(PendingNav{NavOp::ReplaceAll, std::move(screen)});
}

void App::apply_navigation() {
  // Applied outside any screen call: a screen can request its own
  // closing without being destroyed while it runs.
  while (!pending_.empty()) {
    PendingNav nav = std::move(pending_.front());
    pending_.erase(pending_.begin());
    switch (nav.op) {
      case NavOp::Push:
        stack_.push_back(std::move(nav.screen));
        LOG_DEBUG("Screen: %s", stack_.back()->name());
        stack_.back()->enter(*this);
        break;
      case NavOp::Pop:
        if (stack_.size() > 1) {
          stack_.pop_back();
          stack_.back()->resume(*this);
        }
        break;
      case NavOp::PopToRoot:
        if (stack_.size() > 1) {
          stack_.resize(1);
          stack_.back()->resume(*this);
        }
        break;
      case NavOp::ReplaceAll:
        stack_.clear();
        stack_.push_back(std::move(nav.screen));
        LOG_DEBUG("Screen: %s", stack_.back()->name());
        stack_.back()->enter(*this);
        break;
    }
  }
}

void App::handle(const platform::Input& in) {
  dirty_ = true;
  if (in.event == InputEvent::Quit) {
    quit_ = true;
    return;
  }
  if (logging_out_) {
    return;
  }
  if (menu_open()) {
    handle_menu(in);
  } else if (top()) {
    top()->handle(*this, in);
  }
  apply_navigation();
}

void App::update(uint64_t now_ms) {
  now_ms_ = now_ms;
  painter_.set_now(now_ms);
  deps_.main->drain();
  apply_navigation();
  deps_.posters->begin_frame(now_ms);
  if (top()) {
    top()->update(*this);
  }
  apply_navigation();
  if (!toast_.empty() && now_ms >= toast_until_) {
    toast_.clear();
  }
}

void App::draw() {
  if (!top() || !top()->shows_video()) {
    deps_.canvas->fill_rect(rect(0, 0, theme::kScreenW, theme::kScreenH), theme::background());
  }
  if (top()) {
    top()->draw(*this, painter_);
  }
  if (logging_out_) {
    painter_.fill(rect(0, 0, theme::kScreenW, theme::kScreenH), theme::scrim());
    painter_.status(rect(0, 400, theme::kScreenW, 200), util::tr(util::Str::SigningOut), true, false);
  }
  draw_menu();
  draw_toast();
}

bool App::wants_text_input() const {
  return !menu_open() && top() && top()->wants_text_input();
}

bool App::idle() const {
  return deps_.api->pending() == 0 && deps_.main->size() == 0 && !deps_.posters->busy() &&
         pending_.empty() && !logging_out_ && retiring_.empty() && !(top() && top()->busy());
}

void App::open_menu(const std::string& title, std::vector<MenuItem> items,
                    int initial_index) {
  menu_title_ = title;
  menu_items_ = std::move(items);
  const int n = static_cast<int>(menu_items_.size());
  menu_index_ = initial_index >= 0 && initial_index < n ? initial_index : 0;
  menu_scroll_ = 0;
  // Brings the initial element into the visible window.
  if (menu_index_ >= kMenuVisible) {
    menu_scroll_ = menu_index_ - kMenuVisible + 1;
  }
}

void App::handle_menu(const platform::Input& in) {
  const int n = static_cast<int>(menu_items_.size());
  switch (in.event) {
    case InputEvent::Up:
      if (menu_index_ > 0) --menu_index_;
      if (menu_index_ < menu_scroll_) menu_scroll_ = menu_index_;
      break;
    case InputEvent::Down:
      if (menu_index_ + 1 < n) ++menu_index_;
      if (menu_index_ >= menu_scroll_ + kMenuVisible) menu_scroll_ = menu_index_ - kMenuVisible + 1;
      break;
    case InputEvent::Ok: {
      // Copy: the action may open another menu.
      std::function<void(App&)> action = menu_items_[menu_index_].action;
      menu_items_.clear();
      action(*this);
      break;
    }
    case InputEvent::Back:
    case InputEvent::Menu:
      menu_items_.clear();
      break;
    default:
      break;
  }
}

void App::draw_menu() {
  if (!menu_open()) {
    return;
  }
  const int item_h = 76;
  const int count = static_cast<int>(menu_items_.size());
  const int visible = count < kMenuVisible ? count : kMenuVisible;
  // Width according to the labels (tracks: "French · E-AC3 5.1 · title").
  int widest = 0;
  for (const MenuItem& it : menu_items_) {
    const int lw = painter_.text().measure(it.label, Weight::Bold, theme::kBody);
    if (lw > widest) widest = lw;
  }
  int w = widest + 80 + 96;  // margins + room for the check mark
  if (w < 640) w = 640;
  if (w > theme::kContentW) w = theme::kContentW;
  const int h = 110 + visible * (item_h + 12) + 30;
  const Rect panel = rect((theme::kScreenW - w) / 2, (theme::kScreenH - h) / 2, w, h);
  painter_.fill(rect(0, 0, theme::kScreenW, theme::kScreenH), theme::scrim());
  painter_.fill(panel, rgba(28, 31, 44));
  painter_.label(menu_title_, panel.x + 40, panel.y + 30, Weight::Bold, theme::kHeading,
                 theme::text());
  if (count > visible) {
    char pos[32];
    std::snprintf(pos, sizeof(pos), "%d / %d", menu_index_ + 1, count);
    painter_.label_in(pos, rect(panel.x + 40, panel.y + 30, w - 80, 50), Weight::Regular,
                      theme::kSmall, theme::text_dim(), Align::Right);
  }
  int y = panel.y + 110;
  for (int i = menu_scroll_; i < count && i < menu_scroll_ + visible; ++i) {
    const bool focused = i == menu_index_;
    const Rect row = rect(panel.x + 40, y, w - 80, item_h);
    painter_.button(row, menu_items_[i].label, focused);
    if (menu_items_[i].checked) {
      // Mark of the current choice (no check mark in the embedded font).
      painter_.fill(rect(row.x + 22, row.y + (item_h - 20) / 2, 20, 20),
                    focused ? theme::background() : theme::accent());
    }
    y += item_h + 12;
  }
}

void App::toast(const std::string& text, bool error) {
  toast_ = text;
  toast_error_ = error;
  toast_until_ = now_ms_ + 4000;
}

void App::draw_toast() {
  if (toast_.empty()) {
    return;
  }
  const int w = painter_.text().measure(toast_, Weight::Regular, theme::kBody) + 80;
  const Rect r = rect((theme::kScreenW - w) / 2, theme::kScreenH - 190, w, 70);
  painter_.fill(r, toast_error_ ? rgba(120, 30, 30, 235) : rgba(40, 44, 60, 235));
  painter_.label_in(toast_, r, Weight::Regular, theme::kBody, theme::text(), Align::Center);
}

void App::on_logged_in(const SavedSession& session) {
  server_ = session.server;
  deps_.sessions->save(session);
  replace_all(make_home_screen());
}

namespace {

// Native name of a language, whatever the interface language.
const char* native_language_name(util::Lang lang) {
  return util::tr(lang == util::Lang::Fr ? util::Str::LanguageFrench : util::Str::LanguageEnglish);
}

std::string language_setting_label(util::LanguageSetting setting,
                                   const std::vector<std::string>& system_locales) {
  switch (setting) {
    case util::LanguageSetting::English: return native_language_name(util::Lang::En);
    case util::LanguageSetting::French: return native_language_name(util::Lang::Fr);
    case util::LanguageSetting::Auto: break;
  }
  return util::trf(util::Str::LanguageAuto,
                   native_language_name(util::language_from_locales(system_locales)));
}

}  // namespace

void App::set_language_setting(util::LanguageSetting setting) {
  settings_.language = setting;
  util::set_current_language(util::resolve_language(setting, deps_.system_locales));
  ++language_version_;
  mark_dirty();
  LOG_INFO("Language: setting %s, interface in %s", util::language_setting_code(setting),
           util::language_code(util::current_language()));
  if (deps_.settings) {
    deps_.settings->save(settings_);
  }
}

MenuItem App::language_menu_item() {
  return MenuItem{util::trf(util::Str::LanguageItem,
                            language_setting_label(settings_.language, deps_.system_locales).c_str()),
                  [](App& a) { a.open_language_menu(); }};
}

void App::open_language_menu() {
  static const util::LanguageSetting kChoices[] = {
      util::LanguageSetting::Auto, util::LanguageSetting::English, util::LanguageSetting::French};
  std::vector<MenuItem> items;
  int initial = 0;
  for (size_t i = 0; i < sizeof(kChoices) / sizeof(kChoices[0]); ++i) {
    const util::LanguageSetting choice = kChoices[i];
    MenuItem item;
    item.label = language_setting_label(choice, deps_.system_locales);
    item.checked = choice == settings_.language;
    item.action = [choice](App& a) { a.set_language_setting(choice); };
    if (item.checked) initial = static_cast<int>(i);
    items.push_back(std::move(item));
  }
  open_menu(util::tr(util::Str::LanguageMenuTitle), std::move(items), initial);
}

void App::logout() {
  // The session file disappears right away; the revocation on the
  // server side follows (without blocking the display).
  deps_.sessions->clear();
  logging_out_ = true;
  run<LogoutResult>(
      lifetime_, [](Backend& b, LogoutResult& r) { r.result = b.logout(); },
      [this](LogoutResult& r) {
        logging_out_ = false;
        replace_all(make_login_screen(server_, r.result.ok()
                                                   ? util::tr(util::Str::SignedOut)
                                                   : util::tr(util::Str::SignedOutOffline)));
      });
}

void App::play(const api::MediaItem& item, int64_t start_ms, const api::TrackSelection& tracks) {
  push(make_player_screen(item, start_ms, tracks));
}

void App::retire_playback(std::unique_ptr<PlaybackSession> session) {
  if (!session) {
    return;
  }
  playback_stop_started();
  PlaybackSession* raw = session.get();
  retiring_.push_back(std::move(session));
  const Lifetime::Token token = lifetime_.token();
  raw->stop_async(deps_.main, [this, raw, token]() {
    if (!Lifetime::alive(token)) {
      return;
    }
    for (size_t i = 0; i < retiring_.size(); ++i) {
      if (retiring_[i].get() == raw) {
        retiring_.erase(retiring_.begin() + static_cast<long>(i));  // threads already finished
        break;
      }
    }
    if (deps_.audio) {
      deps_.audio->flush();  // nothing from the previous playback must linger
    }
    playback_stop_finished();
  });
}

void App::playback_stop_finished() {
  if (stopping_ > 0) {
    --stopping_;
  }
  bump_data_version();
}

}  // namespace ui
