// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "ui/screens/series_screen.h"

#include "ui/app.h"
#include "ui/format.h"
#include "ui/theme.h"
#include "util/text.h"
#include "util/i18n.h"

namespace ui {

namespace {

using platform::InputEvent;

constexpr int kListTop = 200;
constexpr int kListBottom = theme::kScreenH - theme::kMarginY - 40;
constexpr int kSeasonW = 420;
constexpr int kSeasonH = 76;
constexpr int kEpisodeX = theme::kMarginX + kSeasonW + 48;
constexpr int kEpisodeW = theme::kScreenW - theme::kMarginX - kEpisodeX;
constexpr int kEpisodeH = 124;

}  // namespace

void SeriesScreen::enter(App& app) { load_seasons(app); }

void SeriesScreen::load_seasons(App& app) {
  loading_seasons_ = true;
  error_.clear();
  const std::string id = series_.id;
  app.run<ItemsResult>(
      lifetime(), [id](Backend& b, ItemsResult& r) { r.result = b.seasons(id, &r.list); },
      [this, &app](ItemsResult& r) {
        loading_seasons_ = false;
        if (!r.result.ok()) {
          error_ = describe_error(r.result);
          return;
        }
        seasons_ = r.list.items;
        season_focus_.set_count(static_cast<int>(seasons_.size()));
        if (!seasons_.empty()) load_episodes(app, season_focus_.index());
      });
}

void SeriesScreen::load_episodes(App& app, int season) {
  if (season < 0 || season >= static_cast<int>(seasons_.size())) {
    return;
  }
  loading_episodes_ = true;
  requested_season_ = season;
  loaded_version_ = app.data_version();
  const std::string series_id = series_.id;
  const std::string season_id = seasons_[season].id;
  app.run<ItemsResult>(
      lifetime(),
      [series_id, season_id](Backend& b, ItemsResult& r) {
        r.result = b.episodes(series_id, season_id, &r.list);
      },
      [this, season](ItemsResult& r) {
        if (season != requested_season_) {
          return;  // season left in the meantime: stale result
        }
        loading_episodes_ = false;
        if (!r.result.ok()) {
          error_ = describe_error(r.result);
          return;
        }
        if (season != shown_season_) episode_focus_.set_index(0);
        shown_season_ = season;
        episodes_ = r.list.items;
        episode_focus_.set_count(static_cast<int>(episodes_.size()));
      });
}

void SeriesScreen::update(App& app) {
  if (app.top() == this && shown_season_ >= 0 && !loading_episodes_ &&
      app.data_version() != loaded_version_ && !app.playback_stopping()) {
    load_episodes(app, shown_season_);  // progress and "Watched" up to date
  }
}

void SeriesScreen::handle(App& app, const platform::Input& in) {
  switch (in.event) {
    case InputEvent::Up:
    case InputEvent::Down: {
      const Dir d = in.event == InputEvent::Up ? Dir::Up : Dir::Down;
      if (column_ == 0) {
        if (season_focus_.move(d)) load_episodes(app, season_focus_.index());
      } else {
        episode_focus_.move(d);
      }
      break;
    }
    case InputEvent::Right:
      if (column_ == 0 && !episodes_.empty() && shown_season_ == season_focus_.index()) {
        column_ = 1;
      }
      break;
    case InputEvent::Left:
      column_ = 0;
      break;
    case InputEvent::Ok:
      if (!error_.empty() && seasons_.empty()) {
        load_seasons(app);
      } else if (column_ == 0) {
        if (!episodes_.empty() && shown_season_ == season_focus_.index()) column_ = 1;
      } else if (episode_focus_.index() < static_cast<int>(episodes_.size())) {
        app.push(make_detail_screen(episodes_[episode_focus_.index()]));
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

void SeriesScreen::draw(App& app, Painter& p) {
  (void)app;
  std::string header = display_title(series_);
  p.label_in(header, rect(theme::kMarginX, theme::kMarginY - 10, theme::kContentW, 80),
             Weight::Bold, theme::kTitle, theme::text());
  if (series_.production_year > 0) {
    p.label(std::to_string(series_.production_year), theme::kMarginX, 126, Weight::Regular,
            theme::kBody, theme::text_dim());
  }
  const Rect content = rect(0, kListTop, theme::kScreenW, kListBottom - kListTop);
  if (seasons_.empty()) {
    if (!error_.empty()) {
      p.status(content, util::trf(util::Str::ErrorRetry, error_.c_str()), false, true);
    } else if (loading_seasons_) {
      p.status(content, util::tr(util::Str::LoadingSeasons), true, false);
    } else {
      p.status(content, util::tr(util::Str::NoSeason), false, false);
    }
    return;
  }
  // Saisons.
  const int season_visible = (kListBottom - kListTop) / (kSeasonH + 12);
  const int s_top = season_focus_.scroll_top(season_visible);
  for (int i = s_top; i < static_cast<int>(seasons_.size()) && i < s_top + season_visible; ++i) {
    const Rect r = rect(theme::kMarginX, kListTop + (i - s_top) * (kSeasonH + 12), kSeasonW,
                        kSeasonH);
    const bool selected = i == season_focus_.index();
    const bool focused = selected && column_ == 0;
    p.fill(r, focused ? theme::text() : (selected ? theme::surface_focus() : theme::surface()));
    p.label_in(display_title(seasons_[i]), rect(r.x + 24, r.y, r.w - 48, r.h), Weight::Bold,
               theme::kBody, focused ? theme::background() : theme::text());
  }
  // Episodes of the selected season.
  const Rect list = rect(kEpisodeX, kListTop, kEpisodeW, kListBottom - kListTop);
  if (shown_season_ != season_focus_.index() || (loading_episodes_ && episodes_.empty())) {
    p.status(list, util::tr(util::Str::LoadingEpisodes), true, false);
  } else if (episodes_.empty()) {
    p.status(list, util::tr(util::Str::NoEpisode), false, false);
  } else {
    const int visible = (kListBottom - kListTop) / (kEpisodeH + 12);
    const int top = episode_focus_.scroll_top(visible);
    for (int i = top; i < static_cast<int>(episodes_.size()) && i < top + visible; ++i) {
      const api::MediaItem& ep = episodes_[i];
      const Rect r = rect(kEpisodeX, kListTop + (i - top) * (kEpisodeH + 12), kEpisodeW, kEpisodeH);
      const bool f = column_ == 1 && i == episode_focus_.index();
      p.fill(r, f ? theme::surface_focus() : theme::surface());
      if (f) p.border(r, 4, theme::focus_ring());
      std::string title = display_title(ep);
      if (ep.index_number >= 0) title = std::to_string(ep.index_number) + ". " + title;
      p.label_in(title, rect(r.x + 28, r.y + 14, r.w - 200, 46), Weight::Bold, theme::kBody,
                 theme::text());
      std::string meta = episode_code(ep);
      if (ep.runtime_ticks > 0) meta += "  ·  " + format_runtime(ep.runtime_ticks / kTicksPerMs);
      const int64_t resume = resume_position_ms(ep);
      if (resume > 0) meta += "  ·  " + util::trf(util::Str::ResumeAtMeta, format_clock(resume).c_str());
      p.label(meta, r.x + 28, r.y + 66, Weight::Regular, theme::kSmall, theme::text_dim());
      if (ep.played) {
        const Rect badge = rect(r.x + r.w - 96, r.y + 40, 68, 40);
        p.fill(badge, theme::accent());
        p.label_in(util::tr(util::Str::Watched), badge, Weight::Bold, theme::kSmall, theme::text(), Align::Center);
      }
      const double done = progress_fraction(ep);
      if (done > 0) {
        p.progress(rect(r.x, r.y + r.h - 8, r.w, 8), done, theme::accent(), rgba(0, 0, 0, 120));
      }
    }
  }
  p.hints(column_ == 0 ? util::tr(util::Str::SeriesHintSeasons)
                       : util::tr(util::Str::SeriesHintEpisodes));
}

}  // namespace ui
