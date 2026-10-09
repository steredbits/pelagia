// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UI_SOFT_CANVAS_H
#define PELAGIA_CORE_UI_SOFT_CANVAS_H

// CPU rasterizer of the Canvas interface: in-memory RGBA image, non-premultiplied
// alpha blending and bilinear scaling, like the SDL rendering. Used by the
// capture mode (PNG of the screens) and the tests; never for the real display.
// The produced image faithfully reflects the layout; down to the pixel, the
// smoothing may differ slightly from the GPU's.

#include <cstdint>
#include <map>
#include <vector>

#include "ui/canvas.h"

struct SwsContext;

namespace ui {

class SoftCanvas final : public Canvas {
 public:
  SoftCanvas(int width, int height);
  ~SoftCanvas() override;

  TextureId create_texture(int w, int h, PixelFormat format) override;
  bool update_texture(TextureId id, int x, int y, int w, int h, const uint8_t* pixels,
                      int pitch) override;
  void destroy_texture(TextureId id) override;
  void fill_rect(const Rect& r, Color color) override;
  void draw_texture(TextureId id, const Rect& src, const Rect& dst, Color tint) override;
  void set_clip(const Rect* clip) override;
  bool submit_video_yuv(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                        int stride_y, int stride_u, int stride_v, int w, int h) override;
  void draw_video(const Rect& area) override;
  void clear_video() override;

  // Clears the whole image (opaque black): equivalent of render_begin().
  void clear();

  int width() const { return width_; }
  int height() const { return height_; }
  // RGBA pixels, row by row (width * 4 bytes per row).
  const std::vector<uint8_t>& pixels() const { return pixels_; }
  // Pixel (x, y) in 0xRRGGBB format (tests).
  uint32_t pixel_rgb(int x, int y) const;
  size_t texture_count() const { return textures_.size(); }

 private:
  struct Texture {
    int w = 0;
    int h = 0;
    PixelFormat format = PixelFormat::Rgba8;
    std::vector<uint8_t> data;  // RGBA (4 bytes) or coverage (1 byte)
  };

  void draw_texture_data(const Texture& t, const Rect& src, const Rect& dst, Color tint);
  void blend(uint8_t* dst, int r, int g, int b, int a);
  void sample(const Texture& t, const Rect& src, int fx, int fy, int out[4]) const;
  Rect clip_area() const;

  int width_;
  int height_;
  std::vector<uint8_t> pixels_;
  std::map<TextureId, Texture> textures_;
  TextureId next_id_ = 1;
  bool has_clip_ = false;
  Rect clip_;
  Texture video_;  // last video frame converted to RGBA
  SwsContext* sws_ = nullptr;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_SOFT_CANVAS_H
