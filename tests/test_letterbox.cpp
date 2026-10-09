// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Tests of the display geometry (aspect ratio kept, black bars).

#include "letterbox.h"

#include "test_framework.h"

using platform_sdl::fit_rect;
using platform_sdl::FitRect;

static bool same(const FitRect& r, int x, int y, int w, int h) {
  return r.x == x && r.y == y && r.w == w && r.h == h;
}

int main() {
  // Same ratio: fills the whole window, no bars.
  CHECK(same(fit_rect(1920, 1080, 1280, 720), 0, 0, 1280, 720));
  CHECK(same(fit_rect(1920, 1080, 3840, 2160), 0, 0, 3840, 2160));

  // Window smaller than the video, different ratio (4:3 for 16:9):
  // bars at the top and bottom.
  CHECK(same(fit_rect(1920, 1080, 800, 600), 0, 75, 800, 450));

  // Very wide window: bars on the sides.
  CHECK(same(fit_rect(1920, 1080, 1600, 720), 160, 0, 1280, 720));

  // Narrow and tall window: bars at the top and bottom.
  CHECK(same(fit_rect(1920, 1080, 720, 1280), 0, 437, 720, 405));

  // 4:3 video in a 16:9 window: centered side bars.
  CHECK(same(fit_rect(640, 480, 1920, 1080), 240, 0, 1440, 1080));

  // The rectangle always stays inside the window and centered.
  const int sizes[] = {1, 7, 100, 333, 720, 1080, 1921};
  for (int sw : sizes) {
    for (int sh : sizes) {
      for (int dw : sizes) {
        for (int dh : sizes) {
          const FitRect r = fit_rect(sw, sh, dw, dh);
          CHECK(r.x >= 0 && r.y >= 0);
          CHECK(r.x + r.w <= dw && r.y + r.h <= dh);
          CHECK(r.x == (dw - r.w) / 2 && r.y == (dh - r.h) / 2);
        }
      }
    }
  }

  // Invalid dimensions: full rectangle, no division by zero.
  CHECK(same(fit_rect(0, 1080, 800, 600), 0, 0, 800, 600));
  CHECK(same(fit_rect(1920, 1080, 0, 0), 0, 0, 0, 0));

  return testfw::test_failures();
}
