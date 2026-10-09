// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Tests of the software rasterizer (capture mode): fill, alpha blending,
// clipping, RGBA and Alpha8 textures, scaling, YUV video.

#include <vector>

#include "test_framework.h"
#include "ui/soft_canvas.h"

using ui::rect;
using ui::rgba;
using ui::SoftCanvas;

static void test_fill_and_blend() {
  SoftCanvas c(16, 8);
  CHECK_EQ(c.pixel_rgb(0, 0), 0x000000u);  // fond noir
  c.fill_rect(rect(2, 2, 4, 4), rgba(255, 0, 0));
  CHECK_EQ(c.pixel_rgb(2, 2), 0xFF0000u);
  CHECK_EQ(c.pixel_rgb(5, 5), 0xFF0000u);
  CHECK_EQ(c.pixel_rgb(6, 6), 0x000000u);  // bord exclusif
  c.fill_rect(rect(2, 2, 1, 1), rgba(0, 0, 255, 128));
  const uint32_t mixed = c.pixel_rgb(2, 2);
  CHECK_EQ(mixed >> 16, 127u);  // 255 * (1 - 128/255)
  CHECK_EQ(mixed & 0xFF, 128u);
  // Outside the image: ignored without a crash.
  c.fill_rect(rect(-10, -10, 100, 3), rgba(0, 255, 0));
  c.fill_rect(rect(20, 20, 5, 5), rgba(0, 255, 0));
  CHECK_EQ(c.pixel_rgb(15, 7), 0x000000u);
}

static void test_clip() {
  SoftCanvas c(10, 10);
  const platform::Rect clip = rect(0, 0, 5, 10);
  c.set_clip(&clip);
  c.fill_rect(rect(0, 0, 10, 10), rgba(255, 255, 255));
  CHECK_EQ(c.pixel_rgb(4, 4), 0xFFFFFFu);
  CHECK_EQ(c.pixel_rgb(5, 4), 0x000000u);
  c.set_clip(nullptr);
  c.fill_rect(rect(9, 9, 1, 1), rgba(255, 255, 255));
  CHECK_EQ(c.pixel_rgb(9, 9), 0xFFFFFFu);
}

static void test_textures() {
  SoftCanvas c(8, 8);
  // 2x2 RGBA copied 1:1, white tint.
  const uint8_t px[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 0};
  const platform::TextureId t = c.create_texture(2, 2, platform::PixelFormat::Rgba8);
  CHECK(t != 0);
  CHECK(c.update_texture(t, 0, 0, 2, 2, px, 8));
  CHECK(!c.update_texture(t, 1, 1, 2, 2, px, 8));  // overflows: refused
  c.draw_texture(t, rect(0, 0, 2, 2), rect(1, 1, 2, 2), rgba(255, 255, 255));
  CHECK_EQ(c.pixel_rgb(1, 1), 0xFF0000u);
  CHECK_EQ(c.pixel_rgb(2, 1), 0x00FF00u);
  CHECK_EQ(c.pixel_rgb(1, 2), 0x0000FFu);
  CHECK_EQ(c.pixel_rgb(2, 2), 0x000000u);  // texel transparent
  // x4 enlargement: identical corners, interpolated middle.
  SoftCanvas big(8, 8);
  const platform::TextureId t2 = big.create_texture(2, 1, platform::PixelFormat::Rgba8);
  const uint8_t line[8] = {0, 0, 0, 255, 255, 255, 255, 255};
  big.update_texture(t2, 0, 0, 2, 1, line, 8);
  big.draw_texture(t2, rect(0, 0, 2, 1), rect(0, 0, 8, 1), rgba(255, 255, 255));
  CHECK_EQ(big.pixel_rgb(0, 0), 0x000000u);
  CHECK_EQ(big.pixel_rgb(7, 0), 0xFFFFFFu);
  const uint32_t mid = big.pixel_rgb(4, 0) >> 16;
  CHECK(mid > 64 && mid < 192);
  // Alpha8: coverage drawn in the tint color.
  const platform::TextureId glyph = c.create_texture(1, 1, platform::PixelFormat::Alpha8);
  const uint8_t cov = 255;
  c.update_texture(glyph, 0, 0, 1, 1, &cov, 1);
  c.draw_texture(glyph, rect(0, 0, 1, 1), rect(6, 6, 1, 1), rgba(10, 20, 30));
  CHECK_EQ(c.pixel_rgb(6, 6), 0x0A141Eu);
  c.destroy_texture(glyph);
  c.draw_texture(glyph, rect(0, 0, 1, 1), rect(7, 7, 1, 1), rgba(255, 255, 255));
  CHECK_EQ(c.pixel_rgb(7, 7), 0x000000u);  // texture destroyed: nothing
  CHECK_EQ(c.texture_count(), 1u);
}

