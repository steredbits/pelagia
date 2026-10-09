// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// UI navigation without rendering: in-memory backend, tasks run
// by hand (deterministic), abstract inputs as with a gamepad. Checks
// the screens reached, the focus, text entry (on-screen and physical keyboard), the
// saved session, sign-out and stale results.

#include <string>
#include <vector>

#include "test_framework.h"
#include "ui/app.h"
#include "ui/screens/detail_screen.h"
#include "ui/screens/grid_screen.h"
#include "ui/screens/home_screen.h"
#include "ui/screens/login_screen.h"
#include "ui/screens/series_screen.h"
#include "ui/screens/splash_screen.h"
#include "ui_harness.h"
#include "util/version.h"

namespace {

using namespace uitest;

// Sign-in with a given password, on the physical keyboard.
void login_with_keyboard(Harness& h, const std::string& password) {
  h.type("srv:8096");
  h.press(InputEvent::Down);
  h.type("test");
  h.press(InputEvent::Down);
  h.type(password);
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);
  h.settle();
}

}  // namespace

static void test_login_keyboard_and_session() {
  Harness h;
  h.app->start("");
  CHECK(h.screen() == "login");
  auto* login = h.top<ui::LoginScreen>();
  CHECK_EQ(login->focus(), ui::LoginScreen::kServer);  // no known address
  CHECK(h.app->wants_text_input());                    // the physical keyboard types
  // Empty fields: no request, message.
  h.press(InputEvent::Down, 3);
  h.press(InputEvent::Ok);
  CHECK_EQ(h.backend.login_calls, 0);
  CHECK(login->error());
  CHECK_EQ(login->focus(), ui::LoginScreen::kServer);

  // Wrong password: message, password erased, we stay.
  login_with_keyboard(h, "faux");
  CHECK(h.screen() == "login");
  CHECK(login->message() == "Username or password refused");
  CHECK(login->field(ui::LoginScreen::kPassword).empty());
  CHECK_EQ(login->focus(), ui::LoginScreen::kPassword);
  CHECK(!h.files.files.count("/conf/session.json"));

  // Q, space, comma... typed as text in a field; Backspace
  // (Erase) erases.
  h.type("Q ,");
  CHECK(login->field(ui::LoginScreen::kPassword) == "Q ,");
  h.press(InputEvent::Erase, 3);
  h.type("test");
  h.press(InputEvent::Down);
  CHECK(!h.app->wants_text_input());  // on the button: no more typing
  h.press(InputEvent::Ok);
  CHECK(login->connecting());
  h.settle();
  CHECK(h.screen() == "home");
  CHECK(!h.app->wants_text_input());
  // Saved session: completed address + token, private, without a password.
  const MemoryFileStore::File& f = h.files.files["/conf/session.json"];
  CHECK(f.private_file);
  CHECK(f.data.find("http://srv:8096") != std::string::npos);
  CHECK(f.data.find("tok-new") != std::string::npos);
  CHECK(f.data.find("\"test\"") == std::string::npos);
}

