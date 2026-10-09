// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UI_APP_H
#define PELAGIA_CORE_UI_APP_H

// Application: screen stack, options menu, temporary messages, and
// shared services (backend, tasks, posters, text, saved session).
// Platform independent: the main loop (app_main.cpp or the
// capture mode) passes it the inputs, the time and a Canvas.
//
// Typical loop:  handle(input)*  ->  update(now)  ->  draw()

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/backend.h"
#include "ui/painter.h"
#include "ui/poster_cache.h"
#include "ui/screen.h"
#include "ui/session_store.h"
#include "ui/settings_store.h"
#include "ui/tasks.h"
#include "ui/text_renderer.h"
#include "util/i18n.h"

namespace ui {

class PlaybackSession;

struct MenuItem {
  std::string label;
  std::function<void(App&)> action;
  bool checked = false;  // current choice in a selection menu (tracks)
};

class App {
 public:
  struct Deps {
    Backend* backend = nullptr;
    TaskRunner* api = nullptr;  // UI requests (worker threads)
    MainQueue* main = nullptr;
    PosterCache* posters = nullptr;
    TextRenderer* text = nullptr;
    Canvas* canvas = nullptr;
    SessionStore* sessions = nullptr;
    // Saved settings (language). Null: the current language is left as it is
    // (capture, tests).
    SettingsStore* settings = nullptr;
    // Languages preferred by the system (platform::preferred_locales()), used by
    // the "Automatic" language setting.
    std::vector<std::string> system_locales;
    player::AudioSink* audio = nullptr;  // null: playback without sound (capture, tests)
  };

  explicit App(const Deps& deps);
  ~App();

  // First screen: saved session (resume) or sign-in. server:
  // address imposed by the command line (empty = the saved one).
  void start(const std::string& server);

  void handle(const platform::Input& in);
  void update(uint64_t now_ms);
  void draw();

  bool quit_requested() const { return quit_; }
  void request_quit() { quit_ = true; }
  bool wants_text_input() const;
  // Nothing moves anymore: no task, no pending result or poster.
  bool idle() const;

  // Navigation (deferred until the end of the current call).
  void push(std::unique_ptr<Screen> screen);
  void pop();
  void pop_to_root();
  void replace_all(std::unique_ptr<Screen> screen);
  Screen* top() const { return stack_.empty() ? nullptr : stack_.back().get(); }
  size_t depth() const { return stack_.size(); }

  // Menu d'options modal (bouton Options).
  // initial_index: element selected at opening (track menu: the current
  // choice). Beyond kMenuVisible elements, the list scrolls.
  void open_menu(const std::string& title, std::vector<MenuItem> items, int initial_index = 0);
  bool menu_open() const { return !menu_items_.empty(); }
  int menu_index() const { return menu_index_; }
  const std::string& menu_title() const { return menu_title_; }
  // First element displayed (scrolling list).
  int menu_scroll() const { return menu_scroll_; }
  static constexpr int kMenuVisible = 8;
  const std::vector<MenuItem>& menu_items() const { return menu_items_; }

  // Temporary message at the bottom of the screen.
  void toast(const std::string& text, bool error = false);
  const std::string& toast_text() const { return toast_; }

  // Work on an API thread, result applied on the main thread
  // only if owner still exists (screen not closed in the meantime).
  template <typename T>
  void run(const Lifetime& owner, std::function<void(Backend&, T&)> work,
           std::function<void(T&)> done) {
    std::shared_ptr<T> result = std::make_shared<T>();
    const Lifetime::Token token = owner.token();
    MainQueue* main = deps_.main;
    Backend* backend = deps_.backend;
    deps_.api->post([=]() {
      work(*backend, *result);
      main->post([=]() {
        if (Lifetime::alive(token)) {
          done(*result);
        }
      });
    });
  }

  // Interface language. The setting is saved; the current language
  // (util::tr) changes at once and language_version() increases, so that the
  // screens rebuild the texts they keep.
  util::LanguageSetting language_setting() const { return settings_.language; }
  void set_language_setting(util::LanguageSetting setting);
  uint64_t language_version() const { return language_version_; }
  // "Language: ..." item of the Options menus, and the selection menu it opens.
  MenuItem language_menu_item();
  void open_language_menu();

  // Session.
  void on_logged_in(const SavedSession& session);
  void logout();
  const std::string& server() const { return server_; }

  // Playback: opens the player screen (asynchronous, see PlayerScreen).
  // tracks: tracks chosen on the page (empty: server's choice).
  void play(const api::MediaItem& item, int64_t start_ms,
            const api::TrackSelection& tracks = api::TrackSelection());
  // Playback ended by the user or the end of the movie: stop and stop
  // report in the background; at the end, data_version() changes.
  void retire_playback(std::unique_ptr<PlaybackSession> session);
  size_t retiring_count() const { return retiring_.size(); }
  // Redraw requested (new video frame, input) when the loop runs
  // in fast mode (wants_fast_ticks).
  void mark_dirty() { dirty_ = true; }
  bool take_dirty() {
    const bool d = dirty_;
    dirty_ = false;
    return d;
  }
  bool fast_ticks() const { return top() && top()->wants_fast_ticks(); }
  // Data modified by a playback (resume position): screens
  // reload when the version changes and no stop is in progress.
  uint64_t data_version() const { return data_version_; }
  void bump_data_version() { ++data_version_; }
  bool playback_stopping() const { return stopping_ > 0; }
  void playback_stop_started() { ++stopping_; }
  void playback_stop_finished();

  Backend& backend() { return *deps_.backend; }
  SessionStore& sessions() { return *deps_.sessions; }
  Painter& painter() { return painter_; }
  TextRenderer& text() { return *deps_.text; }
  Canvas& canvas() { return *deps_.canvas; }
  MainQueue& main_queue() { return *deps_.main; }
  TaskRunner& api_runner() { return *deps_.api; }
  player::AudioSink* audio() { return deps_.audio; }
  uint64_t now() const { return now_ms_; }

 private:
  enum class NavOp { Push, Pop, PopToRoot, ReplaceAll };
  struct PendingNav {
    NavOp op;
    std::unique_ptr<Screen> screen;
  };

  void apply_navigation();
  void handle_menu(const platform::Input& in);
  void draw_menu();
  void draw_toast();

  Deps deps_;
  Painter painter_;
  std::vector<std::unique_ptr<Screen>> stack_;
  std::vector<PendingNav> pending_;
  std::string menu_title_;
  std::vector<MenuItem> menu_items_;
  int menu_index_ = 0;
  int menu_scroll_ = 0;
  std::string toast_;
  bool toast_error_ = false;
  uint64_t toast_until_ = 0;
  uint64_t now_ms_ = 0;
  uint64_t data_version_ = 1;
  int stopping_ = 0;
  std::string server_;
  Settings settings_;
  uint64_t language_version_ = 0;
  bool quit_ = false;
  bool logging_out_ = false;
  bool dirty_ = true;
  std::vector<std::unique_ptr<PlaybackSession>> retiring_;
  Lifetime lifetime_;  // tasks started by App itself (sign-out)
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_APP_H
