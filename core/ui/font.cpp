// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "ui/font.h"

#include <cmath>

// stb_truetype implementation compiled here, exactly once. Third-party code:
// its warnings do not concern the project.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace ui {

struct FontImpl {
  stbtt_fontinfo info;
};

Font::Font() {}
Font::~Font() {}

bool Font::load(const unsigned char* data, size_t size) {
  impl_.reset();
  if (!data || size < 12) {
    return false;
  }
  std::unique_ptr<FontImpl> impl(new FontImpl);
  const int offset = stbtt_GetFontOffsetForIndex(data, 0);
  if (offset < 0 || !stbtt_InitFont(&impl->info, data, offset)) {
    return false;
  }
  impl_ = std::move(impl);
  return true;
}

float Font::scale(int px) const {
  return stbtt_ScaleForMappingEmToPixels(&impl_->info, static_cast<float>(px));
}

int Font::glyph_index(uint32_t cp) const {
  return impl_ ? stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(cp)) : 0;
}

void Font::vertical_metrics(int px, int* ascent, int* descent, int* line_gap) const {
  int a = 0, d = 0, g = 0;
  stbtt_GetFontVMetrics(&impl_->info, &a, &d, &g);
  const float s = scale(px);
  *ascent = static_cast<int>(std::lround(a * s));
  *descent = static_cast<int>(std::lround(d * s));
  *line_gap = static_cast<int>(std::lround(g * s));
}

float Font::advance(int glyph, int px) const {
  int adv = 0, lsb = 0;
  stbtt_GetGlyphHMetrics(&impl_->info, glyph, &adv, &lsb);
  return adv * scale(px);
}

float Font::kerning(int glyph1, int glyph2, int px) const {
  return stbtt_GetGlyphKernAdvance(&impl_->info, glyph1, glyph2) * scale(px);
}

void Font::rasterize(int glyph, int px, std::vector<uint8_t>* bitmap, int* w, int* h,
                     int* xoff, int* yoff) const {
  const float s = scale(px);
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  stbtt_GetGlyphBitmapBox(&impl_->info, glyph, s, s, &x0, &y0, &x1, &y1);
  *w = x1 - x0;
  *h = y1 - y0;
  *xoff = x0;
  *yoff = y0;
  if (*w <= 0 || *h <= 0) {
    *w = *h = 0;
    bitmap->clear();
    return;
  }
  bitmap->assign(static_cast<size_t>(*w) * *h, 0);
  stbtt_MakeGlyphBitmap(&impl_->info, bitmap->data(), *w, *h, *w, s, s, glyph);
}

}  // namespace ui
