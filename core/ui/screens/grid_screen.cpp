// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "ui/screens/grid_screen.h"

#include <cstdio>

#include "ui/app.h"
#include "ui/format.h"
#include "ui/theme.h"
#include "util/i18n.h"

namespace ui {

namespace {

using platform::InputEvent;

constexpr int kGridTop = 196;
constexpr int kGridBottom = theme::kScreenH - theme::kMarginY - 40;
constexpr int kCaptionH = 44;
constexpr int kRowPitch = theme::kPosterH + kCaptionH + 16;

}  // namespace

void GridScreen::load(App& app) {
  loading_ = true;
  error_.clear();
  loaded_version_ = app.data_version();
  const api::Library lib = library_;
  app.run<ItemsResult>(
      lifetime(), [lib](Backend& b, ItemsResult& r) { r.result = b.library_items(lib, &r.list); },
      [this](ItemsResult& r) {
        loading_ = false;
        if (!r.result.ok()) {
          error_ = describe_error(r.result);
          return;
        }
        loaded_ = true;
        items_ = r.list.items;
        focus_.set_count(static_cast<int>(items_.size()));  // focus kept on reload
      });
}

void GridScreen::update(App& app) {
  // Return from a playback: progress and "Watched" up to date.
  if (app.top() == this && loaded_ && !loading_ && app.data_version() != loaded_version_ &&
      !app.playback_stopping()) {
    load(app);
  }
}

void GridScreen::handle(App& app, const platform::Input& in) {
  switch (in.event) {
    case InputEvent::Up: focus_.move(Dir::Up); break;
    case InputEvent::Down: focus_.move(Dir::Down); break;
    case InputEvent::Left: focus_.move(Dir::Left); break;
    case InputEvent::Right: focus_.move(Dir::Right); break;
    case InputEvent::Ok:
      if (!error_.empty()) {
        load(app);
      } else if (focus_.index() < static_cast<int>(items_.size())) {
        app.push(make_item_screen(items_[focus_.index()]));
      }
      break;
    case InputEvent::Back:
      app.pop();
      break;
    case InputEvent::Menu:
      app.open_menu(util::tr(util::Str::Options), {MenuItem{util::tr(util::Str::Refresh), [this](App& a) { load(a); }},
                                MenuItem{util::tr(util::Str::BackToHome), [](App& a) { a.pop_to_root(); }}});
      break;
    default:
      break;
  }
}

void GridScreen::draw(App& app, Painter& p) {
  (void)app;
  p.label(library_.name, theme::kMarginX, theme::kMarginY - 10, Weight::Bold, theme::kTitle,
          theme::text());
  const Rect content = rect(0, kGridTop, theme::kScreenW, kGridBottom - kGridTop);
  if (!error_.empty()) {
    p.status(content, util::trf(util::Str::ErrorRetry, error_.c_str()), false, true);
    return;
  }
  if (!loaded_) {
    p.status(content, util::tr(util::Str::Loading), true, false);
    return;
  }
  if (items_.empty()) {
    p.status(content, util::tr(util::Str::LibraryEmpty), false, false);
    return;
  }
  // Reminder of the selected element (full title, year, duration, position).
  const api::MediaItem& sel = items_[focus_.index()];
  std::string info = display_title(sel);
  if (sel.production_year > 0) info += "  ·  " + std::to_string(sel.production_year);
  if (sel.runtime_ticks > 0) info += "  ·  " + format_runtime(sel.runtime_ticks / kTicksPerMs);
  p.label_in(info, rect(theme::kMarginX, 122, theme::kContentW - 220, 50), Weight::Regular,
             theme::kBody, theme::accent());
  char count[48];
  std::snprintf(count, sizeof(count), "%d / %d", focus_.index() + 1,
                static_cast<int>(items_.size()));
  p.label_in(count, rect(theme::kMarginX, 122, theme::kContentW, 50), Weight::Regular,
             theme::kBody, theme::text_dim(), Align::Right);

  const int visible_rows = (kGridBottom - kGridTop) / kRowPitch;
  const int top_row = focus_.scroll_row(visible_rows > 0 ? visible_rows : 1);
  p.clip(&content);
  // Visible rows, plus a partial one at the bottom (hints at what follows).
  for (int row = top_row; row <= top_row + visible_rows && row < focus_.rows(); ++row) {
    for (int c = 0; c < focus_.columns(); ++c) {
      const int i = row * focus_.columns() + c;
      if (i >= static_cast<int>(items_.size())) break;
      const int x = theme::kMarginX + c * (theme::kPosterW + theme::kGap);
      const int y = kGridTop + 8 + (row - top_row) * kRowPitch;
      const bool f = i == focus_.index();
      p.poster(items_[i], rect(x, y, theme::kPosterW, theme::kPosterH), f, theme::kPosterFetchW);
      p.label_in(display_title(items_[i]), rect(x, y + theme::kPosterH + 8, theme::kPosterW, 34),
                 f ? Weight::Bold : Weight::Regular, theme::kSmall,
                 f ? theme::text() : theme::text_dim());
    }
  }
  p.clip(nullptr);
  p.hints(util::tr(util::Str::GridHint));
}

}  // namespace ui