// Gamepad navigation, on the French (AZERTY) layout.
static void test_virtual_keyboard() {
  util::ScopedLanguage french(util::Lang::Fr);
  Harness h;
  h.app->start("http://srv:8096");
  auto* login = h.top<ui::LoginScreen>();
  CHECK_EQ(login->focus(), ui::LoginScreen::kUser);  // adresse connue
  h.press(InputEvent::Ok);                            // clavier virtuel
  const ui::VirtualKeyboard& vk = login->keyboard();
  CHECK(vk.is_open());
  CHECK(vk.focused_label() == "a");
  h.press(InputEvent::Right);  // z
  h.press(InputEvent::Ok);
  h.press(InputEvent::Down);   // next row, same column: s
  h.press(InputEvent::Ok);
  CHECK(login->field(ui::LoginScreen::kUser) == "zs");
  h.press(InputEvent::Left, 2);  // wrapping: from s (col 1) to m (col 9)
  CHECK(vk.focused_label() == "m");
  // Bottom row: wide "Space" key, then going back up through the center.
  h.press(InputEvent::Down, 3);
  CHECK(vk.focused_label() == "Valider");
  h.press(InputEvent::Left);
  CHECK(vk.focused_label() == "Espace");
  h.press(InputEvent::Up);
  CHECK(vk.focused_label() == "_");  // center of "Space" = column 4.5
  h.press(InputEvent::Left, 3);
  CHECK(vk.focused_label() == "Maj");
  h.press(InputEvent::Ok);
  CHECK(vk.layout() == ui::VirtualKeyboard::Layout::Upper);
  h.press(InputEvent::Up, 3);  // A
  h.press(InputEvent::Ok);
  CHECK(login->field(ui::LoginScreen::kUser) == "zsA");
  // Shortcuts: square erases, R1 space, physical text accepted.
  h.press(InputEvent::Erase);
  h.press(InputEvent::SeekFwd);
  h.type("x");
  CHECK(login->field(ui::LoginScreen::kUser) == "zs x");
  // Circle closes without changing field; Start/triangle confirms and moves to the next one.
  h.press(InputEvent::Back);
  CHECK(!vk.is_open());
  CHECK_EQ(login->focus(), ui::LoginScreen::kUser);
  h.press(InputEvent::Ok);
  h.press(InputEvent::PlayPause);
  CHECK(!vk.is_open());
  CHECK_EQ(login->focus(), ui::LoginScreen::kPassword);
  // Symbols keyboard, then back to letters.
  h.press(InputEvent::Ok);
  h.press(InputEvent::Down, 4);
  h.press(InputEvent::Ok);  // "&?123"
  CHECK(vk.layout() == ui::VirtualKeyboard::Layout::Symbols);
  h.press(InputEvent::Ok);  // "abc"
  CHECK(vk.layout() == ui::VirtualKeyboard::Layout::Lower);
}

static void test_saved_session() {
  {
    Harness h;
    save_session(h, "good");
    h.app->start("");
    CHECK(h.screen() == "splash");
    h.settle();
    CHECK(h.screen() == "home");
  }
  {
    // Revoked token: session erased, sign-in with the pre-filled address.
    Harness h;
    save_session(h, "revoked");
    h.app->start("");
    h.settle();
    CHECK(h.screen() == "login");
    CHECK(h.top<ui::LoginScreen>()->field(0) == "http://srv:8096");
    CHECK(h.top<ui::LoginScreen>()->message() == "Session expired: sign in again.");
    CHECK(!h.files.files.count("/conf/session.json"));
  }
  {
    // Server unreachable: session kept, "Retry" or "Change server".
    Harness h;
    save_session(h, "offline");
    h.app->start("");
    h.settle();
    CHECK(h.screen() == "splash");
    CHECK(h.top<ui::SplashScreen>()->failed());
    CHECK(h.files.files.count("/conf/session.json"));
    h.press(InputEvent::Ok);  // retry
    CHECK(!h.top<ui::SplashScreen>()->failed());
    h.settle();
    h.press(InputEvent::Right);
    h.press(InputEvent::Ok);  // change server
    h.settle();
    CHECK(h.screen() == "login");
  }
  {
    // --server different from the saved session: sign-in to that server.
    Harness h;
    save_session(h, "good");
    h.app->start("autre:8096");
    CHECK(h.screen() == "login");
    CHECK(h.top<ui::LoginScreen>()->field(0) == "http://autre:8096");
  }
}

