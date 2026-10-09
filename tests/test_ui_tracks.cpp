// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Track choice on the page, without rendering: preselection from the profile,
// audio / subtitle lists ("None", checked, burned in), gamepad
// navigation, tracks passed to playback, scrolling menu.

#include <string>
#include <vector>

#include "test_framework.h"
#include "ui/app.h"
#include "ui/screens/detail_screen.h"
#include "ui/screens/screens.h"
#include "ui/track_menu.h"
#include "ui_harness.h"

namespace {

using namespace uitest;

api::MediaStreamInfo audio(int index, const char* lang, const char* codec, int channels,
                           bool is_default = false) {
  api::MediaStreamInfo s;
  s.index = index;
  s.kind = api::StreamKind::Audio;
  s.language = lang;
  s.codec = codec;
  s.channels = channels;
  s.is_default = is_default;
  return s;
}

api::MediaStreamInfo sub(int index, const char* lang, const char* codec, bool text,
                         bool forced = false) {
  api::MediaStreamInfo s;
  s.index = index;
  s.kind = api::StreamKind::Subtitle;
  s.language = lang;
  s.codec = codec;
  s.is_text = text;
  s.supports_external = text;
  s.is_forced = forced;
  return s;
}

// Subbed movie: English (default) and French audio; French SRT subtitles,
// forced, English, and one PGS. The server announces audio 1, subtitles 6 (forced).
api::MediaSourceInfo vostfr_source() {
  api::MediaSourceInfo src;
  src.id = "src-vo";
  src.streams.push_back(audio(1, "eng", "eac3", 6, true));
  src.streams.push_back(audio(2, "fre", "eac3", 6));
  src.streams.push_back(sub(3, "fre", "subrip", true));
  src.streams.push_back(sub(4, "eng", "subrip", true));
  src.streams.push_back(sub(5, "fre", "PGSSUB", false));
  src.streams.push_back(sub(6, "fre", "subrip", true, true));
  src.server_defaults = true;
  src.default_audio_index = 1;
  src.default_subtitle_index = 6;
  return src;
}

// Page opened on the first movie of the in-memory backend.
ui::DetailScreen* open_detail(Harness& h, const api::MediaSourceInfo* source) {
  h.backend.movies[0].media_sources.clear();
  if (source) h.backend.movies[0].media_sources.push_back(*source);
  save_session(h, "good");
  h.app->start("");
  h.settle();
  h.app->push(ui::make_detail_screen(h.backend.movies[0]));
  h.settle();
  return h.top<ui::DetailScreen>();
}

}  // namespace

static void test_choices_and_labels() {
  const api::MediaSourceInfo src = vostfr_source();
  api::TrackSelection sel = api::default_selection(src, api::UserPreferences());
  CHECK_EQ(sel.audio_index, 1);
  CHECK_EQ(sel.subtitle_index, 6);

  std::vector<ui::TrackChoice> audio_list = ui::audio_choices(src, sel);
  CHECK_EQ(audio_list.size(), 2u);
  CHECK(audio_list[0].label == "English · E-AC3 5.1" && audio_list[0].selected);
  CHECK(audio_list[1].label == "French · E-AC3 5.1" && !audio_list[1].selected);

  std::vector<ui::TrackChoice> subs = ui::subtitle_choices(src, sel);
  CHECK_EQ(subs.size(), 5u);  // None + 4 tracks
  CHECK(subs[0].label == "None" && subs[0].index == -1 && !subs[0].selected);
  CHECK(subs[1].label == "French · SRT");
  CHECK(subs[3].label == "French · PGS · burned in");  // image: burned in by the server
  CHECK(subs[4].label == "French · SRT · forced" && subs[4].selected);
  CHECK(ui::audio_summary(src, sel) == "English · E-AC3 5.1");
  CHECK(ui::subtitle_summary(src, sel) == "French · SRT · forced");

  sel = api::with_subtitle(src, sel, -1);
  CHECK(ui::subtitle_summary(src, sel) == "None");
  CHECK(ui::subtitle_choices(src, sel)[0].selected);
  sel = api::with_subtitle(src, sel, 5);
  CHECK(ui::subtitle_summary(src, sel) == "French · PGS · burned in");

  api::MediaSourceInfo empty;
  CHECK(!ui::has_track_choices(empty));
  CHECK(ui::has_track_choices(src));
}

