// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_PLATFORM_LINUX_LETTERBOX_H
#define PELAGIA_PLATFORM_LINUX_LETTERBOX_H

// Display geometry: largest rectangle with the video's aspect ratio fitted
// in the window, centered (black bars on the sides or at the top/bottom).
// Pure integer computation, no SDL: testable without a display.

#include <cstdint>

namespace platform_sdl {

struct FitRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

inline FitRect fit_rect(int src_w, int src_h, int dst_w, int dst_h) {
  FitRect r;
  r.w = dst_w;
  r.h = dst_h;
  if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
    return r;
  }
  // Window wider than the video (comparison by cross product):
  // the height is full, vertical bars; otherwise the width is full.
  if (static_cast<int64_t>(dst_w) * src_h > static_cast<int64_t>(dst_h) * src_w) {
    r.w = static_cast<int>(static_cast<int64_t>(dst_h) * src_w / src_h);
    r.x = (dst_w - r.w) / 2;
  } else {
    r.h = static_cast<int>(static_cast<int64_t>(dst_w) * src_h / src_w);
    r.y = (dst_h - r.h) / 2;
  }
  return r;
}

}  // namespace platform_sdl

#endif  // PELAGIA_PLATFORM_LINUX_LETTERBOX_H