static void test_home_grid_detail() {
  Harness h;
  save_session(h, "good");
  h.app->start("");
  h.settle();
  auto* home = h.top<ui::HomeScreen>();
  CHECK(home->loaded());
  CHECK_EQ(home->rows().size(), 4u);
  CHECK(home->rows()[0].title == "Continue watching");
  CHECK(home->rows()[1].kind == ui::HomeScreen::RowKind::Libraries);
  CHECK(home->rows()[2].title == "Latest additions: Films");
  CHECK_EQ(home->focused_row(), 0);
  // Position memory per row.
  h.press(InputEvent::Down, 2);
  h.press(InputEvent::Right, 2);
  h.press(InputEvent::Up);
  h.press(InputEvent::Down);
  CHECK_EQ(home->focused_index(), 2);
  h.press(InputEvent::Right, 10);  // edge: stays on the last one
  CHECK_EQ(home->focused_index(), 3);

  // Movies library: sorted grid (U+200E and accents fixed).
  h.press(InputEvent::Up);
  h.press(InputEvent::Ok);
  h.settle();
  CHECK(h.screen() == "grid");
  auto* grid = h.top<ui::GridScreen>();
  CHECK_EQ(grid->items().size(), 12u);
  CHECK(grid->items()[0].name == "\xE2\x80\x8E" "À bout de souffle");
  CHECK(grid->items()[3].name == "Été 85");
  CHECK(grid->items()[11].name == "Zodiac");
  h.press(InputEvent::Right, 2);
  h.press(InputEvent::Down);  // 2 + 7 = 9
  CHECK_EQ(grid->focus(), 9);
  h.press(InputEvent::Down);  // no 3rd row: stays
  CHECK_EQ(grid->focus(), 9);
  h.press(InputEvent::Up);
  h.press(InputEvent::Ok);  // "Crazy", started
  h.settle();
  CHECK(h.screen() == "detail");
  auto* detail = h.top<ui::DetailScreen>();
  CHECK(detail->refreshed());
  CHECK(detail->item().overview == "Résumé à jour");
  CHECK_EQ(detail->buttons().size(), 2u);
  CHECK(detail->buttons()[0] == "Resume at 20:00");
  CHECK(detail->buttons()[1] == "Play from the beginning");

  // End of a playback (stop report sent): the page is re-read.
  const int calls = h.backend.item_calls;
  h.app->playback_stop_started();
  h.settle();
  CHECK_EQ(h.backend.item_calls, calls);  // not during the stop
  h.app->playback_stop_finished();
  h.settle();
  CHECK_EQ(h.backend.item_calls, calls + 1);

  h.press(InputEvent::Back);
  h.settle();
  CHECK(h.screen() == "grid");
  CHECK_EQ(h.top<ui::GridScreen>()->focus(), 2);  // focus kept
  h.press(InputEvent::Back);
  h.settle();
  CHECK(h.screen() == "home");
  CHECK_EQ(h.top<ui::HomeScreen>()->focused_row(), 1);
}

static void test_series() {
  Harness h;
  save_session(h, "good");
  h.app->start("");
  h.settle();
  h.press(InputEvent::Down);
  h.press(InputEvent::Right);
  h.press(InputEvent::Ok);  // Series
  h.settle();
  h.press(InputEvent::Ok);  // the series
  h.settle();
  CHECK(h.screen() == "series");
  auto* s = h.top<ui::SeriesScreen>();
  CHECK_EQ(s->seasons().size(), 2u);
  CHECK_EQ(s->shown_season(), 0);
  CHECK_EQ(s->episodes().size(), 2u);
  // Season 2 requested then left before the response: the stale result
  // (season 2) is ignored, season 1 is displayed.
  h.press(InputEvent::Down);
  h.press(InputEvent::Up);
  h.settle();
  CHECK_EQ(s->shown_season(), 0);
  CHECK_EQ(s->episodes().size(), 2u);
  h.press(InputEvent::Down);
  h.settle();
  CHECK_EQ(s->shown_season(), 1);
  CHECK_EQ(s->episodes().size(), 3u);
  h.press(InputEvent::Right);
  CHECK_EQ(s->column(), 1);
  h.press(InputEvent::Down, 5);
  CHECK_EQ(s->episode_focus(), 2);
  h.press(InputEvent::Ok);
  h.settle();
  CHECK(h.screen() == "detail");
  CHECK(h.top<ui::DetailScreen>()->item().id == "season2-e3");
}

static void test_screen_closed_before_result() {
  Harness h;
  save_session(h, "good");
  h.app->start("");
  h.settle();
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);   // grid: loading started...
  h.app->update(h.now += 16);
  h.press(InputEvent::Back); // ... and left before the response
  h.settle();                // result ignored, without a crash
  CHECK(h.screen() == "home");
  CHECK(h.app->idle());
}

