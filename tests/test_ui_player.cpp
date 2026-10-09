// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Player screen in the interface: opening outside the display
// loop, states (loading, playing, paused), seek, asynchronous stop
// with stop report then page refresh, end of the movie,
// opening failure, cancellation during loading, application
// closing in the middle of playback. Synthesized MP4 file, no network.

#include <stdlib.h>

#include <chrono>
#include <functional>
#include <string>
#include <thread>

#include "media_synth.h"
#include "test_framework.h"
#include "ui/screens/detail_screen.h"
#include "ui/screens/player_screen.h"
#include "ui_harness.h"

using namespace uitest;

namespace {

uint64_t real_ms() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

// Runs the application in real time until pred() (or timeout).
bool run_until(Harness& h, const std::function<bool()>& pred, int timeout_ms = 10000) {
  const uint64_t end = real_ms() + timeout_ms;
  while (real_ms() < end) {
    h.api.run_all();
    h.app->update(h.now = real_ms());
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }
  return false;
}

std::string g_media_long;   // 20 s
std::string g_media_short;  // 1 s

// Home -> Movies -> "Crazy" (playback started at 20:00) -> page.
ui::DetailScreen* open_detail(Harness& h) {
  save_session(h, "good");
  h.app->start("");
  h.settle();
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);
  h.settle();
  h.press(InputEvent::Right, 2);
  h.press(InputEvent::Ok);
  h.settle();
  CHECK(h.screen() == "detail");
  return h.top<ui::DetailScreen>();
}

ui::PlayerScreen* player_screen(Harness& h) {
  return h.screen() == "player" ? h.top<ui::PlayerScreen>() : nullptr;
}

}  // namespace

static void test_play_pause_seek_stop() {
  Harness h;
  h.backend.media_path = g_media_long;
  open_detail(h);
  const int item_calls = h.backend.item_calls;
  h.press(InputEvent::Right);  // "Play from the beginning"
  h.press(InputEvent::Ok);
  h.app->update(h.now = real_ms());
  CHECK(h.screen() == "player");
  CHECK_EQ(h.backend.last_play_start, 0);
  ui::PlayerScreen* ps = player_screen(h);
  CHECK(ps != nullptr);
  if (!ps) return;
  CHECK(h.app->fast_ticks());
  // The opening runs on a thread: the loop goes on (no blocking).
  CHECK(run_until(h, [&] { return ps->session()->phase() == ui::PlaybackSession::Phase::Playing; }));
  CHECK(run_until(h, [&] { return ps->session()->player().position_ms() > 400; }));
  CHECK(ps->shows_video());
  CHECK_EQ(h.backend.reports->started.load(), 1);
  // Banner: visible after a key press, hidden 4 s later (during playback).
  h.press(InputEvent::Up);
  CHECK(ps->overlay_visible(h.now));
  CHECK(!ps->overlay_visible(h.now + 5000));
  h.press(InputEvent::PlayPause);
  CHECK(ps->session()->player().state() == player::PlayerState::Paused);
  CHECK(ps->overlay_visible(h.now + 60000));  // always visible while paused
  h.press(InputEvent::Ok);
  CHECK(ps->session()->player().state() == player::PlayerState::Playing);
  // Left/right = 10 s seek in the player (no navigation).
  const int64_t before = ps->session()->player().position_ms();
  h.press(InputEvent::Right);
  CHECK(ps->session()->player().position_ms() >= before + 9000);
  CHECK(run_until(h, [&] { return !ps->session()->player().seek_in_flight(); }));
  h.press(InputEvent::SeekBackLong);  // -60 s: clamped to 0
  CHECK(ps->session()->player().position_ms() < 2000);
  CHECK(run_until(h, [&] { return !ps->busy(); }));

  // Back: background stop, stop report, then page re-read.
  h.press(InputEvent::Back);
  // Checked before update(): the end of the stop is only taken into account when
  // the main queue is drained (update), and the stop thread may already
  // have finished (same cause as in test_destroy_while_stopping).
  CHECK(h.screen() == "detail");
  CHECK(h.app->playback_stopping());
  h.app->update(h.now = real_ms());
  CHECK(run_until(h, [&] { return h.app->retiring_count() == 0; }, 8000));
  CHECK(run_until(h, [&] { return h.backend.item_calls > item_calls; }));
  CHECK_EQ(h.backend.reports->stopped.load(), 1);
  CHECK(!h.app->playback_stopping());
  CHECK(!h.app->fast_ticks());
}

