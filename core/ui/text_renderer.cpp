// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "ui/text_renderer.h"

#include <cmath>

#include "ui/embedded_fonts.h"
#include "util/log.h"
#include "util/text.h"

namespace ui {

namespace {

const char kEllipsis[] = "\xE2\x80\xA6";  // U+2026
constexpr int kGlyphPadding = 1;           // avoids filtering overflow

uint64_t glyph_key(Weight weight, int px, uint32_t cp) {
  return (static_cast<uint64_t>(weight) << 56) | (static_cast<uint64_t>(px & 0xFFFF) << 32) |
         cp;
}

std::string trim_right(std::string s) {
  while (!s.empty() && s.back() == ' ') {
    s.pop_back();
  }
  return s;
}

}  // namespace

TextRenderer::TextRenderer(Canvas* canvas, int atlas_size)
    : canvas_(canvas), atlas_size_(atlas_size) {}

TextRenderer::~TextRenderer() {
  if (atlas_ != 0) {
    canvas_->destroy_texture(atlas_);
  }
}

bool TextRenderer::init() {
  const bool ok = fonts_[0].load(kNotoSansRegular, kNotoSansRegularSize) &&
                  fonts_[1].load(kNotoSansBold, kNotoSansBoldSize);
  if (!ok) {
    LOG_ERROR("Unreadable embedded fonts");
  }
  return ok;
}

int TextRenderer::line_height(Weight weight, int px) const {
  int a = 0, d = 0, g = 0;
  font(weight).vertical_metrics(px, &a, &d, &g);
  return a - d + g;
}

int TextRenderer::ascent(Weight weight, int px) const {
  int a = 0, d = 0, g = 0;
  font(weight).vertical_metrics(px, &a, &d, &g);
  return a;
}

bool TextRenderer::covers(uint32_t cp, Weight weight) const {
  return font(weight).glyph_index(cp) != 0;
}

void TextRenderer::reset_atlas() {
  glyphs_.clear();
  pack_x_ = pack_y_ = shelf_h_ = 0;
  ++atlas_resets_;
  LOG_DEBUG("Glyph atlas full: emptied (%d)", atlas_resets_);
}

// Shelf packing: left to right, new shelf when the row
// is full. False if the atlas is full.
bool TextRenderer::pack(int w, int h, int* x, int* y) {
  if (w + kGlyphPadding > atlas_size_ || h + kGlyphPadding > atlas_size_) {
    return false;
  }
  if (pack_x_ + w + kGlyphPadding > atlas_size_) {
    pack_x_ = 0;
    pack_y_ += shelf_h_;
    shelf_h_ = 0;
  }
  if (pack_y_ + h + kGlyphPadding > atlas_size_) {
    return false;
  }
  *x = pack_x_;
  *y = pack_y_;
  pack_x_ += w + kGlyphPadding;
  if (h + kGlyphPadding > shelf_h_) {
    shelf_h_ = h + kGlyphPadding;
  }
  return true;
}

const TextRenderer::Glyph* TextRenderer::glyph(Weight weight, int px, uint32_t cp) {
  if (util::is_invisible_char(cp)) {
    return nullptr;
  }
  const Font& f = font(weight);
  if (!f.loaded()) {
    return nullptr;
  }
  const uint64_t key = glyph_key(weight, px, cp);
  auto it = glyphs_.find(key);
  if (it != glyphs_.end()) {
    return &it->second;
  }
  Glyph g;
  g.index = f.glyph_index(cp);
  if (g.index == 0) {
    g.index = f.glyph_index(util::kReplacementChar);
  }
  g.advance = f.advance(g.index, px);
  f.rasterize(g.index, px, &scratch_, &g.w, &g.h, &g.xoff, &g.yoff);
  if (g.w > 0) {
    if (atlas_ == 0) {
      atlas_ = canvas_->create_texture(atlas_size_, atlas_size_, PixelFormat::Alpha8);
      if (atlas_ == 0) {
        return nullptr;
      }
      // Initial content undefined: we start from a transparent atlas.
      const std::vector<uint8_t> zero(static_cast<size_t>(atlas_size_) * atlas_size_, 0);
      canvas_->update_texture(atlas_, 0, 0, atlas_size_, atlas_size_, zero.data(),
                              atlas_size_);
    }
    if (!pack(g.w, g.h, &g.x, &g.y)) {
      reset_atlas();
      if (!pack(g.w, g.h, &g.x, &g.y)) {
        return nullptr;  // glyph bigger than the atlas
      }
    }
    canvas_->update_texture(atlas_, g.x, g.y, g.w, g.h, scratch_.data(), g.w);
  }
  return &(glyphs_[key] = g);
}

template <typename Fn>
float TextRenderer::layout(const std::string& text, Weight weight, int px, Fn fn) {
  float pen = 0.0f;
  int previous = 0;
  for (size_t pos = 0; pos < text.size();) {
    const uint32_t cp = util::utf8_next(text, &pos);
    const Glyph* g = glyph(weight, px, cp);
    if (!g) {
      continue;
    }
    if (previous != 0) {
      pen += font(weight).kerning(previous, g->index, px);
    }
    fn(*g, pen);
    pen += g->advance;
    previous = g->index;
  }
  return pen;
}

int TextRenderer::measure(const std::string& text, Weight weight, int px) {
  return static_cast<int>(std::ceil(layout(text, weight, px, [](const Glyph&, float) {})));
}

int TextRenderer::draw(const std::string& text, int x, int y, Weight weight, int px,
                       Color color) {
  const int baseline = y + ascent(weight, px);
  const float width = layout(text, weight, px, [&](const Glyph& g, float pen) {
    if (g.w > 0) {
      const int gx = x + static_cast<int>(std::lround(pen)) + g.xoff;
      canvas_->draw_texture(atlas_, rect(g.x, g.y, g.w, g.h),
                            rect(gx, baseline + g.yoff, g.w, g.h), color);
    }
  });
  return static_cast<int>(std::ceil(width));
}

std::string TextRenderer::ellipsize(const std::string& text, Weight weight, int px,
                                    int max_width) {
  if (measure(text, weight, px) <= max_width) {
    return text;
  }
  const int budget = max_width - measure(kEllipsis, weight, px);
  std::string prefix;
  for (size_t pos = 0; pos < text.size();) {
    util::utf8_next(text, &pos);
    if (measure(text.substr(0, pos), weight, px) > budget) {
      break;
    }
    prefix = text.substr(0, pos);
  }
  return trim_right(prefix) + kEllipsis;
}

std::vector<std::string> TextRenderer::wrap(const std::string& text, Weight weight, int px,
                                            int max_width, int max_lines) {
  std::vector<std::string> lines;
  std::string line;
  size_t pos = 0;
  // End of text not placed when the number of lines is reached.
  size_t rest_start = std::string::npos;
  const char* rest_join = " ";  // "" if the cut falls inside a word
  while (pos < text.size()) {
    // Next word (single spaces: the text is already cleaned).
    size_t end = text.find(' ', pos);
    if (end == std::string::npos) {
      end = text.size();
    }
    const std::string word = text.substr(pos, end - pos);
    const std::string candidate = line.empty() ? word : line + " " + word;
    if (measure(candidate, weight, px) <= max_width) {
      line = candidate;
      pos = end + 1;
      continue;
    }
    if (!line.empty()) {
      if (static_cast<int>(lines.size()) + 1 == max_lines) {
        rest_start = pos;
        break;
      }
      lines.push_back(line);
      line.clear();
      continue;
    }
    // Single word too long: cut at the last character that fits.
    size_t cut = pos;
    for (size_t p = pos; p < end;) {
      size_t next = p;
      util::utf8_next(text, &next);
      if (measure(text.substr(pos, next - pos), weight, px) > max_width && cut > pos) {
        break;
      }
      cut = next;
      p = next;
    }
    line = text.substr(pos, cut - pos);
    pos = cut;
    if (pos < end) {
      if (static_cast<int>(lines.size()) + 1 == max_lines) {
        rest_start = pos;
        rest_join = "";
        break;
      }
      lines.push_back(line);
      line.clear();
    } else {
      pos = end + 1;
    }
  }
  if (rest_start != std::string::npos) {
    // Last allowed line: what fits, then "...".
    lines.push_back(ellipsize(line + rest_join + text.substr(rest_start), weight, px, max_width));
  } else if (!line.empty()) {
    lines.push_back(line);
  }
  return lines;
}

}  // namespace ui
