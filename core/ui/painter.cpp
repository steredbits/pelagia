// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "ui/painter.h"

#include <cmath>

#include "ui/format.h"
#include "ui/theme.h"
#include "util/i18n.h"

namespace ui {

void Painter::border(const Rect& r, int t, Color c) {
  fill(rect(r.x - t, r.y - t, r.w + 2 * t, t), c);
  fill(rect(r.x - t, r.y + r.h, r.w + 2 * t, t), c);
  fill(rect(r.x - t, r.y, t, r.h), c);
  fill(rect(r.x + r.w, r.y, t, r.h), c);
}

int Painter::label(const std::string& s, int x, int y, Weight w, int px, Color c) {
  return text_->draw(s, x, y, w, px, c);
}

void Painter::label_in(const std::string& s, const Rect& r, Weight w, int px, Color c,
                       Align align) {
  const std::string fitted = text_->ellipsize(s, w, px, r.w);
  const int width = text_->measure(fitted, w, px);
  int x = r.x;
  if (align == Align::Center) {
    x = r.x + (r.w - width) / 2;
  } else if (align == Align::Right) {
    x = r.x + r.w - width;
  }
  const int y = r.y + (r.h - text_->line_height(w, px)) / 2;
  text_->draw(fitted, x, y, w, px, c);
}

int Painter::paragraph(const std::string& s, const Rect& r, Weight w, int px, Color c,
                       int max_lines) {
  const int line_h = text_->line_height(w, px);
  int y = r.y;
  for (const std::string& line : text_->wrap(s, w, px, r.w, max_lines)) {
    text_->draw(line, r.x, y, w, px, c);
    y += line_h;
  }
  return y - r.y;
}

void Painter::progress(const Rect& r, double fraction, Color fg, Color bg) {
  if (fraction < 0) fraction = 0;
  if (fraction > 1) fraction = 1;
  fill(r, bg);
  fill(rect(r.x, r.y, static_cast<int>(r.w * fraction + 0.5), r.h), fg);
}

void Painter::spinner(int cx, int cy, int radius, Color c) {
  const int dots = 8;
  const int dot = radius / 3 > 4 ? radius / 3 : 4;
  const int head = static_cast<int>(now_ms_ / 110) % dots;
  for (int i = 0; i < dots; ++i) {
    const double a = 2.0 * 3.14159265358979 * i / dots - 3.14159265358979 / 2;
    const int x = cx + static_cast<int>(std::lround(std::cos(a) * radius)) - dot / 2;
    const int y = cy + static_cast<int>(std::lround(std::sin(a) * radius)) - dot / 2;
    const int age = (head - i + dots) % dots;  // 0 = most recent point
    Color dc = c;
    dc.a = static_cast<uint8_t>(255 - age * 26);
    fill(rect(x, y, dot, dot), dc);
  }
}

void Painter::play_icon(const Rect& r, Color c) {
  // Triangle pointing right, drawn line by line.
  for (int y = 0; y < r.h; ++y) {
    const int half = r.h / 2;
    const int dist = y < half ? y : r.h - 1 - y;
    const int w = static_cast<int>(static_cast<int64_t>(r.w) * (2 * dist + 1) / r.h);
    fill(rect(r.x, r.y + y, w, 1), c);
  }
}

void Painter::pause_icon(const Rect& r, Color c) {
  const int bar = r.w * 2 / 5;
  fill(rect(r.x, r.y, bar, r.h), c);
  fill(rect(r.x + r.w - bar, r.y, bar, r.h), c);
}

bool Painter::poster_image(const PosterKey& key, const Rect& r) {
  const Poster* p = posters_->get(key);
  if (!p) {
    return false;
  }
  // Centered crop to fill r without distortion.
  Rect src = rect(0, 0, p->w, p->h);
  if (static_cast<int64_t>(p->w) * r.h > static_cast<int64_t>(p->h) * r.w) {
    src.w = static_cast<int>(static_cast<int64_t>(p->h) * r.w / r.h);
    src.x = (p->w - src.w) / 2;
  } else {
    src.h = static_cast<int>(static_cast<int64_t>(p->w) * r.h / r.w);
    src.y = (p->h - src.h) / 2;
  }
  canvas_->draw_texture(p->texture, src, r, rgba(255, 255, 255));
  return true;
}

void Painter::poster(const api::MediaItem& item, const Rect& r, bool focused, int fetch_width) {
  if (!poster_image(poster_key_for(item, fetch_width), r)) {
    // Replacement: plain background and title (also while loading).
    fill(r, focused ? theme::surface_focus() : theme::surface());
    paragraph(display_title(item), rect(r.x + 14, r.y + r.h / 3, r.w - 28, r.h / 2),
              Weight::Bold, theme::kSmall, theme::text_dim(), 4);
  }
  const double done = progress_fraction(item);
  if (done > 0) {
    progress(rect(r.x, r.y + r.h - 8, r.w, 8), done, theme::accent(), rgba(0, 0, 0, 160));
  }
  if (item.played) {
    const Rect badge = rect(r.x + r.w - 62, r.y + 10, 52, 34);
    fill(badge, theme::accent());
    label_in(util::tr(util::Str::Watched), badge, Weight::Bold, 22, theme::text(), Align::Center);
  }
  if (focused) {
    border(r, theme::kFocusBorder, theme::focus_ring());
  }
}

void Painter::button(const Rect& r, const std::string& s, bool focused, bool with_icon) {
  const Color fg = focused ? theme::background() : theme::text();
  fill(r, focused ? theme::text() : theme::surface());
  const int icon = with_icon ? 40 : 0;  // room reserved for the triangle
  if (with_icon) {
    play_icon(rect(r.x + 30, r.y + (r.h - 26) / 2, 22, 26), fg);
  }
  label_in(s, rect(r.x + 24 + icon, r.y, r.w - 48 - icon, r.h), Weight::Bold, theme::kBody, fg,
           Align::Center);
}

void Painter::hints(const std::string& s) {
  label_in(s, rect(theme::kMarginX, theme::kScreenH - theme::kMarginY - 34, theme::kContentW, 34),
           Weight::Regular, theme::kSmall, theme::text_dim(), Align::Right);
}

void Painter::status(const Rect& r, const std::string& s, bool waiting, bool error) {
  const int line = text_->line_height(Weight::Regular, theme::kBody);
  int y = r.y + (r.h - line) / 2;
  if (waiting) {
    spinner(r.x + r.w / 2, y - 50, 26, theme::text());
  }
  label_in(s, rect(r.x, y, r.w, line), Weight::Regular, theme::kBody,
           error ? theme::danger() : theme::text_dim(), Align::Center);
}

}  // namespace ui