static void test_resume_position() {
  Harness h;
  h.backend.media_path = g_media_long;
  ui::DetailScreen* d = open_detail(h);
  CHECK(d->buttons()[0] == "Resume at 20:00");
  h.press(InputEvent::Ok);  // "Resume from 20:00"
  CHECK_EQ(h.backend.last_play_start, 20 * 60 * 1000);
  h.press(InputEvent::Back);
  CHECK(run_until(h, [&] { return h.app->retiring_count() == 0; }, 8000));
}

static void test_end_of_media() {
  Harness h;
  h.backend.media_path = g_media_short;
  open_detail(h);
  h.press(InputEvent::Right);
  h.press(InputEvent::Ok);
  // End of the movie: automatic return to the page, stop report sent.
  CHECK(run_until(h, [&] { return h.screen() == "detail"; }, 15000));
  CHECK(run_until(h, [&] { return h.app->retiring_count() == 0; }, 8000));
  CHECK_EQ(h.backend.reports->stopped.load(), 1);
}

static void test_open_failure() {
  Harness h;  // media_path inexistant
  open_detail(h);
  h.press(InputEvent::Right);
  h.press(InputEvent::Ok);
  h.app->update(h.now = real_ms());
  ui::PlayerScreen* ps = player_screen(h);
  CHECK(ps != nullptr);
  if (!ps) return;
  CHECK(run_until(h, [&] { return ps->open_failed(); }));
  h.press(InputEvent::PlayPause);  // no effect
  CHECK(h.screen() == "player");
  h.press(InputEvent::Back);
  CHECK(run_until(h, [&] { return h.app->retiring_count() == 0; }));
  CHECK(h.screen() == "detail");
  CHECK_EQ(h.backend.reports->started.load(), 0);
  CHECK_EQ(h.backend.reports->stopped.load(), 0);  // never started
}

static void test_cancel_while_opening() {
  Harness h;
  h.backend.media_path = g_media_long;
  h.backend.stuck_open = true;  // the server does not answer
  open_detail(h);
  h.press(InputEvent::Ok);
  h.app->update(h.now = real_ms());
  ui::PlayerScreen* ps = player_screen(h);
  CHECK(ps != nullptr && ps->opening());
  CHECK(ps != nullptr && ps->busy());
  // The display loop is not blocked during the opening.
  const uint64_t t0 = real_ms();
  for (int i = 0; i < 10; ++i) h.app->update(h.now = real_ms());
  CHECK(real_ms() - t0 < 200);
  h.press(InputEvent::Back);  // annuler
  h.app->update(h.now = real_ms());
  CHECK(h.screen() == "detail");
  const uint64_t t1 = real_ms();
  CHECK(run_until(h, [&] { return h.app->retiring_count() == 0; }, 3000));
  CHECK(real_ms() - t1 < 1000);  // abort_open: no waiting for the timeout
}

static void test_quit_while_playing() {
  std::shared_ptr<ReportLog> log;
  {
    Harness h;
    h.backend.media_path = g_media_long;
    log = h.backend.reports;
    open_detail(h);
    h.press(InputEvent::Right);
    h.press(InputEvent::Ok);
    ui::PlayerScreen* ps = nullptr;
    CHECK(run_until(h, [&] {
      ps = player_screen(h);
      // The player is only readable once the opening (other thread) is finished.
      return ps && ps->session()->phase() == ui::PlaybackSession::Phase::Playing &&
             ps->session()->player().position_ms() > 300;
    }));
    h.press(InputEvent::Quit);
    CHECK(h.app->quit_requested());
  }  // App destroyed in the middle of playback: synchronous stop, position reported
  CHECK_EQ(log->stopped.load(), 1);
  CHECK(log->last_stop_position.load() > 0);
}