static void test_options_logout_quit() {
  Harness h;
  save_session(h, "good");
  h.app->start("");
  h.settle();
  h.press(InputEvent::Back);  // circle on the home screen: menu
  CHECK(h.app->menu_open());
  CHECK_EQ(h.app->menu_items().size(), 5u);  // Refresh, Language, sign out, About, Quit
  CHECK(h.app->menu_title() == std::string("Options — Pelagia ") + util::kVersionTag);
  h.press(InputEvent::Back);
  CHECK(!h.app->menu_open());
  h.press(InputEvent::Menu);
  h.press(InputEvent::Down, 2);
  h.press(InputEvent::Ok);  // Sign out
  CHECK(!h.files.files.count("/conf/session.json"));  // erased right away
  h.settle();
  CHECK_EQ(h.backend.logout_calls, 1);
  CHECK(h.screen() == "login");
  CHECK(h.top<ui::LoginScreen>()->field(0) == "http://srv:8096");
  CHECK(h.top<ui::LoginScreen>()->message() == "You are signed out.");
  // Quit from the menu of the sign-in screen (Language, Quit), and Quit event
  // (Ctrl+Q, window closed).
  h.press(InputEvent::Menu);
  CHECK_EQ(h.app->menu_items().size(), 2u);
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);
  CHECK(h.app->quit_requested());
  Harness h2;
  h2.app->start("");
  h2.press(InputEvent::Quit);
  CHECK(h2.app->quit_requested());
}

// Options > About: static screen, circle to go back to the home screen.
static void test_about_screen() {
  Harness h;
  save_session(h, "good");
  h.app->start("");
  h.settle();
  h.press(InputEvent::Menu);
  h.press(InputEvent::Down, 3);
  CHECK(h.app->menu_items()[3].label == "About");
  h.press(InputEvent::Ok);
  CHECK(h.screen() == "about");
  h.press(InputEvent::Back);
  CHECK(h.screen() == "home");
}

// After a playback, the "Continue watching" row is reloaded and reordered
// (most recent first): the focus follows the element, not the position.
static void test_resume_row_follows_item() {
  Harness h;
  h.backend.resume_list = {h.backend.movies[3], h.backend.movies[5]};
  save_session(h, "good");
  h.app->start("");
  h.settle();
  auto* home = h.top<ui::HomeScreen>();
  h.press(InputEvent::Right);
  CHECK(home->rows()[0].items[home->focused_index()].id == "m5");
  h.backend.resume_list = {h.backend.movies[5], h.backend.movies[3]};
  h.app->playback_stop_started();
  h.app->playback_stop_finished();  // new version of the data
  h.settle();
  CHECK(home->rows()[0].items[0].id == "m5");
  CHECK_EQ(home->focused_index(), 0);
}

// Out-of-order responses (libraries before "Continue watching"): the focus
// still starts from the first row, and navigation waits for the end of
// loading (finding from capture mode).
static void test_home_focus_with_out_of_order_results() {
  Harness h;
  save_session(h, "good");
  h.app->start("");
  h.api.run_all();              // session restoration only
  h.app->update(h.now += 16);   // -> home, loads posted
  CHECK(h.screen() == "home");
  auto* home = h.top<ui::HomeScreen>();
  h.press(InputEvent::Down);  // ignored: not loaded yet
  h.api.run_all_reversed();
  h.settle();
  CHECK(home->loaded());
  CHECK(home->rows()[0].kind == ui::HomeScreen::RowKind::Resume);
  CHECK_EQ(home->focused_row(), 0);
  h.press(InputEvent::Down);
  CHECK(home->rows()[home->focused_row()].kind == ui::HomeScreen::RowKind::Libraries);
}

