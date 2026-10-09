// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "media_synth.h"

#include <cmath>
#include <cstdio>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

namespace testmedia {

namespace {

bool encode_and_write(AVFormatContext* oc, AVCodecContext* ctx, AVStream* st,
                      AVFrame* frame) {
  if (avcodec_send_frame(ctx, frame) < 0) {
    return false;
  }
  AVPacket* pkt = av_packet_alloc();
  while (true) {
    const int r = avcodec_receive_packet(ctx, pkt);
    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) {
      break;
    }
    if (r < 0) {
      av_packet_free(&pkt);
      return false;
    }
    av_packet_rescale_ts(pkt, ctx->time_base, st->time_base);
    pkt->stream_index = st->index;
    if (av_interleaved_write_frame(oc, pkt) < 0) {
      av_packet_free(&pkt);
      return false;
    }
  }
  av_packet_free(&pkt);
  return true;
}

}  // namespace

bool synth_media(const char* path, const SynthSpec& spec) {
  AVFormatContext* oc = nullptr;
  if (avformat_alloc_output_context2(&oc, nullptr, "mp4", path) < 0) {
    return false;
  }

  AVCodecContext* vctx = nullptr;
  AVCodecContext* actx = nullptr;
  AVStream* vst = nullptr;
  AVStream* ast = nullptr;
  bool ok = true;

  if (spec.with_video) {
    const AVCodec* vcodec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    vctx = vcodec ? avcodec_alloc_context3(vcodec) : nullptr;
    if (!vctx) {
      ok = false;
    } else {
      vctx->width = spec.width;
      vctx->height = spec.height;
      vctx->pix_fmt = AV_PIX_FMT_YUV420P;
      vctx->time_base = AVRational{1, spec.fps};
      vctx->bit_rate = 400000;
      vctx->gop_size = 12;
      if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
        vctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
      }
      if (avcodec_open2(vctx, vcodec, nullptr) < 0) {
        ok = false;
      } else {
        vst = avformat_new_stream(oc, nullptr);
        vst->time_base = vctx->time_base;
        avcodec_parameters_from_context(vst->codecpar, vctx);
      }
    }
  }

  if (ok && spec.with_audio) {
    const AVCodec* acodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    actx = acodec ? avcodec_alloc_context3(acodec) : nullptr;
    if (!actx) {
      ok = false;
    } else {
      actx->sample_rate = spec.sample_rate;
      actx->sample_fmt = AV_SAMPLE_FMT_FLTP;
      av_channel_layout_default(&actx->ch_layout, 2);
      actx->bit_rate = 96000;
      actx->time_base = AVRational{1, spec.sample_rate};
      if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
        actx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
      }
      if (avcodec_open2(actx, acodec, nullptr) < 0) {
        ok = false;
      } else {
        ast = avformat_new_stream(oc, nullptr);
        ast->time_base = AVRational{1, spec.sample_rate};
        avcodec_parameters_from_context(ast->codecpar, actx);
      }
    }
  }

  if (ok && (avio_open(&oc->pb, path, AVIO_FLAG_WRITE) < 0 ||
             avformat_write_header(oc, nullptr) < 0)) {
    ok = false;
  }

  // Video: programmed moving test pattern.
  if (ok && vctx) {
    AVFrame* frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = spec.width;
    frame->height = spec.height;
    av_frame_get_buffer(frame, 0);
    const int frames = static_cast<int>(spec.seconds * spec.fps + 0.5);
    for (int i = 0; ok && i < frames; ++i) {
      av_frame_make_writable(frame);
      for (int y = 0; y < spec.height; ++y) {
        for (int x = 0; x < spec.width; ++x) {
          frame->data[0][y * frame->linesize[0] + x] =
              static_cast<uint8_t>(x + y + i * 3);
        }
      }
      for (int y = 0; y < spec.height / 2; ++y) {
        for (int x = 0; x < spec.width / 2; ++x) {
          frame->data[1][y * frame->linesize[1] + x] =
              static_cast<uint8_t>(128 + i);
          frame->data[2][y * frame->linesize[2] + x] =
              static_cast<uint8_t>(64 + x);
        }
      }
      frame->pts = i;
      ok = encode_and_write(oc, vctx, vst, frame);
    }
    if (ok) {
      ok = encode_and_write(oc, vctx, vst, nullptr);  // flush
    }
    av_frame_free(&frame);
  }

  // Audio: 440 Hz stereo sine wave.
  if (ok && actx) {
    const int frame_size = actx->frame_size > 0 ? actx->frame_size : 1024;
    AVFrame* frame = av_frame_alloc();
    frame->format = AV_SAMPLE_FMT_FLTP;
    frame->nb_samples = frame_size;
    av_channel_layout_copy(&frame->ch_layout, &actx->ch_layout);
    frame->sample_rate = spec.sample_rate;
    av_frame_get_buffer(frame, 0);
    const long total = static_cast<long>(spec.seconds * spec.sample_rate);
    long pos = 0;
    while (ok && pos < total) {
      av_frame_make_writable(frame);
      float* left = reinterpret_cast<float*>(frame->data[0]);
      float* right = reinterpret_cast<float*>(frame->data[1]);
      for (int i = 0; i < frame_size; ++i) {
        const double t = static_cast<double>(pos + i) / spec.sample_rate;
        const float v = 0.2f * static_cast<float>(std::sin(2.0 * M_PI * 440.0 * t));
        left[i] = v;
        right[i] = v;
      }
      frame->pts = pos;
      ok = encode_and_write(oc, actx, ast, frame);
      pos += frame_size;
    }
    if (ok) {
      ok = encode_and_write(oc, actx, ast, nullptr);  // flush
    }
    av_frame_free(&frame);
  }

  if (ok) {
    ok = av_write_trailer(oc) >= 0;
  }

  if (vctx) {
    avcodec_free_context(&vctx);
  }
  if (actx) {
    avcodec_free_context(&actx);
  }
  if (oc) {
    if (oc->pb) {
      avio_closep(&oc->pb);
    }
    avformat_free_context(oc);
  }
  if (!ok) {
    std::fprintf(stderr, "media_synth: failed to generate %s\n", path);
  }
  return ok;
}

}  // namespace testmedia