// The application closes while the stop of a playback (blocked opening)
// is still in progress: the destructor and the stop thread must not
// join the same thread (race detected by ThreadSanitizer).
static void test_destroy_while_stopping() {
  for (int i = 0; i < 5; ++i) {
    Harness h;
    h.backend.media_path = g_media_long;
    h.backend.stuck_open = true;
    open_detail(h);
    h.press(InputEvent::Ok);
    h.app->update(h.now = real_ms());
    CHECK(h.screen() == "player");
    h.press(InputEvent::Back);  // stop started in the background
    // Checked before update(): the session only leaves the list when
    // the main queue is drained (update), and the stop thread may already have finished
    // (blocked opening interrupted right away); checking after update()
    // made the test unstable on a loaded machine (CI).
    CHECK(h.app->retiring_count() == 1);
    h.app->update(h.now = real_ms());
  }  // ~Harness: the application is destroyed during the stop
}

static api::MediaStreamInfo track_stream(int index, api::StreamKind kind, const char* lang,
                                         const char* codec, bool text = false) {
  api::MediaStreamInfo st;
  st.index = index;
  st.kind = kind;
  st.language = lang;
  st.codec = codec;
  st.channels = kind == api::StreamKind::Audio ? 6 : 0;
  st.is_text = text;
  st.supports_external = text;
  return st;
}

// Player track menu: Options -> Audio / Subtitles, stream reopening
// for audio and burned-in subtitles, immediate change for text,
// reports that carry the tracks.
static void test_track_menu_in_player() {
  Harness h;
  h.backend.media_path = g_media_long;
  api::MediaSourceInfo src;
  src.id = "src-pistes";
  src.streams.push_back(track_stream(1, api::StreamKind::Audio, "eng", "eac3"));
  src.streams.push_back(track_stream(2, api::StreamKind::Audio, "fre", "eac3"));
  src.streams.push_back(track_stream(3, api::StreamKind::Subtitle, "fre", "subrip", true));
  src.streams.push_back(track_stream(4, api::StreamKind::Subtitle, "fre", "PGSSUB"));
  src.server_defaults = true;
  src.default_audio_index = 1;
  src.default_subtitle_index = 3;
  h.backend.movies[3].media_sources.push_back(src);
  open_detail(h);
  h.press(InputEvent::Right);  // "Play from the beginning"
  h.press(InputEvent::Ok);
  h.app->update(h.now = real_ms());
  ui::PlayerScreen* ps = player_screen(h);
  CHECK(ps != nullptr);
  if (!ps) return;
  CHECK_EQ(h.backend.last_play_tracks.audio_index, 1);
  CHECK_EQ(h.backend.last_play_tracks.subtitle_index, 3);
  CHECK(run_until(h, [&] { return ps->session()->phase() == ui::PlaybackSession::Phase::Playing; }));
  CHECK(run_until(h, [&] { return ps->session()->player().position_ms() > 600; }));
  // The first reports already carry the tracks.
  CHECK(run_until(h, [&] { return h.backend.reports->last_audio.load() == 1; }));
  CHECK_EQ(h.backend.reports->last_subtitle.load(), 3);

  // Options: two entries that recall the current tracks.
  h.press(InputEvent::Menu);
  CHECK(h.app->menu_open());
  CHECK_EQ(h.app->menu_items().size(), 2u);
  CHECK(h.app->menu_items()[0].label == "Audio: English · E-AC3 5.1");
  CHECK(h.app->menu_items()[1].label == "Subtitles: French · SRT");
  // Audio: list, current track checked; French reopens the stream.
  h.press(InputEvent::Ok);
  CHECK(h.app->menu_open());
  CHECK_EQ(h.app->menu_items().size(), 2u);
  CHECK(h.app->menu_items()[0].checked && h.app->menu_index() == 0);
  const int64_t before = ps->session()->player().position_ms();
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);
  CHECK(!h.app->menu_open());
  CHECK_EQ(h.backend.reports->set_tracks_calls.load(), 1);
  CHECK(ps->session()->player().seek_in_flight());  // reopening at the current position
  CHECK(ps->track_change_pending());
  CHECK(run_until(h, [&] { return !ps->track_change_pending(); }));
  CHECK(ps->session()->player().position_ms() >= before - 500);
  api::TrackSelection now;
  CHECK(ps->session()->tracks(&now));
  CHECK_EQ(now.audio_index, 2);
  CHECK_EQ(now.subtitle_index, 3);
  // The server is notified right away (Progress with the new tracks).
  CHECK(run_until(h, [&] { return h.backend.reports->last_audio.load() == 2; }));

  // Subtitles rendered by the client (text): "None" without reopening.
  h.press(InputEvent::Menu);
  CHECK(h.app->menu_items()[0].label == "Audio: French · E-AC3 5.1");
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);
  CHECK_EQ(h.app->menu_items().size(), 3u);  // None, SRT, burned-in PGS
  CHECK(h.app->menu_items()[2].label == "French · PGS · burned in");
  CHECK_EQ(h.app->menu_index(), 1);
  h.press(InputEvent::Up);
  h.press(InputEvent::Ok);  // Aucun
  CHECK_EQ(h.backend.reports->set_tracks_calls.load(), 2);
  CHECK(!ps->session()->player().seek_in_flight());
  CHECK(!ps->track_change_pending());
  CHECK(run_until(h, [&] { return h.backend.reports->last_subtitle.load() == -1; }));

  // Image: burned in by the server, hence reopening.
  h.press(InputEvent::Menu);
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);
  h.press(InputEvent::Down, 2);
  h.press(InputEvent::Ok);  // PGS
  CHECK_EQ(h.backend.reports->set_tracks_calls.load(), 3);
  CHECK(ps->track_change_pending());
  CHECK(run_until(h, [&] { return !ps->track_change_pending(); }));
  CHECK(run_until(h, [&] { return h.backend.reports->last_subtitle.load() == 4; }));

  // Same choice: nothing changes. Back in a menu: cancels.
  h.press(InputEvent::Menu);
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);
  h.press(InputEvent::Ok);
  CHECK_EQ(h.backend.reports->set_tracks_calls.load(), 3);
  h.press(InputEvent::Menu);
  h.press(InputEvent::Back);
  CHECK(!h.app->menu_open());
  h.press(InputEvent::Back);  // playback stop
  CHECK(run_until(h, [&] { return h.screen() == "detail"; }));
}

