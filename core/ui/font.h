// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UI_FONT_H
#define PELAGIA_CORE_UI_FONT_H

// TrueType font rasterized by stb_truetype (third_party/stb, public
// domain / MIT, a single portable C file: nothing else to cross-compile).
// Sizes are in pixels per em (like font-size in CSS).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ui {

struct FontImpl;

class Font {
 public:
  Font();
  ~Font();
  Font(const Font&) = delete;
  Font& operator=(const Font&) = delete;

  // data must stay valid as long as the font is used (embedded
  // data: lifetime of the program).
  bool load(const unsigned char* data, size_t size);
  bool loaded() const { return impl_ != nullptr; }

  // 0 if the font has no glyph for this code point.
  int glyph_index(uint32_t cp) const;

  // Vertical metrics rounded to pixels (negative descent).
  void vertical_metrics(int px, int* ascent, int* descent, int* line_gap) const;
  float advance(int glyph, int px) const;
  float kerning(int glyph1, int glyph2, int px) const;

  // 8-bit coverage of the glyph (empty for a space: w == 0). xoff/yoff:
  // position of the top-left corner relative to the origin on the baseline.
  void rasterize(int glyph, int px, std::vector<uint8_t>* bitmap, int* w, int* h,
                 int* xoff, int* yoff) const;

 private:
  float scale(int px) const;
  std::unique_ptr<FontImpl> impl_;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_FONT_H
