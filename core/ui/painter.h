// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UI_PAINTER_H
#define PELAGIA_CORE_UI_PAINTER_H

// Visual elements shared by the screens, built from the Canvas
// primitives: aligned and truncated text, poster (or placeholder frame),
// buttons, progress bar, animated waiting indicator, play/pause icons, key
// hints. No state logic here.

#include <cstdint>
#include <string>

#include "api/jellyfin_models.h"
#include "ui/canvas.h"
#include "ui/poster_cache.h"
#include "ui/text_renderer.h"

namespace ui {

enum class Align { Left, Center, Right };

class Painter {
 public:
  Painter(Canvas* canvas, TextRenderer* text, PosterCache* posters)
      : canvas_(canvas), text_(text), posters_(posters) {}

  Canvas& canvas() { return *canvas_; }
  TextRenderer& text() { return *text_; }
  void set_now(uint64_t now_ms) { now_ms_ = now_ms; }
  uint64_t now() const { return now_ms_; }

  void fill(const Rect& r, Color c) { canvas_->fill_rect(r, c); }
  void clip(const Rect* r) { canvas_->set_clip(r); }
  // Frame of thickness pixels outside r.
  void border(const Rect& r, int thickness, Color c);

  // A line (y = top of line); returns the drawn width.
  int label(const std::string& s, int x, int y, Weight w, int px, Color c);
  // A line truncated with "..." to r.w, aligned in r and vertically centered.
  void label_in(const std::string& s, const Rect& r, Weight w, int px, Color c,
                Align align = Align::Left);
  // Paragraph: wraps within r.w, max_lines lines; returns the
  // height used.
  int paragraph(const std::string& s, const Rect& r, Weight w, int px, Color c,
                int max_lines);

  void progress(const Rect& r, double fraction, Color fg, Color bg);
  // Animated waiting indicator: 8 dots in a circle.
  void spinner(int cx, int cy, int radius, Color c);
  void play_icon(const Rect& r, Color c);
  void pause_icon(const Rect& r, Color c);

  // Poster of the item, cropped to fill r (or placeholder frame with
  // the title); progress bar if playback is started, "Watched"
  // mention if the item is marked played; focus frame.
  void poster(const api::MediaItem& item, const Rect& r, bool focused, int fetch_width);
  // Image only (page), without caption; false if it is not ready yet.
  bool poster_image(const PosterKey& key, const Rect& r);

  // play_icon: "play" triangle to the left of the text.
  void button(const Rect& r, const std::string& text, bool focused, bool play_icon = false);
  // Key hint banner at the bottom of the screen.
  void hints(const std::string& text);
  // Message centered in r (error or waiting), with a spinner if waiting.
  void status(const Rect& r, const std::string& text, bool waiting, bool error);

 private:
  Canvas* canvas_;
  TextRenderer* text_;
  PosterCache* posters_;
  uint64_t now_ms_ = 0;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_PAINTER_H