// Options > Language: automatic (system locales), manual choice, saved setting,
// texts kept by the screens, keyboard layout.
static void test_language_option() {
  const util::ScopedLanguage restore(util::current_language());
  Harness h(true, {"fr_FR", "en_US"});
  CHECK(util::current_language() == util::Lang::Fr);  // automatic: the system's language
  CHECK(h.app->language_setting() == util::LanguageSetting::Auto);
  save_session(h, "good");
  h.app->start("");
  h.settle();
  auto* home = h.top<ui::HomeScreen>();
  CHECK(home->rows()[0].title == "Reprendre la lecture");

  h.press(InputEvent::Menu);
  CHECK(h.app->menu_items()[1].label == "Langue : Automatique (Français)");
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);  // language menu
  CHECK(h.app->menu_open());
  CHECK(h.app->menu_title() == "Langue");
  CHECK_EQ(h.app->menu_items().size(), 3u);
  CHECK(h.app->menu_items()[0].label == "Automatique (Français)" && h.app->menu_items()[0].checked);
  CHECK(h.app->menu_items()[1].label == "English" && !h.app->menu_items()[1].checked);
  CHECK(h.app->menu_items()[2].label == "Français");
  CHECK_EQ(h.app->menu_index(), 0);
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);  // English
  h.settle();
  CHECK(!h.app->menu_open());
  CHECK(util::current_language() == util::Lang::En);
  CHECK(h.app->language_setting() == util::LanguageSetting::English);
  CHECK(h.settings.load().language == util::LanguageSetting::English);  // saved
  CHECK(h.top<ui::HomeScreen>()->rows()[0].title == "Continue watching");

  // The Options menu follows, and the choice is checked when the menu is reopened.
  h.press(InputEvent::Menu);
  CHECK(h.app->menu_items()[1].label == "Language: English");
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);
  CHECK_EQ(h.app->menu_index(), 1);
  CHECK(h.app->menu_items()[1].checked);
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);  // French
  CHECK(util::current_language() == util::Lang::Fr);
  CHECK(h.settings.load().language == util::LanguageSetting::French);

  // Back to automatic: unknown system languages mean English.
  Harness other(true, {"ja_JP"});
  CHECK(util::current_language() == util::Lang::En);
  CHECK(other.app->language_setting() == util::LanguageSetting::Auto);
  other.app->start("");
  other.press(InputEvent::Menu);  // sign-in screen: Language, Quit
  CHECK(other.app->menu_items()[0].label == "Language: Automatic (English)");

  // A saved setting wins over the system.
  Harness saved(false);
  ui::Settings s;
  s.language = util::LanguageSetting::French;
  saved.settings.save(s);
  {
    ui::App::Deps d;
    d.backend = &saved.backend;
    d.api = &saved.api;
    d.main = &saved.main;
    d.posters = &saved.posters;
    d.text = &saved.text;
    d.canvas = &saved.canvas;
    d.sessions = &saved.sessions;
    d.settings = &saved.settings;
    d.system_locales = {"en_US"};
    ui::App app(d);
    CHECK(util::current_language() == util::Lang::Fr);
  }
}

// QWERTY layout in English, AZERTY in French (letter rows from the string catalog).
static void test_keyboard_layouts() {
  {
    util::ScopedLanguage english(util::Lang::En);
    Harness h;
    h.app->start("http://srv:8096");
    h.press(InputEvent::Ok);
    const ui::VirtualKeyboard& vk = h.top<ui::LoginScreen>()->keyboard();
    CHECK(vk.focused_label() == "q");
    h.press(InputEvent::Down, 3);
    CHECK(vk.focused_label() == "Shift");
    // The layout follows a language change while the keyboard is open.
    util::set_current_language(util::Lang::Fr);
    CHECK(vk.focused_label() == "Maj");
  }
}

int main() {
  test_language_option();
  test_keyboard_layouts();
  test_login_keyboard_and_session();
  test_virtual_keyboard();
  test_saved_session();
  test_home_grid_detail();
  test_series();
  test_screen_closed_before_result();
  test_options_logout_quit();
  test_about_screen();
  test_resume_row_follows_item();
  test_home_focus_with_out_of_order_results();
  return testfw::test_failures();
}
