// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "ui/screens/home_screen.h"

#include "ui/app.h"
#include "ui/format.h"
#include "ui/theme.h"
#include "util/version.h"
#include "util/i18n.h"

namespace ui {

namespace {

using platform::InputEvent;

constexpr int kContentTop = 150;
constexpr int kContentBottom = theme::kScreenH - theme::kMarginY - 40;
constexpr int kRowTitleH = 54;
constexpr int kCaptionH = 76;
constexpr int kRowGap = 34;
constexpr int kTileW = 380;
constexpr int kTileH = 170;

struct LibraryLatest {
  api::ApiResult result;
  api::ItemList list;
};

}  // namespace

void HomeScreen::load(App& app, bool show_spinner) {
  if (show_spinner) {
    libraries_done_ = resume_done_ = false;
    libraries_.clear();
    resume_items_.clear();
    latest_.clear();
    latest_done_.clear();
    rows_.clear();
    focus_settled_ = false;
    focus_.set_rows({});
  }
  error_.clear();
  loaded_version_ = app.data_version();
  pending_ += 2;
  app.run<ItemsResult>(
      lifetime(), [](Backend& b, ItemsResult& r) { r.result = b.resume(&r.list); },
      [this](ItemsResult& r) {
        --pending_;
        resume_done_ = true;
        // Non-blocking failure: the home screen stays usable without this row.
        resume_items_ = r.result.ok() ? r.list.items : std::vector<api::MediaItem>();
        rebuild_rows();
      });
  app.run<LibrariesResult>(
      lifetime(), [](Backend& b, LibrariesResult& r) { r.result = b.libraries(&r.libraries); },
      [this, &app](LibrariesResult& r) {
        --pending_;
        if (!r.result.ok()) {
          error_ = describe_error(r.result);
          return;
        }
        libraries_done_ = true;
        libraries_ = r.libraries;
        if (latest_.size() != libraries_.size()) {
          // First load (or libraries changed); otherwise the current rows
          // stay displayed until the new ones arrive.
          latest_.assign(libraries_.size(), std::vector<api::MediaItem>());
          latest_done_.assign(libraries_.size(), false);
        }
        for (size_t i = 0; i < libraries_.size(); ++i) {
          const api::Library lib = libraries_[i];
          ++pending_;
          app.run<LibraryLatest>(
              lifetime(), [lib](Backend& b, LibraryLatest& l) { l.result = b.latest(lib, &l.list); },
              [this, i](LibraryLatest& l) {
                --pending_;
                if (i < latest_.size() && l.result.ok()) {
                  latest_[i] = l.list.items;
                  latest_done_[i] = true;
                  rebuild_rows();
                }
              });
        }
        rebuild_rows();
      });
}

void HomeScreen::rebuild_rows() {
  // Row and focus position tracked by their kind: they stay
  // in place when a row arrives or disappears during loading.
  RowKind kind = RowKind::Resume;
  int library = -1;
  const int index = focus_.index();
  std::string item_id;  // the selected element may change place
  const bool had_focus = !rows_.empty();
  if (had_focus) {
    const Row& row = rows_[focus_.row()];
    kind = row.kind;
    library = row.library;
    if (index < static_cast<int>(row.items.size())) item_id = row.items[index].id;
  }
  rows_.clear();
  if (!resume_items_.empty()) {
    rows_.push_back(Row{RowKind::Resume, util::tr(util::Str::ContinueWatching), -1, resume_items_});
  }
  if (!libraries_.empty()) {
    rows_.push_back(Row{RowKind::Libraries, util::tr(util::Str::Libraries), -1, {}});
  }
  for (size_t i = 0; i < latest_.size(); ++i) {
    if (latest_done_[i] && !latest_[i].empty()) {
      rows_.push_back(Row{RowKind::Latest, util::trf(util::Str::LatestIn, libraries_[i].name.c_str()),
                          static_cast<int>(i), latest_[i]});
    }
  }
  std::vector<int> counts;
  for (const Row& r : rows_) {
    counts.push_back(r.kind == RowKind::Libraries ? static_cast<int>(libraries_.size())
                                                  : static_cast<int>(r.items.size()));
  }
  focus_.set_rows(counts);
  if (!focus_settled_) {
    // First display: the rows arrive in any order
    // ("Continue watching" may arrive after "Libraries"); the focus starts from
    // the first row once everything is received.
    focus_.set_position(0, 0);
    focus_settled_ = loaded();
  } else if (had_focus) {
    for (size_t r = 0; r < rows_.size(); ++r) {
      if (rows_[r].kind == kind && rows_[r].library == library) {
        int target = index;
        for (size_t i = 0; i < rows_[r].items.size(); ++i) {
          if (!item_id.empty() && rows_[r].items[i].id == item_id) target = static_cast<int>(i);
        }
        focus_.set_position(static_cast<int>(r), target);
        break;
      }
    }
  }
}

void HomeScreen::resume(App& app) {
  if (app.data_version() != loaded_version_ && !app.playback_stopping()) {
    load(app, false);
  }
}

void HomeScreen::update(App& app) {
  // Language changed in the Options menu: the row titles are texts kept by the screen.
  if (language_version_ != app.language_version()) {
    language_version_ = app.language_version();
    rebuild_rows();
  }
  // Stop of a playback finished while another screen was displayed.
  if (app.top() == this) {
    resume(app);
  }
}

void HomeScreen::open_focused(App& app) {
  if (rows_.empty()) {
    return;
  }
  const Row& row = rows_[focus_.row()];
  const int i = focus_.index();
  if (row.kind == RowKind::Libraries) {
    if (i < static_cast<int>(libraries_.size())) app.push(make_grid_screen(libraries_[i]));
  } else if (i < static_cast<int>(row.items.size())) {
    app.push(make_item_screen(row.items[i]));
  }
}

void HomeScreen::open_options(App& app) {
  app.open_menu(util::trf(util::Str::OptionsTitle, util::kVersionTag),
                {MenuItem{util::tr(util::Str::Refresh), [this](App& a) { load(a, true); }},
                 app.language_menu_item(),
                 MenuItem{util::tr(util::Str::SignOut), [](App& a) { a.logout(); }},
                 MenuItem{util::tr(util::Str::About), [](App& a) { a.push(make_about_screen()); }},
                 MenuItem{util::tr(util::Str::QuitApp), [](App& a) { a.request_quit(); }}});
}

void HomeScreen::handle(App& app, const platform::Input& in) {
  if (!loaded() && error_.empty() && in.event != InputEvent::Menu &&
      in.event != InputEvent::Back) {
    return;  // nothing to browse until the home screen is displayed
  }
  switch (in.event) {
    case InputEvent::Up: focus_.move(Dir::Up); break;
    case InputEvent::Down: focus_.move(Dir::Down); break;
    case InputEvent::Left: focus_.move(Dir::Left); break;
    case InputEvent::Right: focus_.move(Dir::Right); break;
    case InputEvent::Ok:
      if (!error_.empty()) {
        load(app, true);
      } else {
        open_focused(app);
      }
      break;
    case InputEvent::Menu:
    case InputEvent::Back:
      open_options(app);
      break;
    default:
      break;
  }
}

int HomeScreen::row_height(const Row& row) const {
  return kRowTitleH +
         (row.kind == RowKind::Libraries ? kTileH + 20 : theme::kRowPosterH + kCaptionH) +
         kRowGap;
}

void HomeScreen::draw(App& app, Painter& p) {
  p.label("Pelagia", theme::kMarginX, theme::kMarginY, Weight::Bold, 44, theme::accent());
  p.label_in(app.backend().user_name(),
             rect(theme::kMarginX, theme::kMarginY, theme::kContentW, 60), Weight::Regular,
             theme::kBody, theme::text_dim(), Align::Right);
  const Rect content = rect(0, kContentTop, theme::kScreenW, kContentBottom - kContentTop);
  if (!error_.empty()) {
    p.status(content, util::trf(util::Str::ErrorRetry, error_.c_str()), false, true);
    return;
  }
  if (!loaded()) {
    p.status(content, util::tr(util::Str::LoadingLibrary), true, false);
    return;
  }
  if (rows_.empty()) {
    p.status(content, util::tr(util::Str::NoLibraries), false, false);
    return;
  }
  // Minimal vertical scrolling: the focus row stays fully visible.
  int top = kContentTop;
  int focus_top = 0;
  for (int r = 0; r < static_cast<int>(rows_.size()); ++r) {
    if (r == focus_.row()) focus_top = top;
    top += row_height(rows_[r]);
  }
  const int focus_h = row_height(rows_[focus_.row()]);
  if (focus_top - scroll_y_ < kContentTop) scroll_y_ = focus_top - kContentTop;
  if (focus_top + focus_h - scroll_y_ > kContentBottom) {
    scroll_y_ = focus_top + focus_h - kContentBottom;
  }
  p.clip(&content);
  int y = kContentTop - scroll_y_;
  for (int r = 0; r < static_cast<int>(rows_.size()); ++r) {
    const Row& row = rows_[r];
    const int h = row_height(row);
    if (y + h > kContentTop && y < kContentBottom) {
      const bool row_focused = r == focus_.row();
      p.label(row.title, theme::kMarginX, y, Weight::Bold, theme::kHeading,
              row_focused ? theme::text() : theme::text_dim());
      const int item_y = y + kRowTitleH;
      if (row.kind == RowKind::Libraries) {
        const int first = focus_.scroll_in(r, theme::kContentW / (kTileW + theme::kGap));
        for (int i = first; i < static_cast<int>(libraries_.size()); ++i) {
          const Rect tile = rect(theme::kMarginX + (i - first) * (kTileW + theme::kGap), item_y,
                                 kTileW, kTileH);
          if (tile.x > theme::kScreenW) break;
          const bool f = row_focused && i == focus_.index_in(r);
          p.fill(tile, f ? theme::accent() : theme::surface());
          p.label_in(libraries_[i].name, tile, Weight::Bold, theme::kHeading, theme::text(),
                     Align::Center);
          if (f) p.border(tile, theme::kFocusBorder, theme::focus_ring());
        }
      } else {
        const int pitch = theme::kRowPosterW + theme::kGap;
        const int first = focus_.scroll_in(r, theme::kContentW / pitch);
        for (int i = first; i < static_cast<int>(row.items.size()); ++i) {
          const int x = theme::kMarginX + (i - first) * pitch;
          if (x > theme::kScreenW) break;
          const api::MediaItem& item = row.items[i];
          const bool f = row_focused && i == focus_.index_in(r);
          p.poster(item, rect(x, item_y, theme::kRowPosterW, theme::kRowPosterH), f,
                   theme::kPosterFetchW);
          const bool episode = item.type == "Episode";
          const std::string line1 = episode ? item.series_name : display_title(item);
          std::string line2 = episode ? episode_code(item) + " · " + display_title(item)
                                      : (item.production_year > 0
                                             ? std::to_string(item.production_year)
                                             : std::string());
          const int cy = item_y + theme::kRowPosterH + 10;
          p.label_in(line1, rect(x, cy, theme::kRowPosterW, 32), f ? Weight::Bold : Weight::Regular,
                     theme::kSmall, f ? theme::text() : theme::text_dim());
          p.label_in(line2, rect(x, cy + 32, theme::kRowPosterW, 30), Weight::Regular, 22,
                     theme::text_dim());
        }
      }
    }
    y += h;
  }
  p.clip(nullptr);
  p.hints(util::tr(util::Str::HomeHint));
}

}  // namespace ui