// Text subtitles rendered by the client: downloaded once per track outside
// the display loop, instant change (no stream reopening).
static void test_client_subtitles() {
  Harness h;
  h.backend.media_path = g_media_long;
  api::MediaSourceInfo src;
  src.id = "src-st";
  src.streams.push_back(track_stream(1, api::StreamKind::Audio, "eng", "eac3"));
  src.streams.push_back(track_stream(3, api::StreamKind::Subtitle, "fre", "subrip", true));
  src.streams.push_back(track_stream(4, api::StreamKind::Subtitle, "eng", "subrip", true));
  src.streams.push_back(track_stream(5, api::StreamKind::Subtitle, "ger", "subrip", true));
  src.server_defaults = true;
  src.default_audio_index = 1;
  src.default_subtitle_index = 3;
  h.backend.movies[3].media_sources.push_back(src);
  h.backend.failing_subtitle = 5;
  open_detail(h);
  h.press(InputEvent::Right);
  h.press(InputEvent::Ok);
  h.app->update(h.now = real_ms());
  ui::PlayerScreen* ps = player_screen(h);
  CHECK(ps != nullptr);
  if (!ps) return;
  CHECK(run_until(h, [&] { return ps->session()->phase() == ui::PlaybackSession::Phase::Playing; }));
  // Default track (client): downloaded, nothing is requested from the stream.
  CHECK_EQ(ps->wanted_subtitle(), 3);
  CHECK(run_until(h, [&] { return ps->subtitles_loaded(3); }));
  CHECK_EQ(h.backend.subtitle_calls.load(), 1);
  CHECK(h.backend.last_subtitle_request == std::string(h.backend.movies[3].id) + "/src-st/3");
  CHECK(ps->subtitle_lines(500).empty());
  CHECK(ps->subtitle_lines(2000) == std::vector<std::string>({"Bonjour"}));
  CHECK(ps->subtitle_lines(4000) == std::vector<std::string>({"Deux", "lignes"}));
  CHECK(ps->subtitle_lines(5000).empty());

  // Other text track: downloaded in turn, without reopening the stream.
  h.press(InputEvent::Menu);
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);
  h.press(InputEvent::Down);  // anglais (index 4)
  h.press(InputEvent::Ok);
  CHECK_EQ(ps->wanted_subtitle(), 4);
  CHECK(!ps->session()->player().seek_in_flight());
  CHECK(!ps->track_change_pending());
  CHECK(run_until(h, [&] { return ps->subtitles_loaded(4); }));
  CHECK_EQ(h.backend.subtitle_calls.load(), 2);
  CHECK(!ps->subtitles_loading());

  // "None": nothing on screen anymore; back to an already loaded track: no new download.
  h.press(InputEvent::Menu);
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);
  h.press(InputEvent::Up, 5);
  h.press(InputEvent::Ok);
  CHECK_EQ(ps->wanted_subtitle(), -1);
  CHECK(ps->subtitle_lines(2000).empty());
  h.press(InputEvent::Menu);
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);  // French, already in memory
  CHECK_EQ(ps->wanted_subtitle(), 3);
  CHECK(ps->subtitle_lines(2000) == std::vector<std::string>({"Bonjour"}));
  CHECK_EQ(h.backend.subtitle_calls.load(), 2);

  // Download failure: message, playback continues without subtitles.
  h.press(InputEvent::Menu);
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);
  h.press(InputEvent::Down, 3);  // allemand (index 5)
  h.press(InputEvent::Ok);
  CHECK(run_until(h, [&] { return h.backend.subtitle_calls.load() == 3 && !ps->subtitles_loading(); }));
  CHECK(!ps->subtitles_loaded(5));
  CHECK(h.app->toast_text() == "Subtitles unavailable");
  CHECK(ps->subtitle_lines(2000).empty());
  CHECK(ps->session()->player().state() == player::PlayerState::Playing);
  h.press(InputEvent::Back);
  CHECK(run_until(h, [&] { return h.screen() == "detail"; }));
}

