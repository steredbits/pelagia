// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "player/network_source.h"

#include <chrono>
#include <thread>

#include "player/av_log_bridge.h"
#include "util/log.h"

namespace player {

namespace {

int64_t mono_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

const char* av_err(int err, char* buf, size_t size) {
  av_strerror(err, buf, size);
  return buf;
}

constexpr int kMaxStreams = 16;

}  // namespace

NetworkSource::NetworkSource(StreamLocator* locator, const NetworkSourceOptions& options)
    : locator_(locator), options_(options) {
  for (int& m : map_) {
    m = -1;
  }
  ensure_ffmpeg_log_bridge();
}

NetworkSource::~NetworkSource() {
  close();
}

void NetworkSource::set_interrupt(InterruptFn fn, void* ctx) {
  interrupt_fn_ = fn;
  interrupt_ctx_ = ctx;
}

bool NetworkSource::externally_interrupted() const {
  return interrupt_fn_ && interrupt_fn_(interrupt_ctx_);
}

int NetworkSource::ff_interrupt(void* opaque) {
  NetworkSource* self = static_cast<NetworkSource*>(opaque);
  if (self->externally_interrupted()) {
    return 1;
  }
  const int64_t deadline = self->deadline_ms_.load();
  if (deadline > 0 && mono_ms() > deadline) {
    self->timed_out_ = true;
    return 1;
  }
  return 0;
}

bool NetworkSource::open() {
  return ctx_ != nullptr || open_at(options_.start_ms);
}

void NetworkSource::close() {
  close_stream();
}

std::string NetworkSource::session_id() const {
  std::lock_guard<std::mutex> lock(session_mutex_);
  return session_id_;
}

bool NetworkSource::open_at(int64_t start_ms) {
  const std::string session = locator_->new_session_id();
  StreamRequest request;
  if (!locator_->locate(start_ms, session, &request)) {
    LOG_ERROR("NetworkSource: cannot build the stream request");
    return false;
  }
  {
    // As soon as the request is sent, a job may exist on the server side: the
    // session must be released even if the opening fails.
    std::lock_guard<std::mutex> lock(session_mutex_);
    session_id_ = session;
  }

  ctx_ = avformat_alloc_context();
  if (!ctx_) {
    close_stream();
    return false;
  }
  ctx_->interrupt_callback.callback = &NetworkSource::ff_interrupt;
  ctx_->interrupt_callback.opaque = this;

  AVDictionary* opts = nullptr;
  if (!request.headers.empty()) {
    av_dict_set(&opts, "headers", request.headers.c_str(), 0);
  }
  av_dict_set(&opts, "user_agent", "Pelagia/0.1", 0);
  // No automatic ffmpeg reconnection: on a stream without
  // Accept-Ranges it would replay the transcoding from the start.
  av_dict_set(&opts, "reconnect", "0", 0);
  av_dict_set(&opts, "protocol_whitelist", "http,https,tcp,tls", 0);
  // Wide probe limits (see NetworkSourceOptions): the probe stops
  // by itself when audio and video are known, startup is only slowed
  // down if the audio is really late.
  av_dict_set_int(&opts, "probesize", options_.probe_bytes, 0);
  av_dict_set_int(&opts, "analyzeduration", options_.probe_duration_us, 0);

  LOG_INFO("NetworkSource: opening the stream at %lld ms: %s",
           static_cast<long long>(start_ms), request.log_url.c_str());
  const int64_t t0 = mono_ms();
  timed_out_ = false;
  deadline_ms_ = t0 + options_.open_timeout_ms;
  int err = avformat_open_input(&ctx_, request.url.c_str(), nullptr, &opts);
  av_dict_free(&opts);
  if (err >= 0) {
    err = avformat_find_stream_info(ctx_, nullptr);
  }
  deadline_ms_ = 0;

  if (err < 0) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    if (externally_interrupted()) {
      LOG_DEBUG("NetworkSource: opening interrupted (stop or more recent seek)");
    } else if (timed_out_) {
      LOG_WARN("NetworkSource: no data after %d ms", options_.open_timeout_ms);
    } else {
      LOG_ERROR("NetworkSource: cannot open the stream: %s",
                av_err(err, buf, sizeof(buf)));
    }
    close_stream();
    return false;
  }

  build_stream_map();
  stream_start_ms_ = start_ms;
  first_pts_ms_ = -1;
  last_media_ms_ = start_ms;
  LOG_INFO("NetworkSource: stream opened in %lld ms",
           static_cast<long long>(mono_ms() - t0));
  return true;
}

// av_find_best_stream ignores an audio stream whose probe did not find the
// parameters (0 channels): on a reopening, its packets would be dropped and the
// player would wait for audio forever. The decoder of the first stream
// (same parameters: same server, same transcoding) can decode them, so
// the audio stream is kept as is.
int NetworkSource::pick_stream(AVMediaType type) const {
  const int best = av_find_best_stream(ctx_, type, -1, -1, nullptr, 0);
  if (best >= 0 || type != AVMEDIA_TYPE_AUDIO) {
    return best;
  }
  for (unsigned i = 0; i < ctx_->nb_streams; ++i) {
    if (ctx_->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
      continue;
    }
    if (ref_audio_index_ >= 0) {
      LOG_WARN("NetworkSource: audio parameters not found after the probe (%lld KB, %lld s),"
               "stream no. %u kept with the first stream's decoder",
               static_cast<long long>(options_.probe_bytes / 1000),
               static_cast<long long>(options_.probe_duration_us / 1000000), i);
      return static_cast<int>(i);
    }
    LOG_WARN("NetworkSource: audio track parameters not found after the probe (%lld KB,"
             "%lld s): playing without sound",
             static_cast<long long>(options_.probe_bytes / 1000),
             static_cast<long long>(options_.probe_duration_us / 1000000));
    break;
  }
  return -1;
}

