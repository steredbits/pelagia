// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "ui/image_codec.h"

#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include "util/log.h"

namespace ui {

namespace {

AVCodecID codec_for(ImageFormat format) {
  switch (format) {
    case ImageFormat::Jpeg: return AV_CODEC_ID_MJPEG;
    case ImageFormat::Png: return AV_CODEC_ID_PNG;
    case ImageFormat::WebP: return AV_CODEC_ID_WEBP;
    default: return AV_CODEC_ID_NONE;
  }
}

// "J" formats (JPEG, full range): swscale considers them obsolete and
// reports it in the logs; we pass the standard format + full range.
AVPixelFormat normalize_jpeg_format(AVPixelFormat fmt, bool* full_range) {
  switch (fmt) {
    case AV_PIX_FMT_YUVJ420P: *full_range = true; return AV_PIX_FMT_YUV420P;
    case AV_PIX_FMT_YUVJ422P: *full_range = true; return AV_PIX_FMT_YUV422P;
    case AV_PIX_FMT_YUVJ444P: *full_range = true; return AV_PIX_FMT_YUV444P;
    case AV_PIX_FMT_YUVJ440P: *full_range = true; return AV_PIX_FMT_YUV440P;
    case AV_PIX_FMT_YUVJ411P: *full_range = true; return AV_PIX_FMT_YUV411P;
    default: return fmt;
  }
}

bool decode_frame(AVCodecID id, const std::string& bytes, AVFrame* frame) {
  const AVCodec* codec = avcodec_find_decoder(id);
  if (!codec) {
    return false;
  }
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  AVPacket* pkt = av_packet_alloc();
  // The decoder may read past the end: buffer padded with zeros.
  std::vector<uint8_t> buffer(bytes.size() + AV_INPUT_BUFFER_PADDING_SIZE, 0);
  std::memcpy(buffer.data(), bytes.data(), bytes.size());
  bool ok = false;
  if (ctx && pkt) {
    ctx->thread_count = 1;
    if (avcodec_open2(ctx, codec, nullptr) == 0) {
      pkt->data = buffer.data();
      pkt->size = static_cast<int>(bytes.size());
      if (avcodec_send_packet(ctx, pkt) == 0) {
        int rc = avcodec_receive_frame(ctx, frame);
        if (rc == AVERROR(EAGAIN)) {
          avcodec_send_packet(ctx, nullptr);  // vidange
          rc = avcodec_receive_frame(ctx, frame);
        }
        ok = rc == 0 && frame->width > 0 && frame->height > 0;
      }
    }
  }
  av_packet_free(&pkt);
  avcodec_free_context(&ctx);
  return ok;
}

}  // namespace

ImageFormat sniff_image_format(const std::string& b) {
  const auto at = [&b](size_t i) { return static_cast<unsigned char>(b[i]); };
  if (b.size() >= 3 && at(0) == 0xFF && at(1) == 0xD8 && at(2) == 0xFF) {
    return ImageFormat::Jpeg;
  }
  if (b.size() >= 8 && std::memcmp(b.data(), "\x89PNG\r\n\x1a\n", 8) == 0) {
    return ImageFormat::Png;
  }
  if (b.size() >= 12 && std::memcmp(b.data(), "RIFF", 4) == 0 &&
      std::memcmp(b.data() + 8, "WEBP", 4) == 0) {
    return ImageFormat::WebP;
  }
  return ImageFormat::Unknown;
}

void fit_within(int w, int h, int max_w, int max_h, int* out_w, int* out_h) {
  int ow = w;
  int oh = h;
  if (max_w > 0 && ow > max_w) {
    oh = static_cast<int>((static_cast<int64_t>(oh) * max_w + ow / 2) / ow);
    ow = max_w;
  }
  if (max_h > 0 && oh > max_h) {
    ow = static_cast<int>((static_cast<int64_t>(ow) * max_h + oh / 2) / oh);
    oh = max_h;
  }
  *out_w = ow > 0 ? ow : 1;
  *out_h = oh > 0 ? oh : 1;
}

bool decode_image(const std::string& bytes, int max_w, int max_h, Image* out) {
  const AVCodecID id = codec_for(sniff_image_format(bytes));
  if (id == AV_CODEC_ID_NONE) {
    LOG_DEBUG("Image : format non reconnu (%zu octets)", bytes.size());
    return false;
  }
  AVFrame* frame = av_frame_alloc();
  if (!frame || !decode_frame(id, bytes, frame)) {
    LOG_DEBUG("Image: cannot decode (%zu bytes)", bytes.size());
    av_frame_free(&frame);
    return false;
  }
  int dw = 0;
  int dh = 0;
  fit_within(frame->width, frame->height, max_w, max_h, &dw, &dh);
  bool full_range = frame->color_range == AVCOL_RANGE_JPEG;
  const AVPixelFormat src_fmt =
      normalize_jpeg_format(static_cast<AVPixelFormat>(frame->format), &full_range);
  // Downscaling: SWS_AREA (average, no aliasing); otherwise bicubic.
  const int flags = (dw < frame->width) ? SWS_AREA : SWS_BICUBIC;
  SwsContext* sws = sws_getContext(frame->width, frame->height, src_fmt, dw, dh,
                                   AV_PIX_FMT_RGBA, flags, nullptr, nullptr, nullptr);
  bool ok = false;
  if (sws) {
    int* inv_table = nullptr;
    int* table = nullptr;
    int src_range = 0, dst_range = 0, brightness = 0, contrast = 0, saturation = 0;
    if (sws_getColorspaceDetails(sws, &inv_table, &src_range, &table, &dst_range,
                                 &brightness, &contrast, &saturation) >= 0) {
      sws_setColorspaceDetails(sws, inv_table, full_range ? 1 : 0, table, 1, brightness,
                               contrast, saturation);
    }
    out->w = dw;
    out->h = dh;
    out->rgba.assign(static_cast<size_t>(dw) * dh * 4, 0);
    uint8_t* dst[1] = {out->rgba.data()};
    const int dst_stride[1] = {dw * 4};
    ok = sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dst_stride) ==
         dh;
    sws_freeContext(sws);
  }
  av_frame_free(&frame);
  return ok;
}