static void test_video() {
  SoftCanvas c(40, 40);
  // White 4x2 (16:8) YUV420p frame: drawn with bars at the top/bottom.
  std::vector<uint8_t> y(4 * 2, 235), u(2 * 1, 128), v(2 * 1, 128);
  CHECK(c.submit_video_yuv(y.data(), u.data(), v.data(), 4, 2, 2, 4, 2));
  c.draw_video(rect(0, 0, 40, 40));
  CHECK_EQ(c.pixel_rgb(20, 20) >> 16, 255u);
  CHECK_EQ(c.pixel_rgb(20, 2), 0x000000u);   // bande noire
  c.clear_video();
  c.clear();
  c.draw_video(rect(0, 0, 40, 40));
  CHECK_EQ(c.pixel_rgb(20, 20), 0x000000u);
}

// Drawings entirely outside the image or the clip: the intersection is
// empty but its coordinates fall outside the image; nothing must be read or written
// outside the buffer (detected by _GLIBCXX_ASSERTIONS / ASan, see CI).
static void test_fully_outside() {
  SoftCanvas c(64, 64);
  const platform::TextureId t = c.create_texture(8, 8, platform::PixelFormat::Alpha8);
  const std::vector<uint8_t> cov(64, 255);
  c.update_texture(t, 0, 0, 8, 8, cov.data(), 8);
  const platform::Rect far_cases[] = {
      rect(262, 16, 13, 45),   // to the right (case of the text test glyph)
      rect(-300, 10, 20, 20),  // to the left
      rect(10, 500, 20, 20),   // below
      rect(10, -90, 20, 20),   // above
      rect(64, 64, 8, 8),      // just past the corner
  };
  for (const platform::Rect& r : far_cases) {
    c.draw_texture(t, rect(0, 0, 8, 8), r, rgba(255, 255, 255));
    c.fill_rect(r, rgba(255, 255, 255));
  }
  // Clip that excludes the destination.
  const platform::Rect clip = rect(0, 0, 4, 4);
  c.set_clip(&clip);
  c.draw_texture(t, rect(0, 0, 8, 8), rect(30, 30, 8, 8), rgba(255, 255, 255));
  c.fill_rect(rect(30, 30, 8, 8), rgba(255, 255, 255));
  c.set_clip(nullptr);
  // Empty clip and zero-width texture on the source side.
  const platform::Rect empty_clip = rect(10, 10, 0, 0);
  c.set_clip(&empty_clip);
  c.fill_rect(rect(0, 0, 64, 64), rgba(255, 255, 255));
  c.set_clip(nullptr);
  c.draw_texture(t, rect(100, 100, 8, 8), rect(0, 0, 8, 8), rgba(255, 255, 255));
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      if (c.pixel_rgb(x, y) != 0) {
        CHECK(false);
        return;
      }
    }
  }
}

int main() {
  test_fill_and_blend();
  test_clip();
  test_textures();
  test_video();
  test_fully_outside();
  return testfw::test_failures();
}