static void test_detail_preselection_and_menus() {
  Harness h;
  const api::MediaSourceInfo src = vostfr_source();
  ui::DetailScreen* detail = open_detail(h, &src);
  CHECK(detail->has_track_rows());
  CHECK_EQ(detail->row(), ui::DetailScreen::kButtons);
  CHECK_EQ(detail->tracks().audio_index, 1);
  CHECK_EQ(detail->tracks().subtitle_index, 6);
  CHECK(detail->audio_text() == "English · E-AC3 5.1");
  CHECK(detail->subtitle_text() == "French · SRT · forced");

  // Down: audio line; cross: list, current track selected and checked.
  h.press(InputEvent::Down);
  CHECK_EQ(detail->row(), ui::DetailScreen::kAudioRow);
  h.press(InputEvent::Ok);
  CHECK(h.app->menu_open());
  CHECK_EQ(h.app->menu_items().size(), 2u);
  CHECK_EQ(h.app->menu_index(), 0);
  CHECK(h.app->menu_items()[0].checked && !h.app->menu_items()[1].checked);
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);  // French
  CHECK(!h.app->menu_open());
  CHECK_EQ(detail->tracks().audio_index, 2);
  CHECK(detail->audio_text() == "French · E-AC3 5.1");
  CHECK_EQ(detail->tracks().subtitle_index, 6);  // Default mode: subtitles kept

  // Back cancels without changing anything.
  h.press(InputEvent::Ok);
  CHECK_EQ(h.app->menu_index(), 1);  // opened on the current track
  h.press(InputEvent::Back);
  CHECK(!h.app->menu_open());
  CHECK_EQ(detail->tracks().audio_index, 2);

  // Subtitles: "None" first; choice of the PGS (burned in).
  h.press(InputEvent::Down);
  CHECK_EQ(detail->row(), ui::DetailScreen::kSubtitleRow);
  h.press(InputEvent::Down);  // last line: stays
  CHECK_EQ(detail->row(), ui::DetailScreen::kSubtitleRow);
  h.press(InputEvent::Ok);
  CHECK_EQ(h.app->menu_items().size(), 5u);
  CHECK(h.app->menu_items()[0].label == "None");
  CHECK_EQ(h.app->menu_index(), 4);  // on the current track (forced)
  h.press(InputEvent::Up);
  h.press(InputEvent::Ok);  // PGS
  CHECK_EQ(detail->tracks().subtitle_index, 5);
  CHECK(detail->tracks().subtitle_delivery == api::SubtitleDelivery::Encode);
  CHECK(detail->subtitle_text() == "French · PGS · burned in");
  h.press(InputEvent::Ok);
  h.press(InputEvent::Up, 4);
  CHECK_EQ(h.app->menu_index(), 0);
  h.press(InputEvent::Ok);  // Aucun
  CHECK_EQ(detail->tracks().subtitle_index, -1);
  CHECK(detail->subtitle_text() == "None");

  // Playback from the subtitle line (triangle) then from the buttons (cross):
  // the chosen tracks go with the playback.
  h.press(InputEvent::PlayPause);
  h.settle();
  CHECK_EQ(h.backend.play_calls, 1);
  CHECK_EQ(h.backend.last_play_tracks.audio_index, 2);
  CHECK_EQ(h.backend.last_play_tracks.subtitle_index, -1);
  CHECK(h.backend.last_play_tracks.media_source_id == "src-vo");
  CHECK(h.screen() == "player");
}

static void test_smart_mode_recomputes_subtitles() {
  Harness h;
  h.backend.prefs.subtitle_mode = api::SubtitleMode::Smart;
  h.backend.prefs.subtitle_language = "fre";
  api::MediaSourceInfo src = vostfr_source();
  src.default_subtitle_index = 3;  // Subbed: English audio -> full French subtitles
  ui::DetailScreen* detail = open_detail(h, &src);
  CHECK_EQ(detail->tracks().subtitle_index, 3);
  // French (dubbed) audio: no more full subtitles, the forced ones remain.
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);
  h.press(InputEvent::Down);
  h.press(InputEvent::Ok);
  CHECK_EQ(detail->tracks().audio_index, 2);
  CHECK_EQ(detail->tracks().subtitle_index, 6);
}

static void test_no_tracks_no_rows() {
  Harness h;
  ui::DetailScreen* detail = open_detail(h, nullptr);
  CHECK(!detail->has_track_rows());
  h.press(InputEvent::Down);
  CHECK_EQ(detail->row(), ui::DetailScreen::kButtons);
  h.press(InputEvent::Ok);
  h.settle();
  CHECK_EQ(h.backend.play_calls, 1);
  CHECK(h.backend.last_play_tracks == api::TrackSelection());  // choice left to the server
}

static void test_long_menu_scrolls() {
  Harness h;
  api::MediaSourceInfo src;
  src.id = "long";
  src.streams.push_back(audio(1, "eng", "aac", 2, true));
  for (int i = 0; i < 12; ++i) src.streams.push_back(sub(10 + i, "fre", "subrip", true));
  src.server_defaults = true;
  src.default_audio_index = 1;
  ui::DetailScreen* detail = open_detail(h, &src);
  h.press(InputEvent::Down, 2);
  CHECK_EQ(detail->row(), ui::DetailScreen::kSubtitleRow);
  h.press(InputEvent::Ok);
  CHECK_EQ(h.app->menu_items().size(), 13u);  // Aucun + 12
  CHECK_EQ(h.app->menu_scroll(), 0);
  h.press(InputEvent::Down, 7);
  CHECK_EQ(h.app->menu_index(), 7);
  CHECK_EQ(h.app->menu_scroll(), 0);
  h.press(InputEvent::Down, 3);
  CHECK_EQ(h.app->menu_index(), 10);
  CHECK_EQ(h.app->menu_scroll(), 3);  // the window follows the cursor
  h.press(InputEvent::Down, 10);
  CHECK_EQ(h.app->menu_index(), 12);
  CHECK_EQ(h.app->menu_scroll(), 5);
  h.press(InputEvent::Up, 12);
  CHECK_EQ(h.app->menu_index(), 0);
  CHECK_EQ(h.app->menu_scroll(), 0);
  // Current track far down the list: menu opened on it, visible.
  h.press(InputEvent::Down, 11);
  h.press(InputEvent::Ok);
  CHECK_EQ(detail->tracks().subtitle_index, 20);
  h.press(InputEvent::Ok);
  CHECK_EQ(h.app->menu_index(), 11);
  CHECK(h.app->menu_scroll() > 0 && h.app->menu_scroll() <= 11 &&
        h.app->menu_index() < h.app->menu_scroll() + ui::App::kMenuVisible);
}

int main() {
  test_choices_and_labels();
  test_detail_preselection_and_menus();
  test_smart_mode_recomputes_subtitles();
  test_no_tracks_no_rows();
  test_long_menu_scrolls();
  return testfw::test_failures();
}
