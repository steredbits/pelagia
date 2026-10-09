// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "player/decoder.h"

#include <chrono>

#include "util/log.h"

namespace player {

namespace {

double chrono_now_ms() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration<double, std::milli>(now).count();
}

}  // namespace

// --- VideoDecoder -----------------------------------------------------------

bool VideoDecoder::open(const AVStream* stream) {
  return open(stream->codecpar, stream->time_base, stream->avg_frame_rate);
}

bool VideoDecoder::open(const AVCodecParameters* par, AVRational time_base,
                        AVRational frame_rate) {
  const AVCodec* codec = avcodec_find_decoder(par->codec_id);
  if (!codec) {
    LOG_ERROR("VideoDecoder: codec %d not supported", static_cast<int>(par->codec_id));
    return false;
  }
  codec_ = avcodec_alloc_context3(codec);
  if (!codec_ || avcodec_parameters_to_context(codec_, par) < 0 ||
      avcodec_open2(codec_, codec, nullptr) < 0) {
    LOG_ERROR("VideoDecoder: cannot open codec %s", codec->name);
    close();
    return false;
  }
  time_base_ = time_base;
  const AVRational fr = frame_rate;
  frame_duration_ms_ = (fr.num > 0) ? (1000LL * fr.den / fr.num) : 40;
  last_pts_ms_ = 0;
  LOG_INFO("VideoDecoder : %s %dx%d", codec->name, codec_->width, codec_->height);
  return true;
}

void VideoDecoder::close() {
  if (sws_) {
    sws_freeContext(sws_);
    sws_ = nullptr;
  }
  if (codec_) {
    avcodec_free_context(&codec_);
  }
}

int VideoDecoder::send(const AVPacket* pkt) {
  const double t0 = chrono_now_ms();
  const int err = avcodec_send_packet(codec_, pkt);
  busy_ms_ += chrono_now_ms() - t0;
  return err;
}

AVFrame* VideoDecoder::receive(int* status) {
  AVFrame* frame = av_frame_alloc();
  const double t0 = chrono_now_ms();
  const int err = avcodec_receive_frame(codec_, frame);
  if (err < 0) {
    busy_ms_ += chrono_now_ms() - t0;
    av_frame_free(&frame);
    *status = err;
    return nullptr;
  }

  if (frame->format != AV_PIX_FMT_YUV420P) {
    // Conversion to YUV420p - only if the source is not already in it
    // (regular 8-bit H.264 outputs YUV420p natively).
    AVFrame* converted = av_frame_alloc();
    converted->format = AV_PIX_FMT_YUV420P;
    converted->width = frame->width;
    converted->height = frame->height;
    if (av_frame_get_buffer(converted, 0) < 0) {
      LOG_ERROR("VideoDecoder: cannot allocate the converted frame");
      av_frame_free(&converted);
      av_frame_free(&frame);
      busy_ms_ += chrono_now_ms() - t0;
      *status = AVERROR(ENOMEM);
      return nullptr;
    }
    sws_ = sws_getCachedContext(sws_, frame->width, frame->height,
                                static_cast<AVPixelFormat>(frame->format),
                                frame->width, frame->height, AV_PIX_FMT_YUV420P,
                                SWS_BILINEAR, nullptr, nullptr, nullptr);
    sws_scale(sws_, frame->data, frame->linesize, 0, frame->height,
              converted->data, converted->linesize);
    converted->pts = frame->pts;
    converted->best_effort_timestamp = frame->best_effort_timestamp;
    av_frame_free(&frame);
    frame = converted;
  }

  busy_ms_ += chrono_now_ms() - t0;
  *status = 0;
  return frame;
}

void VideoDecoder::flush() {
  if (codec_) {
    avcodec_flush_buffers(codec_);
  }
}

int64_t VideoDecoder::frame_pts_ms(const AVFrame* frame) {
  int64_t pts = frame->best_effort_timestamp;
  if (pts == AV_NOPTS_VALUE) {
    pts = frame->pts;
  }
  if (pts == AV_NOPTS_VALUE) {
    last_pts_ms_ += frame_duration_ms_;
    return last_pts_ms_;
  }
  last_pts_ms_ = av_rescale_q(pts, time_base_, AVRational{1, 1000});
  return last_pts_ms_;
}

// --- AudioDecoder -----------------------------------------------------------

bool AudioDecoder::open(const AVStream* stream, int out_sample_rate,
                        int out_channels) {
  return open(stream->codecpar, stream->time_base, out_sample_rate, out_channels);
}

