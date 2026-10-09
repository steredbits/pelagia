// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "player/media_source.h"

#include "util/log.h"

namespace player {

bool MediaSource::stream_info(AVMediaType type, AVCodecParameters* par, AVRational* time_base,
                              AVRational* frame_rate) {
  AVFormatContext* ctx = format_context();
  if (!ctx) {
    return false;
  }
  const int index = av_find_best_stream(ctx, type, -1, -1, nullptr, 0);
  if (index < 0 || avcodec_parameters_copy(par, ctx->streams[index]->codecpar) < 0) {
    return false;
  }
  *time_base = ctx->streams[index]->time_base;
  *frame_rate = ctx->streams[index]->avg_frame_rate;
  return true;
}

bool FileSource::open() {
  if (ctx_) {
    return true;
  }
  int err = avformat_open_input(&ctx_, path_, nullptr, nullptr);
  if (err < 0) {
    char buf[128];
    av_strerror(err, buf, sizeof(buf));
    LOG_ERROR("FileSource: cannot open '%s': %s", path_, buf);
    return false;
  }
  err = avformat_find_stream_info(ctx_, nullptr);
  if (err < 0) {
    LOG_ERROR("FileSource: unreadable streams in '%s' (err=%d)", path_, err);
    avformat_close_input(&ctx_);
    return false;
  }
  return true;
}

void FileSource::close() {
  if (ctx_) {
    avformat_close_input(&ctx_);
  }
}

int FileSource::read(AVPacket* pkt) {
  return av_read_frame(ctx_, pkt);
}

bool FileSource::seek(int64_t target_ms) {
  // Keyframe preceding the target: the decoder restarts cleanly.
  const int64_t ts = target_ms * (AV_TIME_BASE / 1000);
  const int err = av_seek_frame(ctx_, -1, ts, AVSEEK_FLAG_BACKWARD);
  if (err < 0) {
    LOG_WARN("FileSource: seek to %lld ms refused (err=%d)",
             static_cast<long long>(target_ms), err);
    return false;
  }
  return true;
}

int64_t FileSource::duration_ms() const {
  if (!ctx_ || ctx_->duration == AV_NOPTS_VALUE) {
    return 0;
  }
  return ctx_->duration / (AV_TIME_BASE / 1000);
}

int64_t FileSource::start_time_ms() const {
  if (!ctx_ || ctx_->start_time == AV_NOPTS_VALUE) {
    return 0;
  }
  return ctx_->start_time / (AV_TIME_BASE / 1000);
}

}  // namespace player
