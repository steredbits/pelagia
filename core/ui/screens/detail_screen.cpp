// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "ui/screens/detail_screen.h"

#include <cstdio>

#include "ui/app.h"
#include "ui/format.h"
#include "ui/theme.h"
#include "ui/track_menu.h"
#include "util/text.h"
#include "util/i18n.h"

namespace ui {

namespace {

using platform::InputEvent;

constexpr int kPosterX = theme::kMarginX;
constexpr int kPosterY = 120;
constexpr int kPosterW = 400;
constexpr int kPosterH = 600;
constexpr int kInfoX = kPosterX + kPosterW + 64;
constexpr int kInfoW = theme::kScreenW - theme::kMarginX - kInfoX;

std::string series_line(const api::MediaItem& item) {
  const std::string code = episode_code(item);
  const std::string series = util::clean_display_text(item.series_name);
  return code.empty() ? series : series + "  ·  " + code;
}

}  // namespace

void DetailScreen::load(App& app) {
  loading_ = true;
  error_.clear();
  loaded_version_ = app.data_version();
  const std::string id = item_.id;
  app.run<ItemResult>(
      lifetime(), [id](Backend& b, ItemResult& r) { r.result = b.item(id, &r.item); },
      [this, &app](ItemResult& r) {
        loading_ = false;
        if (!r.result.ok()) {
          // The page stays usable with the data from the list.
          error_ = describe_error(r.result);
          return;
        }
        item_ = r.item;
        refreshed_ = true;
        focus_ = 0;  // "Resume" first if it exists
        row_ = kButtons;
        choose_default_tracks(app);
      });
}

const api::MediaSourceInfo* DetailScreen::source() const {
  return api::find_source(item_, tracks_.media_source_id);
}

bool DetailScreen::has_track_rows() const {
  const api::MediaSourceInfo* src = source();
  return src && is_playable(item_) && has_track_choices(*src);
}

// Profile tracks: those the server announces for this user
// (language preferences, subtitle mode, remembered choices).
void DetailScreen::choose_default_tracks(App& app) {
  tracks_ = api::TrackSelection();
  if (!item_.media_sources.empty()) {
    tracks_ = api::default_selection(item_.media_sources[0], app.backend().preferences());
  }
}

std::string DetailScreen::audio_text() const {
  const api::MediaSourceInfo* src = source();
  return src ? audio_summary(*src, tracks_) : std::string();
}

std::string DetailScreen::subtitle_text() const {
  const api::MediaSourceInfo* src = source();
  return src ? subtitle_summary(*src, tracks_) : std::string();
}

void DetailScreen::open_audio_menu(App& app) {
  const api::MediaSourceInfo* src = source();
  if (!src) return;
  int initial = 0;
  const api::UserPreferences prefs = app.backend().preferences();
  std::vector<MenuItem> items = track_menu_items(
      audio_choices(*src, tracks_),
      [this, prefs](App&, int index) {
        if (const api::MediaSourceInfo* s = source()) {
          tracks_ = api::with_audio(*s, prefs, tracks_, index);
        }
      },
      &initial);
  app.open_menu(util::tr(util::Str::Audio), std::move(items), initial);
}

void DetailScreen::open_subtitle_menu(App& app) {
  const api::MediaSourceInfo* src = source();
  if (!src) return;
  int initial = 0;
  std::vector<MenuItem> items = track_menu_items(
      subtitle_choices(*src, tracks_),
      [this](App&, int index) {
        if (const api::MediaSourceInfo* s = source()) {
          tracks_ = api::with_subtitle(*s, tracks_, index);
        }
      },
      &initial);
  app.open_menu(util::tr(util::Str::Subtitles), std::move(items), initial);
}

void DetailScreen::update(App& app) {
  if (app.top() == this && !loading_ && app.data_version() != loaded_version_ &&
      !app.playback_stopping()) {
    load(app);
  }
}

std::vector<std::string> DetailScreen::buttons() const {
  if (!is_playable(item_)) {
    return {};
  }
  const int64_t resume = resume_position_ms(item_);
  if (resume > 0) {
    return {util::trf(util::Str::ResumeAt, format_clock(resume).c_str()), util::tr(util::Str::PlayFromStart)};
  }
  return {util::tr(util::Str::Play)};
}

int64_t DetailScreen::start_for(int button) const {
  return button == 0 ? resume_position_ms(item_) : 0;
}

void DetailScreen::handle(App& app, const platform::Input& in) {
  const int count = static_cast<int>(buttons().size());
  const int last_row = has_track_rows() ? kSubtitleRow : kButtons;
  switch (in.event) {
    case InputEvent::Up:
      if (row_ > kButtons) --row_;
      break;
    case InputEvent::Down:
      if (row_ < last_row) ++row_;
      break;
    case InputEvent::Left:
      if (row_ == kButtons && focus_ > 0) --focus_;
      break;
    case InputEvent::Right:
      if (row_ == kButtons && focus_ + 1 < count) ++focus_;
      break;
    case InputEvent::Ok:
    case InputEvent::PlayPause:
      if (in.event == InputEvent::Ok && row_ == kAudioRow && last_row >= kAudioRow) {
        open_audio_menu(app);
      } else if (in.event == InputEvent::Ok && row_ == kSubtitleRow && last_row >= kSubtitleRow) {
        open_subtitle_menu(app);
      } else if (count > 0 && !app.playback_stopping()) {
        app.play(item_, start_for(row_ == kButtons && focus_ < count ? focus_ : 0), tracks_);
      }
      break;
    case InputEvent::Back:
      app.pop();
      break;
    case InputEvent::Menu:
      app.open_menu(util::tr(util::Str::Options), {MenuItem{util::tr(util::Str::BackToHome), [](App& a) { a.pop_to_root(); }}});
      break;
    default:
      break;
  }
}

void DetailScreen::draw(App& app, Painter& p) {
  const Rect poster = rect(kPosterX, kPosterY, kPosterW, kPosterH);
  if (!p.poster_image(poster_key_for(item_, theme::kDetailPosterW), poster)) {
    p.fill(poster, theme::surface());
  }
  int y = kPosterY;
  if (item_.type == "Episode") {
    p.label_in(series_line(item_), rect(kInfoX, y, kInfoW, 46), Weight::Regular,
               theme::kHeading, theme::text_dim());
    y += 52;
  }
  y += p.paragraph(display_title(item_), rect(kInfoX, y, kInfoW, 160), Weight::Bold,
                   theme::kTitle, theme::text(), 2);
  // Information line: year, duration, state.
  std::string meta;
  auto add = [&meta](const std::string& s) {
    if (s.empty()) return;
    if (!meta.empty()) meta += "   ·   ";
    meta += s;
  };
  if (item_.production_year > 0) add(std::to_string(item_.production_year));
  if (item_.runtime_ticks > 0) add(format_runtime(item_.runtime_ticks / kTicksPerMs));
  if (item_.played) add(util::tr(util::Str::Watched));
  p.label(meta, kInfoX, y + 8, Weight::Regular, theme::kBody, theme::text_dim());
  y += 60;
  const double done = progress_fraction(item_);
  if (done > 0) {
    p.progress(rect(kInfoX, y + 6, 520, 10), done, theme::accent(), theme::surface());
    char pct[32];
    std::snprintf(pct, sizeof(pct), "%d %%", static_cast<int>(done * 100 + 0.5));
    p.label(pct, kInfoX + 540, y - 8, Weight::Regular, theme::kSmall, theme::text_dim());
    y += 34;
  }
  // Boutons.
  const std::vector<std::string> labels = buttons();
  int x = kInfoX;
  y += 20;
  for (size_t i = 0; i < labels.size(); ++i) {
    const bool icon = i == 0;
    const int w = p.text().measure(labels[i], Weight::Bold, theme::kBody) + 96 + (icon ? 40 : 0);
    p.button(rect(x, y, w, 76), labels[i], row_ == kButtons && static_cast<int>(i) == focus_,
             icon);
    x += w + theme::kGap;
  }
  const bool rows = has_track_rows();
  if (rows) {
    draw_track_rows(p, y + 100);
    y += 2 * 72;
  }
  if (labels.empty()) {
    p.label(util::tr(util::Str::NoPlayableSource), kInfoX, y + 20, Weight::Regular,
            theme::kBody, theme::danger());
  }
  y += 120;
  if (app.playback_stopping() || loading_) {
    p.spinner(kInfoX + 20, y - 20, 14, theme::text_dim());
    p.label(app.playback_stopping() ? util::tr(util::Str::SavingPosition) : util::tr(util::Str::Updating),
            kInfoX + 50, y - 38, Weight::Regular, theme::kSmall, theme::text_dim());
  } else if (!error_.empty()) {
    p.label(error_, kInfoX, y - 38, Weight::Regular, theme::kSmall, theme::danger());
  }
  p.paragraph(util::clean_display_text(item_.overview), rect(kInfoX, y + 10, kInfoW, 300), Weight::Regular,
              theme::kBody, theme::text(), rows ? 4 : 7);
  p.hints(rows ? util::tr(util::Str::DetailHintTracks)
               : util::tr(util::Str::DetailHint));
}

// Two lines "Audio" and "Subtitles": current value, frame on the selected line.
void DetailScreen::draw_track_rows(Painter& p, int y) {
  const char* names[] = {util::tr(util::Str::Audio), util::tr(util::Str::Subtitles)};
  const std::string values[] = {audio_text(), subtitle_text()};
  for (int i = 0; i < 2; ++i) {
    const bool focused = row_ == i + 1;
    const int ry = y + i * 72;
    p.label_in(names[i], rect(kInfoX, ry, 210, 60), Weight::Regular, theme::kBody,
               theme::text_dim());
    const Rect box = rect(kInfoX + 220, ry, kInfoW - 220, 60);
    p.fill(box, focused ? theme::text() : theme::surface());
    p.label_in(values[i], rect(box.x + 24, box.y, box.w - 48, box.h), Weight::Bold, theme::kBody,
               focused ? theme::background() : theme::text());
  }
}

}  // namespace ui
