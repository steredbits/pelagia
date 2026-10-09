// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UI_TEXT_RENDERER_H
#define PELAGIA_CORE_UI_TEXT_RENDERER_H

// UI text: glyphs rasterized on demand into an Alpha8 atlas
// (one texture), then drawn as tinted sprites. UTF-8 input;
// invisible characters are ignored, those the font does not cover
// are shown as U+FFFD (no fallback font in v1, see ROADMAP).

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ui/canvas.h"
#include "ui/font.h"

namespace ui {

enum class Weight : uint8_t { Regular = 0, Bold = 1 };

class TextRenderer {
 public:
  // The atlas is created at the first rasterization, on this canvas.
  explicit TextRenderer(Canvas* canvas, int atlas_size = 1024);
  ~TextRenderer();

  // Loads the embedded fonts. False if they are unreadable.
  bool init();

  // Width in pixels of the text on one line.
  int measure(const std::string& text, Weight weight, int px);
  // Line height (ascent + descent + line gap) and ascent.
  int line_height(Weight weight, int px) const;
  int ascent(Weight weight, int px) const;

  // Draws on one line; y = top of the line box. Returns the width.
  int draw(const std::string& text, int x, int y, Weight weight, int px, Color color);

  // Truncates with "..." to fit in max_width (text unchanged if it fits).
  std::string ellipsize(const std::string& text, Weight weight, int px, int max_width);

  // Splits into lines of at most max_width, at spaces (a word that is too long is
  // cut); beyond max_lines, the last line ends with "...".
  // max_lines <= 0: no limit.
  std::vector<std::string> wrap(const std::string& text, Weight weight, int px,
                                int max_width, int max_lines);

  // True if the font has a real glyph for cp (not the fallback glyph).
  bool covers(uint32_t cp, Weight weight) const;

  // Number of cached glyphs (tests); a full atlas is emptied and rebuilt.
  size_t cached_glyphs() const { return glyphs_.size(); }
  int atlas_resets() const { return atlas_resets_; }

 private:
  struct Glyph {
    int index = 0;      // glyph in the font (for kerning)
    float advance = 0;
    int x = 0, y = 0;   // position in the atlas
    int w = 0, h = 0;   // 0 for a space
    int xoff = 0, yoff = 0;
  };

  const Glyph* glyph(Weight weight, int px, uint32_t cp);
  bool pack(int w, int h, int* x, int* y);
  void reset_atlas();
  const Font& font(Weight weight) const { return fonts_[static_cast<int>(weight)]; }

  // Walks the glyphs (kerning included); fn(glyph, pen_x) for each one.
  template <typename Fn>
  float layout(const std::string& text, Weight weight, int px, Fn fn);

  Canvas* canvas_;
  int atlas_size_;
  TextureId atlas_ = 0;
  int pack_x_ = 0, pack_y_ = 0, shelf_h_ = 0;
  int atlas_resets_ = 0;
  Font fonts_[2];
  std::unordered_map<uint64_t, Glyph> glyphs_;
  std::vector<uint8_t> scratch_;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_TEXT_RENDERER_H
