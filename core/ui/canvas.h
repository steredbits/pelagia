// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UI_CANVAS_H
#define PELAGIA_CORE_UI_CANVAS_H

// Drawing surface of the UI. Two implementations:
// - PlatformCanvas (ui/platform_bridge): platform.h primitives (screen);
// - SoftCanvas (ui/soft_canvas): CPU rasterizer for the PNG capture mode and
//   the tests, with no window or GPU.
// Screens only know this interface. Logical coordinates
// 1920x1080 (platform::kLogicalWidth/Height).

#include <cstdint>

#include "platform.h"

namespace ui {

using platform::Color;
using platform::PixelFormat;
using platform::Rect;
using platform::TextureId;

class Canvas {
 public:
  virtual ~Canvas() {}

  virtual TextureId create_texture(int w, int h, PixelFormat format) = 0;
  virtual bool update_texture(TextureId id, int x, int y, int w, int h,
                              const uint8_t* pixels, int pitch) = 0;
  virtual void destroy_texture(TextureId id) = 0;

  virtual void fill_rect(const Rect& rect, Color color) = 0;
  virtual void draw_texture(TextureId id, const Rect& src, const Rect& dst,
                            Color tint) = 0;
  virtual void set_clip(const Rect* clip) = 0;

  // Video: last YUV420p frame submitted by the player, drawn with aspect
  // ratio kept in area.
  virtual bool submit_video_yuv(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                                int stride_y, int stride_u, int stride_v, int w,
                                int h) = 0;
  virtual void draw_video(const Rect& area) = 0;
  virtual void clear_video() = 0;
};

inline Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
  Color c;
  c.r = r;
  c.g = g;
  c.b = b;
  c.a = a;
  return c;
}

inline Rect rect(int x, int y, int w, int h) {
  Rect r;
  r.x = x;
  r.y = y;
  r.w = w;
  r.h = h;
  return r;
}

// Intersection (w or h zero if empty).
inline Rect intersect(const Rect& a, const Rect& b) {
  const int x0 = a.x > b.x ? a.x : b.x;
  const int y0 = a.y > b.y ? a.y : b.y;
  const int x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
  const int y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
  return rect(x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0);
}

}  // namespace ui

#endif  // PELAGIA_CORE_UI_CANVAS_H
