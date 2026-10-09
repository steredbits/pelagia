// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Tests of poster decoding (JPEG, PNG) and PNG encoding.

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <string>

#include "test_framework.h"
#include "ui/image_codec.h"

using ui::Image;

static Image make_gradient(int w, int h) {
  Image img;
  img.w = w;
  img.h = h;
  img.rgba.resize(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      uint8_t* p = &img.rgba[(static_cast<size_t>(y) * w + x) * 4];
      p[0] = static_cast<uint8_t>(x * 255 / (w - 1));
      p[1] = static_cast<uint8_t>(y * 255 / (h - 1));
      p[2] = 80;
      p[3] = 255;
    }
  }
  return img;
}

// JPEG produced by libavcodec's MJPEG encoder (like a Jellyfin poster).
static std::string make_jpeg(int w, int h) {
  const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  ctx->width = w;
  ctx->height = h;
  ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
  ctx->time_base = AVRational{1, 25};
  std::string out;
  if (avcodec_open2(ctx, codec, nullptr) == 0) {
    AVFrame* f = av_frame_alloc();
    f->format = ctx->pix_fmt;
    f->width = w;
    f->height = h;
    av_frame_get_buffer(f, 0);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        f->data[0][y * f->linesize[0] + x] = 200;  // clair
      }
    }
    for (int y = 0; y < h / 2; ++y) {
      for (int x = 0; x < w / 2; ++x) {
        f->data[1][y * f->linesize[1] + x] = 128;
        f->data[2][y * f->linesize[2] + x] = 200;  // reddish
      }
    }
    AVPacket* pkt = av_packet_alloc();
    if (avcodec_send_frame(ctx, f) == 0 && avcodec_receive_packet(ctx, pkt) == 0) {
      out.assign(reinterpret_cast<char*>(pkt->data), pkt->size);
    }
    av_packet_free(&pkt);
    av_frame_free(&f);
  }
  avcodec_free_context(&ctx);
  return out;
}

static void test_fit_within() {
  int w = 0, h = 0;
  ui::fit_within(1000, 1500, 300, 0, &w, &h);
  CHECK(w == 300 && h == 450);
  ui::fit_within(1920, 1080, 400, 400, &w, &h);
  CHECK(w == 400 && h == 225);
  ui::fit_within(100, 150, 300, 450, &w, &h);  // never upscaled
  CHECK(w == 100 && h == 150);
  ui::fit_within(5000, 1, 10, 10, &w, &h);     // never 0
  CHECK(w == 10 && h == 1);
}

static void test_png_roundtrip() {
  const Image src = make_gradient(64, 32);
  std::string png;
  CHECK(ui::encode_png(src, &png));
  CHECK(ui::sniff_image_format(png) == ui::ImageFormat::Png);
  Image back;
  CHECK(ui::decode_image(png, 0, 0, &back));
  CHECK(back.w == 64 && back.h == 32);
  CHECK(back.rgba == src.rgba);  // lossless PNG
  // Downscaling on demand.
  Image small;
  CHECK(ui::decode_image(png, 16, 0, &small));
  CHECK(small.w == 16 && small.h == 8);
}

static void test_jpeg() {
  const std::string jpeg = make_jpeg(120, 180);
  CHECK(!jpeg.empty());
  CHECK(ui::sniff_image_format(jpeg) == ui::ImageFormat::Jpeg);
  Image img;
  CHECK(ui::decode_image(jpeg, 60, 0, &img));
  CHECK(img.w == 60 && img.h == 90);
  const uint8_t* p = &img.rgba[(45 * 60 + 30) * 4];
  CHECK(p[0] > p[1] && p[0] > p[2]);  // red dominant color kept
  CHECK(p[3] == 255);
}

static void test_invalid() {
  Image img;
  CHECK(!ui::decode_image("", 0, 0, &img));
  CHECK(!ui::decode_image("<html>404</html>", 0, 0, &img));
  CHECK(ui::sniff_image_format("RIFF0000WEBPVP8 ") == ui::ImageFormat::WebP);
  // Valid JPEG header but truncated data: refused without crashing.
  std::string truncated = make_jpeg(64, 64).substr(0, 40);
  CHECK(!ui::decode_image(truncated, 0, 0, &img));
  Image empty;
  std::string out;
  CHECK(!ui::encode_png(empty, &out));
}

int main() {
  test_fit_within();
  test_png_roundtrip();
  test_jpeg();
  test_invalid();
  return testfw::test_failures();
}