void NetworkSource::build_stream_map() {
  for (int& m : map_) {
    m = -1;
  }
  const int video = pick_stream(AVMEDIA_TYPE_VIDEO);
  const int audio = pick_stream(AVMEDIA_TYPE_AUDIO);
  cur_video_index_ = video;
  cur_audio_index_ = audio;
  if (ref_video_index_ < 0 && ref_audio_index_ < 0) {
    // First opening: these are the indexes on which the player opens
    // its decoders (it uses the same av_find_best_stream).
    ref_video_index_ = video;
    ref_audio_index_ = audio;
  }
  if (video >= 0 && video < kMaxStreams) {
    map_[video] = ref_video_index_;
  }
  if (audio >= 0 && audio < kMaxStreams) {
    map_[audio] = ref_audio_index_;
  }
}

bool NetworkSource::stream_info(AVMediaType type, AVCodecParameters* par,
                                AVRational* time_base, AVRational* frame_rate) {
  const int index = type == AVMEDIA_TYPE_VIDEO   ? cur_video_index_
                    : type == AVMEDIA_TYPE_AUDIO ? cur_audio_index_
                                                 : -1;
  if (!ctx_ || index < 0 || index >= static_cast<int>(ctx_->nb_streams) ||
      avcodec_parameters_copy(par, ctx_->streams[index]->codecpar) < 0) {
    return false;
  }
  *time_base = ctx_->streams[index]->time_base;
  *frame_rate = ctx_->streams[index]->avg_frame_rate;
  return true;
}

void NetworkSource::close_stream() {
  if (ctx_) {
    avformat_close_input(&ctx_);
  }
  std::string session;
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    session.swap(session_id_);
  }
  if (!session.empty()) {
    locator_->release(session);
  }
}

int NetworkSource::read(AVPacket* pkt) {
  if (!ctx_) {
    return AVERROR(EIO);
  }
  timed_out_ = false;
  deadline_ms_ = mono_ms() + options_.read_timeout_ms;
  const int err = av_read_frame(ctx_, pkt);
  deadline_ms_ = 0;
  if (err >= 0) {
    const int index = pkt->stream_index;
    if (pkt->pts != AV_NOPTS_VALUE && index >= 0 &&
        index < static_cast<int>(ctx_->nb_streams)) {
      const int64_t pts_ms =
          av_rescale_q(pkt->pts, ctx_->streams[index]->time_base, AVRational{1, 1000});
      if (first_pts_ms_ < 0) {
        first_pts_ms_ = pts_ms;
      }
      last_media_ms_ = stream_start_ms_ + (pts_ms - first_pts_ms_);
    }
    pkt->stream_index = (index >= 0 && index < kMaxStreams) ? map_[index] : -1;
    return 0;
  }
  if (externally_interrupted()) {
    return AVERROR_EXIT;
  }
  // A deadline may expire during a call that still returns an already
  // demuxed packet: the error then stays stuck to the I/O context and the
  // following calls return AVERROR_EXIT without waiting. Without an interruption
  // requested by the player, this is always an exceeded timeout: never
  // pass it off as a stop or a seek (the recovery would not happen).
  if (timed_out_ || err == AVERROR_EXIT) {
    LOG_WARN("NetworkSource: no data for %d ms", options_.read_timeout_ms);
    return AVERROR(ETIMEDOUT);
  }
  if (err == AVERROR_EOF && options_.duration_ms > 0 &&
      last_media_ms_ < options_.duration_ms - options_.end_tolerance_ms) {
    LOG_WARN("NetworkSource: premature end of stream at %lld ms (duration %lld ms)",
             static_cast<long long>(last_media_ms_),
             static_cast<long long>(options_.duration_ms));
    return AVERROR(EIO);
  }
  if (err != AVERROR_EOF) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    LOG_WARN("NetworkSource: read error: %s", av_err(err, buf, sizeof(buf)));
  }
  return err;
}

bool NetworkSource::wait_interruptible(int ms) {
  const int64_t end = mono_ms() + ms;
  while (mono_ms() < end) {
    if (externally_interrupted()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return externally_interrupted();
}

bool NetworkSource::seek(int64_t target_ms) {
  // A burst of seeks (key held down) must only start one
  // transcoding: a more recent seek during this delay cancels this one.
  if (wait_interruptible(options_.seek_debounce_ms)) {
    return false;
  }
  // Close and release the job BEFORE reopening: otherwise the server may see
  // the new request while the old job is still running.
  close_stream();
  if (externally_interrupted()) {
    return false;
  }
  return open_at(target_ms);
}

}  // namespace player