bool encode_png(const Image& image, std::string* out) {
  if (image.w <= 0 || image.h <= 0 ||
      image.rgba.size() < static_cast<size_t>(image.w) * image.h * 4) {
    return false;
  }
  const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
  if (!codec) {
    return false;
  }
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  AVFrame* frame = av_frame_alloc();
  AVPacket* pkt = av_packet_alloc();
  bool ok = false;
  if (ctx && frame && pkt) {
    ctx->width = image.w;
    ctx->height = image.h;
    ctx->pix_fmt = AV_PIX_FMT_RGB24;
    ctx->time_base = AVRational{1, 25};
    ctx->compression_level = 9;
    av_opt_set(ctx->priv_data, "pred", "mixed", 0);  // meilleure compression
    frame->format = AV_PIX_FMT_RGB24;
    frame->width = image.w;
    frame->height = image.h;
    if (avcodec_open2(ctx, codec, nullptr) == 0 && av_frame_get_buffer(frame, 0) == 0) {
      for (int y = 0; y < image.h; ++y) {
        const uint8_t* src = &image.rgba[static_cast<size_t>(y) * image.w * 4];
        uint8_t* dst = frame->data[0] + static_cast<size_t>(y) * frame->linesize[0];
        for (int x = 0; x < image.w; ++x) {
          dst[x * 3 + 0] = src[x * 4 + 0];
          dst[x * 3 + 1] = src[x * 4 + 1];
          dst[x * 3 + 2] = src[x * 4 + 2];
        }
      }
      if (avcodec_send_frame(ctx, frame) == 0 && avcodec_receive_packet(ctx, pkt) == 0) {
        out->assign(reinterpret_cast<const char*>(pkt->data), static_cast<size_t>(pkt->size));
        ok = true;
      }
    }
  }
  av_packet_free(&pkt);
  av_frame_free(&frame);
  avcodec_free_context(&ctx);
  return ok;
}

}  // namespace ui