bool AudioDecoder::open(const AVCodecParameters* par, AVRational time_base,
                        int out_sample_rate, int out_channels) {
  const AVCodec* codec = avcodec_find_decoder(par->codec_id);
  if (!codec) {
    LOG_ERROR("AudioDecoder: codec %d not supported", static_cast<int>(par->codec_id));
    return false;
  }
  codec_ = avcodec_alloc_context3(codec);
  if (!codec_ || avcodec_parameters_to_context(codec_, par) < 0 ||
      avcodec_open2(codec_, codec, nullptr) < 0) {
    LOG_ERROR("AudioDecoder: cannot open codec %s", codec->name);
    close();
    return false;
  }
  time_base_ = time_base;
  out_sample_rate_ = out_sample_rate;
  out_channels_ = out_channels;

  AVChannelLayout out_layout;
  av_channel_layout_default(&out_layout, out_channels);
  AVChannelLayout in_layout = codec_->ch_layout;
  if (in_layout.nb_channels == 0) {
    av_channel_layout_default(&in_layout, 2);
  }
  const int err = swr_alloc_set_opts2(
      &swr_, &out_layout, AV_SAMPLE_FMT_S16, out_sample_rate, &in_layout,
      codec_->sample_fmt, codec_->sample_rate, 0, nullptr);
  if (err < 0 || swr_init(swr_) < 0) {
    LOG_ERROR("AudioDecoder: swresample initialization failed");
    close();
    return false;
  }
  next_pts_ms_ = 0;
  LOG_INFO("AudioDecoder : %s %d Hz %d canaux -> S16 %d Hz %d canaux",
           codec->name, codec_->sample_rate, codec_->ch_layout.nb_channels,
           out_sample_rate, out_channels);
  return true;
}

void AudioDecoder::close() {
  if (swr_) {
    swr_free(&swr_);
  }
  if (codec_) {
    avcodec_free_context(&codec_);
  }
}

int AudioDecoder::send(const AVPacket* pkt) {
  const double t0 = chrono_now_ms();
  const int err = avcodec_send_packet(codec_, pkt);
  busy_ms_ += chrono_now_ms() - t0;
  return err;
}

bool AudioDecoder::convert_frame(const AVFrame* frame, AudioChunk* out) {
  const int in_samples = frame ? frame->nb_samples : 0;
  const int64_t max_out = av_rescale_rnd(
      swr_get_delay(swr_, codec_->sample_rate) + in_samples, out_sample_rate_,
      codec_->sample_rate > 0 ? codec_->sample_rate : out_sample_rate_,
      AV_ROUND_UP);
  if (max_out <= 0) {
    return false;
  }
  const size_t bytes_per_sample = sizeof(int16_t) * out_channels_;
  uint8_t* buf = static_cast<uint8_t*>(av_malloc(max_out * bytes_per_sample));
  if (!buf) {
    return false;
  }
  const uint8_t** in_data =
      frame ? const_cast<const uint8_t**>(frame->extended_data) : nullptr;
  const int got = swr_convert(swr_, &buf, static_cast<int>(max_out), in_data,
                              in_samples);
  if (got <= 0) {
    av_free(buf);
    return false;
  }

  out->data = buf;
  out->bytes = static_cast<size_t>(got) * bytes_per_sample;
  out->duration_ms = 1000LL * got / out_sample_rate_;

  int64_t pts = frame ? frame->best_effort_timestamp : AV_NOPTS_VALUE;
  if (frame && pts == AV_NOPTS_VALUE) {
    pts = frame->pts;
  }
  if (pts != AV_NOPTS_VALUE) {
    out->pts_ms = av_rescale_q(pts, time_base_, AVRational{1, 1000});
  } else {
    out->pts_ms = next_pts_ms_;
  }
  next_pts_ms_ = out->pts_ms + out->duration_ms;
  return true;
}

bool AudioDecoder::receive(AudioChunk* out, int* status) {
  AVFrame* frame = av_frame_alloc();
  const double t0 = chrono_now_ms();
  const int err = avcodec_receive_frame(codec_, frame);
  if (err < 0) {
    // At the end of the drain, flushes what is left in swresample.
    bool got_tail = false;
    if (err == AVERROR_EOF) {
      got_tail = convert_frame(nullptr, out);
    }
    busy_ms_ += chrono_now_ms() - t0;
    av_frame_free(&frame);
    *status = got_tail ? 0 : err;
    return got_tail;
  }
  const bool ok = convert_frame(frame, out);
  busy_ms_ += chrono_now_ms() - t0;
  av_frame_free(&frame);
  *status = ok ? 0 : AVERROR(EAGAIN);
  return ok;
}

void AudioDecoder::flush() {
  if (codec_) {
    avcodec_flush_buffers(codec_);
  }
  // Starts again from a clean converter: pending samples date
  // from before the seek.
  if (swr_) {
    swr_close(swr_);
    swr_init(swr_);
  }
}

}  // namespace player