// Media without a track choice: Options opens nothing, a message says so.
static void test_player_options_without_tracks() {
  Harness h;
  h.backend.media_path = g_media_long;
  open_detail(h);
  h.press(InputEvent::Right);
  h.press(InputEvent::Ok);
  h.app->update(h.now = real_ms());
  ui::PlayerScreen* ps = player_screen(h);
  CHECK(ps != nullptr);
  if (!ps) return;
  CHECK(run_until(h, [&] { return ps->session()->phase() == ui::PlaybackSession::Phase::Playing; }));
  h.press(InputEvent::Menu);
  CHECK(!h.app->menu_open());
  CHECK(!h.app->toast_text().empty());
  h.press(InputEvent::Back);
  CHECK(run_until(h, [&] { return h.screen() == "detail"; }));
}

int main() {
  char dir[] = "/tmp/pelagia-uiplayer-XXXXXX";
  if (!mkdtemp(dir)) return 1;
  g_media_long = std::string(dir) + "/long.mp4";
  g_media_short = std::string(dir) + "/short.mp4";
  testmedia::SynthSpec spec;
  spec.seconds = 20.0;
  CHECK(testmedia::synth_media(g_media_long.c_str(), spec));
  spec.seconds = 1.0;
  CHECK(testmedia::synth_media(g_media_short.c_str(), spec));

  test_play_pause_seek_stop();
  test_resume_position();
  test_end_of_media();
  test_open_failure();
  test_cancel_while_opening();
  test_quit_while_playing();
  test_destroy_while_stopping();
  test_track_menu_in_player();
  test_client_subtitles();
  test_player_options_without_tracks();

  const std::string cleanup = std::string("rm -rf ") + dir;
  if (std::system(cleanup.c_str()) != 0) return 1;
  return testfw::test_failures();
}
