// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "ui/soft_canvas.h"

#include <cstring>

extern "C" {
#include <libswscale/swscale.h>
}

namespace ui {

namespace {

int clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Source position in 1/256 of a pixel of the center of destination pixel d
// (GPU convention: pixel centers aligned).
int source_pos_256(int d, int src_len, int dst_len) {
  const int64_t num = static_cast<int64_t>(2 * d + 1) * src_len * 256;
  const int pos = static_cast<int>(num / (2 * static_cast<int64_t>(dst_len))) - 128;
  return clamp_int(pos, 0, (src_len - 1) * 256);
}

}  // namespace

SoftCanvas::SoftCanvas(int width, int height)
    : width_(width), height_(height),
      pixels_(static_cast<size_t>(width) * height * 4, 0) {
  clear();
}

SoftCanvas::~SoftCanvas() {
  if (sws_) {
    sws_freeContext(sws_);
  }
}

void SoftCanvas::clear() {
  for (size_t i = 0; i < pixels_.size(); i += 4) {
    pixels_[i] = pixels_[i + 1] = pixels_[i + 2] = 0;
    pixels_[i + 3] = 255;
  }
}

uint32_t SoftCanvas::pixel_rgb(int x, int y) const {
  const uint8_t* p = &pixels_[(static_cast<size_t>(y) * width_ + x) * 4];
  return (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
}

TextureId SoftCanvas::create_texture(int w, int h, PixelFormat format) {
  if (w <= 0 || h <= 0) {
    return 0;
  }
  Texture t;
  t.w = w;
  t.h = h;
  t.format = format;
  t.data.assign(static_cast<size_t>(w) * h * (format == PixelFormat::Rgba8 ? 4 : 1), 0);
  const TextureId id = next_id_++;
  textures_[id] = t;
  return id;
}

bool SoftCanvas::update_texture(TextureId id, int x, int y, int w, int h,
                                const uint8_t* pixels, int pitch) {
  auto it = textures_.find(id);
  if (it == textures_.end() || !pixels || x < 0 || y < 0 || w <= 0 || h <= 0 ||
      x + w > it->second.w || y + h > it->second.h) {
    return false;
  }
  Texture& t = it->second;
  const int bpp = t.format == PixelFormat::Rgba8 ? 4 : 1;
  for (int row = 0; row < h; ++row) {
    std::memcpy(&t.data[(static_cast<size_t>(y + row) * t.w + x) * bpp],
                pixels + static_cast<size_t>(row) * pitch, static_cast<size_t>(w) * bpp);
  }
  return true;
}

void SoftCanvas::destroy_texture(TextureId id) { textures_.erase(id); }

void SoftCanvas::set_clip(const Rect* clip) {
  has_clip_ = clip != nullptr;
  if (clip) {
    clip_ = *clip;
  }
}

Rect SoftCanvas::clip_area() const {
  const Rect full = rect(0, 0, width_, height_);
  return has_clip_ ? intersect(full, clip_) : full;
}

void SoftCanvas::blend(uint8_t* dst, int r, int g, int b, int a) {
  if (a >= 255) {
    dst[0] = static_cast<uint8_t>(r);
    dst[1] = static_cast<uint8_t>(g);
    dst[2] = static_cast<uint8_t>(b);
    return;
  }
  if (a <= 0) {
    return;
  }
  const int inv = 255 - a;
  dst[0] = static_cast<uint8_t>((r * a + dst[0] * inv + 127) / 255);
  dst[1] = static_cast<uint8_t>((g * a + dst[1] * inv + 127) / 255);
  dst[2] = static_cast<uint8_t>((b * a + dst[2] * inv + 127) / 255);
}

void SoftCanvas::fill_rect(const Rect& r, Color color) {
  const Rect area = intersect(r, clip_area());
  if (area.w <= 0 || area.h <= 0) {
    return;  // entirely outside the image or the clip: nothing to write
  }
  for (int y = area.y; y < area.y + area.h; ++y) {
    uint8_t* line = pixels_.data() + (static_cast<size_t>(y) * width_ + area.x) * 4;
    for (int x = 0; x < area.w; ++x) {
      blend(line + x * 4, color.r, color.g, color.b, color.a);
    }
  }
}

// Bilinear sample (premultiplied alpha during interpolation, to
// avoid dark fringes on transparent edges). out = R, G, B, A.
void SoftCanvas::sample(const Texture& t, const Rect& src, int fx, int fy,
                        int out[4]) const {
  const int x0 = fx >> 8;
  const int y0 = fy >> 8;
  const int wx = fx & 255;
  const int wy = fy & 255;
  const int x1 = x0 + 1 < src.w ? x0 + 1 : x0;
  const int y1 = y0 + 1 < src.h ? y0 + 1 : y0;
  const int xs[4] = {x0, x1, x0, x1};
  const int ys[4] = {y0, y0, y1, y1};
  const int ws[4] = {(256 - wx) * (256 - wy), wx * (256 - wy), (256 - wx) * wy, wx * wy};
  int64_t acc[4] = {0, 0, 0, 0};
  for (int k = 0; k < 4; ++k) {
    if (ws[k] == 0) {
      continue;
    }
    const size_t idx = static_cast<size_t>(src.y + ys[k]) * t.w + (src.x + xs[k]);
    int r = 255, g = 255, b = 255, a;
    if (t.format == PixelFormat::Rgba8) {
      const uint8_t* p = &t.data[idx * 4];
      r = p[0], g = p[1], b = p[2], a = p[3];
    } else {
      a = t.data[idx];
    }
    acc[0] += static_cast<int64_t>(r) * a * ws[k];
    acc[1] += static_cast<int64_t>(g) * a * ws[k];
    acc[2] += static_cast<int64_t>(b) * a * ws[k];
    acc[3] += static_cast<int64_t>(a) * ws[k];
  }
  if (acc[3] == 0) {
    out[0] = out[1] = out[2] = out[3] = 0;
    return;
  }
  out[0] = static_cast<int>(acc[0] / acc[3]);
  out[1] = static_cast<int>(acc[1] / acc[3]);
  out[2] = static_cast<int>(acc[2] / acc[3]);
  out[3] = static_cast<int>((acc[3] + 32768) >> 16);
}

void SoftCanvas::draw_texture(TextureId id, const Rect& src, const Rect& dst,
                              Color tint) {
  auto it = textures_.find(id);
  if (it != textures_.end()) {
    draw_texture_data(it->second, src, dst, tint);
  }
}

void SoftCanvas::draw_texture_data(const Texture& t, const Rect& src_in, const Rect& dst,
                                   Color tint) {
  if (dst.w <= 0 || dst.h <= 0) {
    return;
  }
  const Rect src = intersect(src_in, rect(0, 0, t.w, t.h));
  if (src.w <= 0 || src.h <= 0) {
    return;
  }
  const Rect area = intersect(dst, clip_area());
  if (area.w <= 0 || area.h <= 0) {
    // Entirely outside the image or the clip: area.x / area.y may
    // then fall outside the image, no row address must even be computed.
    return;
  }
  std::vector<int> col_pos(static_cast<size_t>(area.w));
  for (int x = 0; x < area.w; ++x) {
    col_pos[x] = source_pos_256(area.x + x - dst.x, src.w, dst.w);
  }
  int texel[4];
  for (int y = area.y; y < area.y + area.h; ++y) {
    const int fy = source_pos_256(y - dst.y, src.h, dst.h);
    uint8_t* line = pixels_.data() + (static_cast<size_t>(y) * width_ + area.x) * 4;
    for (int x = 0; x < area.w; ++x) {
      sample(t, src, col_pos[x], fy, texel);
      const int a = texel[3] * tint.a / 255;
      blend(line + x * 4, texel[0] * tint.r / 255, texel[1] * tint.g / 255,
            texel[2] * tint.b / 255, a);
    }
  }
}

bool SoftCanvas::submit_video_yuv(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                                  int stride_y, int stride_u, int stride_v, int w,
                                  int h) {
  if (!y || !u || !v || w <= 0 || h <= 0) {
    return false;
  }
  sws_ = sws_getCachedContext(sws_, w, h, AV_PIX_FMT_YUV420P, w, h, AV_PIX_FMT_RGBA,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (!sws_) {
    return false;
  }
  video_.w = w;
  video_.h = h;
  video_.format = PixelFormat::Rgba8;
  video_.data.resize(static_cast<size_t>(w) * h * 4);
  const uint8_t* planes[3] = {y, u, v};
  const int strides[3] = {stride_y, stride_u, stride_v};
  uint8_t* out[1] = {video_.data.data()};
  const int out_stride[1] = {w * 4};
  sws_scale(sws_, planes, strides, 0, h, out, out_stride);
  return true;
}

void SoftCanvas::draw_video(const Rect& area) {
  if (video_.data.empty() || area.w <= 0 || area.h <= 0) {
    return;
  }
  // Aspect ratio kept, centered (same rules as platform_sdl::fit_rect).
  int w = area.w;
  int h = static_cast<int>(static_cast<int64_t>(area.w) * video_.h / video_.w);
  if (h > area.h) {
    h = area.h;
    w = static_cast<int>(static_cast<int64_t>(area.h) * video_.w / video_.h);
  }
  draw_texture_data(video_, rect(0, 0, video_.w, video_.h),
                    rect(area.x + (area.w - w) / 2, area.y + (area.h - h) / 2, w, h),
                    rgba(255, 255, 255));
}

void SoftCanvas::clear_video() {
  video_.data.clear();
  video_.w = video_.h = 0;
}

}  // namespace ui
