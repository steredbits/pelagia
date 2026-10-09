// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "player/player.h"

#include <chrono>

#include "util/log.h"

namespace player {

namespace {

uint64_t default_time_fn(void*) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

double chrono_now_ms() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration<double, std::milli>(now).count();
}

void sleep_ms_portable(unsigned ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void release_packet_entry(BoundedQueue<AVPacket*>::Entry* e) {
  if (e->item) {
    av_packet_free(&e->item);
  }
}

// Target audio buffer on the device side (latency vs robustness).
constexpr int64_t kAudioTargetMs = 250;
// Maximum time allowed for the end of the audio to drain before the Ended state.
constexpr uint64_t kEndDrainTimeoutMs = 2000;
// Audio track declared but with no packet: after this delay following the
// first frame, the clock locks on the video (otherwise the picture stays frozen
// waiting for a sound that never comes).
constexpr uint64_t kAudioClockGraceMs = 3000;

}  // namespace

static bool audio_params_differ(const AVCodecParameters* a, const AVCodecParameters* b) {
  return a->codec_id != b->codec_id || a->sample_rate != b->sample_rate ||
         a->ch_layout.nb_channels != b->ch_layout.nb_channels;
}

// --- Opening / life cycle ---------------------------------------------------

Player::~Player() {
  stop();
  avcodec_parameters_free(&audio_params_);
  avcodec_parameters_free(&video_params_);
  avcodec_parameters_free(&audio_pending_);
  avcodec_parameters_free(&video_pending_);
}

bool Player::open(MediaSource* source, const Callbacks& callbacks) {
  source_ = source;
  cb_ = callbacks;
  if (!cb_.time_fn) {
    cb_.time_fn = default_time_fn;
    cb_.time_ctx = nullptr;
  }

  // Before open(): a network opening (up to several tens of
  // seconds of transcoding) must stay interruptible.
  source_->set_interrupt(&Player::source_interrupt, this);
  if (!source_->open() || abort_requested_) {
    return false;  // abort requested during the opening, even if it barely succeeded
  }
  AVFormatContext* ctx = source_->format_context();

  int idx = av_find_best_stream(ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  video_stream_ = idx >= 0 ? idx : -1;
  idx = av_find_best_stream(ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  audio_stream_ = idx >= 0 ? idx : -1;

  if (video_stream_ >= 0 && !vdec_.open(ctx->streams[video_stream_])) {
    video_stream_ = -1;
  }
  if (audio_stream_ >= 0 &&
      !adec_.open(ctx->streams[audio_stream_], cb_.audio_sample_rate,
                  cb_.audio_channels)) {
    audio_stream_ = -1;
  }
  if (video_stream_ < 0 && audio_stream_ < 0) {
    LOG_ERROR("Player: no usable audio or video stream");
    return false;
  }
  // Reference parameters of the decoders, to detect a stream change.
  {
    AVRational tb{1, 1000}, fr{0, 1};
    if (video_stream_ >= 0) {
      video_params_ = avcodec_parameters_alloc();
      if (!source_->stream_info(AVMEDIA_TYPE_VIDEO, video_params_, &tb, &fr)) {
        avcodec_parameters_free(&video_params_);
      }
    }
    if (audio_stream_ >= 0) {
      audio_params_ = avcodec_parameters_alloc();
      if (!source_->stream_info(AVMEDIA_TYPE_AUDIO, audio_params_, &tb, &fr)) {
        avcodec_parameters_free(&audio_params_);
      }
    }
  }

  if (video_stream_ >= 0) {
    video_tb_ = ctx->streams[video_stream_]->time_base;
  }
  if (audio_stream_ >= 0) {
    audio_tb_ = ctx->streams[audio_stream_]->time_base;
  }
  if (cb_.deep_buffer) {
    // ~20 s at 8 Mbit/s: network jitter is absorbed without a shortage.
    video_pkts_.set_limits(2000, 24u * 1024 * 1024);
    audio_pkts_.set_limits(4000, 4u * 1024 * 1024);
  }
  gate_.set_policy(cb_.buffering);
  gate_.reset_for_start();
  buffering_ = gate_.buffering();
  buffering_since_ = mono_ms();

  video_eof_ = (video_stream_ < 0);
  audio_eof_ = (audio_stream_ < 0);
  tracker_.init(source_->timestamps_absolute(), -source_->start_time_ms());
  tracker_.on_seek(cb_.start_position_ms);
  if (cb_.start_position_ms > 0) {
    last_seek_target_ = cb_.start_position_ms;
  }
  last_position_ms_ = cb_.start_position_ms;
  clock_.init(cb_.time_fn, cb_.time_ctx);
  consumed_serial_ = serial_.load();

  LOG_INFO("Player: duration %lld ms, video=%d, audio=%d",
           static_cast<long long>(source_->duration_ms()),
           video_stream_ >= 0, audio_stream_ >= 0);
  return true;
}

bool Player::start() {
  if (!source_) {
    return false;
  }
  if (started_.exchange(true)) {
    return true;
  }
  quit_ = false;
  state_ = PlayerState::Playing;
  failure_ = FailureKind::None;
  data_wait_since_ = mono_ms();
  if (cb_.audio) {
    cb_.audio->pause(false);
  }
  demux_thread_ = std::thread(&Player::demux_loop, this);
  if (video_stream_ >= 0) {
    video_thread_ = std::thread(&Player::video_loop, this);
  }
  if (audio_stream_ >= 0) {
    audio_thread_ = std::thread(&Player::audio_loop, this);
  }
  return true;
}

void Player::stop() {
  if (!started_.exchange(false)) {
    return;
  }
  quit_ = true;
  video_pkts_.abort();
  audio_pkts_.abort();
  video_frames_.abort();
  audio_frames_.abort();
  if (demux_thread_.joinable()) {
    demux_thread_.join();
  }
  if (video_thread_.joinable()) {
    video_thread_.join();
  }
  if (audio_thread_.joinable()) {
    audio_thread_.join();
  }
  drain_queues();
  state_ = PlayerState::Stopped;
}

void Player::drain_queues() {
  video_pkts_.flush(release_packet_entry);
  audio_pkts_.flush(release_packet_entry);
  video_frames_.flush([](BoundedQueue<VideoItem>::Entry* e) {
    if (e->item.frame) {
      av_frame_free(&e->item.frame);
    }
  });
  audio_frames_.flush([](BoundedQueue<AudioItem>::Entry* e) {
    if (e->item.chunk.data) {
      av_free(e->item.chunk.data);
    }
  });
  drop_pending_items();
}

// --- Demux thread -------------------------------------------------------------

void Player::demux_loop() {
  while (!quit_) {
    bool do_seek = false;
    int64_t target = 0;
    {
      std::lock_guard<std::mutex> lock(seek_mutex_);
      if (seek_pending_) {
        seek_pending_ = false;
        seek_flag_ = false;
        target = seek_target_ms_;
        do_seek = true;
      }
    }
    if (do_seek) {
      perform_seek(target, false);
      continue;
    }
    if (demux_eof_sent_) {
      sleep_ms_portable(10);
      continue;
    }

    AVPacket* pkt = av_packet_alloc();
    const int err = source_->read(pkt);
    const int serial = serial_.load();
    if (err < 0) {
      av_packet_free(&pkt);
      if (err == AVERROR(EAGAIN)) {
        sleep_ms_portable(5);
        continue;
      }
      if (err == AVERROR_EXIT) {
        // Read interrupted by a stop or a seek: handled at the top of the
        // loop. Never an empty loop if the interruption is not followed.
        if (!seek_flag_ && !quit_) {
          sleep_ms_portable(5);
        }
        continue;
      }
      if (err != AVERROR_EOF && source_->recoverable() && cb_.recovery.enabled()) {
        if (!recover_source()) {
          LOG_ERROR("Player: stream lost, %d recovery attempts without success",
                    cb_.recovery.max_attempts);
          fail(FailureKind::Network);
          demux_eof_sent_ = true;  // nothing left to read; a seek will restart
        }
        continue;
      }
      if (err != AVERROR_EOF) {
        LOG_WARN("Player: source read error (err=%d)", err);
      }
      push_eof_markers(serial);
      demux_eof_sent_ = true;
      continue;
    }

    if (pkt->stream_index == video_stream_) {
      push_packet(&video_pkts_, pkt, serial,
                  packet_duration_ms(pkt, video_tb_, &video_prev_dts_ms_));
    } else if (pkt->stream_index == audio_stream_) {
      push_packet(&audio_pkts_, pkt, serial,
                  packet_duration_ms(pkt, audio_tb_, &audio_prev_dts_ms_));
    } else {
      av_packet_free(&pkt);
    }
  }
}

// Non-blocking push in a loop: if a seek arrives while the queues are
// full, the packet is dropped (it belongs to the stale generation) and
// the demux thread stays free to process the seek. A blocking push here
// would be a deadlock: the queue flush happens in this same thread.
int64_t Player::packet_duration_ms(const AVPacket* pkt, AVRational time_base,
                                   int64_t* prev_dts_ms) {
  const AVRational ms = {1, 1000};
  int64_t duration = 0;
  if (pkt->duration > 0) {
    duration = av_rescale_q(pkt->duration, time_base, ms);
  }
  if (pkt->dts != AV_NOPTS_VALUE) {
    const int64_t dts = av_rescale_q(pkt->dts, time_base, ms);
    if (duration <= 0 && *prev_dts_ms >= 0 && dts > *prev_dts_ms) {
      duration = dts - *prev_dts_ms;
    }
    *prev_dts_ms = dts;
  }
  return duration;
}

bool Player::push_packet(BoundedQueue<AVPacket*>* queue, AVPacket* pkt,
                         int serial, int64_t duration_ms) {
  while (!quit_) {
    if (queue->try_push(pkt, static_cast<size_t>(pkt->size), serial, duration_ms)) {
      return true;
    }
    {
      std::lock_guard<std::mutex> lock(seek_mutex_);
      if (seek_pending_) {
        break;
      }
    }
    sleep_ms_portable(5);
  }
  av_packet_free(&pkt);
  return false;
}

void Player::push_eof_markers(int serial) {
  // Drain marker (null packet) for each active decoder.
  bool video_sent = (video_stream_ < 0);
  bool audio_sent = (audio_stream_ < 0);
  while (!quit_ && (!video_sent || !audio_sent)) {
    if (!video_sent && video_pkts_.try_push(nullptr, 0, serial)) {
      video_sent = true;
    }
    if (!audio_sent && audio_pkts_.try_push(nullptr, 0, serial)) {
      audio_sent = true;
    }
    if (video_sent && audio_sent) {
      break;
    }
    {
      std::lock_guard<std::mutex> lock(seek_mutex_);
      if (seek_pending_) {
        break;  // the generation changes, the markers would be dropped
      }
    }
    sleep_ms_portable(5);
  }
}

bool Player::wait_demux(int ms) {
  for (int waited = 0; waited < ms; waited += 10) {
    if (quit_ || seek_flag_) {
      return true;
    }
    sleep_ms_portable(10);
  }
  return quit_ || seek_flag_;
}

// Network drop: a few spaced attempts, each reopening the stream at
// the current playback position (playback continues on the reserve
// while waiting). True if the stream is restored, or if a seek / the stop
// takes over.
bool Player::recover_source() {
  const RetryPolicy& policy = cb_.recovery;
  for (int attempt = 1; attempt <= policy.max_attempts; ++attempt) {
    const int delay = policy.delay_ms(attempt);
    LOG_WARN("Player: network drop, attempt %d/%d in %d ms", attempt,
             policy.max_attempts, delay);
    if (wait_demux(delay)) {
      return true;
    }
    const int64_t target = clamp_seek_target(last_position_ms_.load(), duration_ms());
    if (perform_seek(target, true)) {
      stat_reconnections_.fetch_add(1);
      LOG_INFO("Player: stream restored at %lld ms (attempt %d)",
               static_cast<long long>(target), attempt);
      return true;
    }
    if (quit_ || seek_flag_) {
      return true;
    }
  }
  return false;
}

// recovery: restart decided by the demux (drop) and not by the
// user; the consumer is notified by restart_pending_.
bool Player::perform_seek(int64_t target_ms, bool recovery) {
  // New generation first: the decoders now drop any
  // packet/frame carrying the old serial, flush or not.
  const int new_serial = serial_.fetch_add(1) + 1;
  if (recovery) {
    last_seek_target_ = target_ms;
    min_valid_serial_ = new_serial;
    restart_pending_ = true;
  }
  {
    std::lock_guard<std::mutex> lock(seek_mutex_);
    tracker_.on_seek(target_ms);
  }
  video_pkts_.flush(release_packet_entry);
  audio_pkts_.flush(release_packet_entry);
  video_frames_.flush([](BoundedQueue<VideoItem>::Entry* e) {
    if (e->item.frame) {
      av_frame_free(&e->item.frame);
    }
  });
  audio_frames_.flush([](BoundedQueue<AudioItem>::Entry* e) {
    if (e->item.chunk.data) {
      av_free(e->item.chunk.data);
    }
  });
  // Before the network reopening (several seconds): an end of stream
  // from before the seek must not pass for "source exhausted".
  video_eof_ = (video_stream_ < 0);
  audio_eof_ = (audio_stream_ < 0);
  demux_eof_sent_ = false;
  video_prev_dts_ms_ = -1;
  audio_prev_dts_ms_ = -1;

  if (!source_->seek(target_ms)) {
    if (seek_flag_ || quit_) {
      // A more recent seek (or the stop) interrupted this one: only the
      // last one counts, it will be handled on the next turn.
      LOG_DEBUG("Player: seek to %lld ms replaced", static_cast<long long>(target_ms));
      return false;
    }
    if (!recovery) {
      LOG_WARN("Player: seek to %lld ms failed", static_cast<long long>(target_ms));
    }
    return false;
  }
  note_stream_change();  // before the first packet of the new generation
  force_present_ = true;  // when paused, present the first frame of the seek
  data_wait_since_ = mono_ms();  // stream reopened: the data timeout is running

  PlayerState expected = PlayerState::Ended;
  state_.compare_exchange_strong(expected, PlayerState::Playing);
  expected = PlayerState::Failed;
  if (state_.compare_exchange_strong(expected, PlayerState::Playing)) {
    failure_ = FailureKind::None;
  }
  LOG_DEBUG("Player: seek -> %lld ms (generation %d)",
            static_cast<long long>(target_ms), serial_.load());
  return true;
}

// Demux thread, after the source reopening: does the new stream (other audio
// track) have other parameters than the decoders'? If so they
// are published for the decoding threads. Incomplete parameters (the probe
// did not find the rate or the channels) replace nothing: the decoder
// in place is kept, like when resuming a stream without audio parameters.
void Player::note_stream_change() {
  AVCodecParameters* par = avcodec_parameters_alloc();
  AVRational tb{1, 1000}, fr{0, 1};
  if (audio_params_ && source_->stream_info(AVMEDIA_TYPE_AUDIO, par, &tb, &fr) &&
      par->sample_rate > 0 && par->ch_layout.nb_channels > 0 &&
      audio_params_differ(audio_params_, par)) {
    LOG_INFO("Player: new audio track (%s %d Hz %d channels -> %s %d Hz %d channels)",
             avcodec_get_name(audio_params_->codec_id), audio_params_->sample_rate,
             audio_params_->ch_layout.nb_channels, avcodec_get_name(par->codec_id),
             par->sample_rate, par->ch_layout.nb_channels);
    {
      std::lock_guard<std::mutex> lock(codec_mutex_);
      if (!audio_pending_) {
        audio_pending_ = avcodec_parameters_alloc();
      }
      avcodec_parameters_copy(audio_pending_, par);
      audio_pending_tb_ = tb;
      ++audio_codec_version_;
    }
    avcodec_parameters_copy(audio_params_, par);
    audio_tb_ = tb;
  }
  if (video_params_ && source_->stream_info(AVMEDIA_TYPE_VIDEO, par, &tb, &fr) &&
      par->codec_id != video_params_->codec_id) {
    LOG_INFO("Player: new video codec (%s -> %s)",
             avcodec_get_name(video_params_->codec_id), avcodec_get_name(par->codec_id));
    {
      std::lock_guard<std::mutex> lock(codec_mutex_);
      if (!video_pending_) {
        video_pending_ = avcodec_parameters_alloc();
      }
      avcodec_parameters_copy(video_pending_, par);
      video_pending_tb_ = tb;
      video_pending_fr_ = fr;
      ++video_codec_version_;
    }
    avcodec_parameters_copy(video_params_, par);
    video_tb_ = tb;
  }
  avcodec_parameters_free(&par);
}

// Audio thread, at the first packet of a generation: applies the parameters
// published by the demux. False if nothing changed (a simple flush afterwards).
bool Player::reopen_audio_decoder() {
  AVCodecParameters* par = avcodec_parameters_alloc();
  AVRational tb{1, 1000};
  {
    std::lock_guard<std::mutex> lock(codec_mutex_);
    if (audio_codec_version_ == audio_codec_applied_ || !audio_pending_) {
      avcodec_parameters_free(&par);
      return false;
    }
    audio_codec_applied_ = audio_codec_version_;
    avcodec_parameters_copy(par, audio_pending_);
    tb = audio_pending_tb_;
  }
  adec_.close();
  const bool ok = adec_.open(par, tb, cb_.audio_sample_rate, cb_.audio_channels);
  audio_decoder_ok_ = ok;
  if (ok) {
    stat_codec_reopens_.fetch_add(1);
  } else {
    LOG_ERROR("Player: cannot open the audio decoder of the new track: playing without sound");
  }
  avcodec_parameters_free(&par);
  return true;
}

bool Player::reopen_video_decoder() {
  AVCodecParameters* par = avcodec_parameters_alloc();
  AVRational tb{1, 1000}, fr{0, 1};
  {
    std::lock_guard<std::mutex> lock(codec_mutex_);
    if (video_codec_version_ == video_codec_applied_ || !video_pending_) {
      avcodec_parameters_free(&par);
      return false;
    }
    video_codec_applied_ = video_codec_version_;
    avcodec_parameters_copy(par, video_pending_);
    tb = video_pending_tb_;
    fr = video_pending_fr_;
  }
  vdec_.close();
  const bool ok = vdec_.open(par, tb, fr);
  video_decoder_ok_ = ok;
  if (ok) {
    stat_codec_reopens_.fetch_add(1);
  } else {
    LOG_ERROR("Player: cannot open the video decoder of the new stream");
  }
  avcodec_parameters_free(&par);
  return true;
}

bool Player::source_interrupt(void* ctx) {
  const Player* self = static_cast<const Player*>(ctx);
  // abort_open() only concerns the opening: once started, a flag
  // left raised would interrupt each read.
  return self->quit_.load() || self->seek_flag_.load() ||
         (!self->started_.load() && self->abort_requested_.load());
}

// --- Decoding threads ---------------------------------------------------------

void Player::video_loop() {
  int cur_serial = -1;
  while (true) {
    BoundedQueue<AVPacket*>::Entry e;
    if (!video_pkts_.pop(&e)) {
      return;  // abort (stop)
    }
    AVPacket* pkt = e.item;
    if (e.serial != serial_.load()) {
      if (pkt) {
        av_packet_free(&pkt);
      }
      continue;
    }
    if (e.serial != cur_serial) {
      if (!reopen_video_decoder()) {
        vdec_.flush();
      }
      cur_serial = e.serial;
    }
    if (!video_decoder_ok_) {
      if (!pkt) {  // end marker: playback must be able to finish
        VideoItem eof_item;
        eof_item.eof = true;
        video_frames_.push(eof_item, 0, e.serial);
      } else {
        av_packet_free(&pkt);
      }
      continue;
    }

    if (vdec_.send(pkt) < 0 && pkt) {
      LOG_WARN("Player: video packet rejected by the decoder");
      av_packet_free(&pkt);
      continue;
    }
    if (pkt) {
      av_packet_free(&pkt);
    }

    int status = 0;
    while (AVFrame* frame = vdec_.receive(&status)) {
      VideoItem item;
      item.frame = frame;
      item.pts_ms = vdec_.frame_pts_ms(frame);
      stat_video_frames_.fetch_add(1);
      const size_t bytes =
          static_cast<size_t>(frame->width) * frame->height * 3 / 2;
      if (!video_frames_.push(item, bytes, e.serial, vdec_.frame_duration_ms())) {
        av_frame_free(&frame);
        return;  // abort
      }
    }
    if (status == AVERROR_EOF) {
      VideoItem eof_item;
      eof_item.eof = true;
      video_frames_.push(eof_item, 0, e.serial);
      vdec_.flush();  // ready for the next generation
    }
  }
}

void Player::audio_loop() {
  int cur_serial = -1;
  while (true) {
    BoundedQueue<AVPacket*>::Entry e;
    if (!audio_pkts_.pop(&e)) {
      return;
    }
    AVPacket* pkt = e.item;
    if (e.serial != serial_.load()) {
      if (pkt) {
        av_packet_free(&pkt);
      }
      continue;
    }
    if (e.serial != cur_serial) {
      if (!reopen_audio_decoder()) {
        adec_.flush();
      }
      cur_serial = e.serial;
    }
    if (!audio_decoder_ok_) {
      if (!pkt) {  // end marker: playback must be able to finish
        AudioItem eof_item;
        eof_item.eof = true;
        audio_frames_.push(eof_item, 0, e.serial);
      } else {
        av_packet_free(&pkt);
      }
      continue;
    }

    if (adec_.send(pkt) < 0 && pkt) {
      LOG_WARN("Player: audio packet rejected by the decoder");
      av_packet_free(&pkt);
      continue;
    }
    if (pkt) {
      av_packet_free(&pkt);
    }

    int status = 0;
    AudioChunk chunk;
    while (adec_.receive(&chunk, &status)) {
      AudioItem item;
      item.chunk = chunk;
      stat_audio_frames_.fetch_add(1);
      if (!audio_frames_.push(item, chunk.bytes, e.serial, chunk.duration_ms)) {
        av_free(chunk.data);
        return;
      }
      chunk = AudioChunk{};
    }
    if (status == AVERROR_EOF) {
      AudioItem eof_item;
      eof_item.eof = true;
      audio_frames_.push(eof_item, 0, e.serial);
      adec_.flush();
    }
  }
}

// --- Consommation (tick / headless) ------------------------------------------
// tick(), set_paused(), seek_*() and position_ms() are called from a single
// thread (the main loop): the clock and the consumer state are not
// shared beyond it.

int64_t Player::tracker_media_time(int64_t stream_pts_ms) {
  std::lock_guard<std::mutex> lock(seek_mutex_);
  return tracker_.media_time(stream_pts_ms);
}

bool Player::is_stale(int serial) const {
  return serial != serial_.load() || serial < min_valid_serial_.load();
}

void Player::drop_pending_items() {
  if (has_pending_video_ && pending_video_.frame) {
    av_frame_free(&pending_video_.frame);
  }
  has_pending_video_ = false;
  pending_video_ = VideoItem{};
  if (has_pending_audio_ && pending_audio_.chunk.data) {
    av_free(pending_audio_.chunk.data);
  }
  has_pending_audio_ = false;
  pending_audio_ = AudioItem{};
}

void Player::note_serial_change(int serial) {
  if (serial != consumed_serial_) {
    consumed_serial_ = serial;
    clock_.invalidate();
    first_video_shown_ = false;
    audio_wait_since_ = 0;
    audio_clock_waived_ = false;
    end_wait_start_ = 0;
  }
}

// True while waiting for audio to set the clock.
bool Player::audio_clock_expected() const {
  return has_audio() && !audio_eof_.load() && !audio_clock_waived_;
}

void Player::fail(FailureKind kind) {
  failure_ = kind;
  state_ = PlayerState::Failed;
}

uint64_t Player::mono_ms() const {
  return cb_.time_fn ? cb_.time_fn(cb_.time_ctx) : default_time_fn(nullptr);
}

size_t Player::audio_bytes_per_second() const {
  return static_cast<size_t>(cb_.audio_sample_rate) * cb_.audio_channels *
         sizeof(int16_t);
}

bool Player::pump_audio() {
  bool progressed = false;
  const size_t target_bytes =
      audio_bytes_per_second() * kAudioTargetMs / 1000;

  while (true) {
    if (!has_pending_audio_) {
      BoundedQueue<AudioItem>::Entry e;
      if (!audio_frames_.try_pop(&e)) {
        break;
      }
      if (is_stale(e.serial)) {
        if (e.item.chunk.data) {
          av_free(e.item.chunk.data);
        }
        continue;
      }
      note_serial_change(e.serial);
      pending_audio_ = e.item;
      has_pending_audio_ = true;
    }

    if (pending_audio_.eof) {
      audio_eof_ = true;
      has_pending_audio_ = false;
      pending_audio_ = AudioItem{};
      break;
    }

    const int64_t media_pts = tracker_media_time(pending_audio_.chunk.pts_ms);

    if (cb_.audio) {
      if (cb_.audio->queued_bytes() >= target_bytes) {
        break;
      }
      cb_.audio->queue(pending_audio_.chunk.data, pending_audio_.chunk.bytes);
      const size_t queued_after = cb_.audio->queued_bytes();
      const int64_t queued_ms =
          static_cast<int64_t>(queued_after) * 1000 /
          static_cast<int64_t>(audio_bytes_per_second());
      clock_.set(media_pts + pending_audio_.chunk.duration_ms - queued_ms);
    } else {
      // No audio output: chunks are released at the pace of their PTS
      // so that the clock keeps a realistic pace (tests, debugging).
      if (!clock_.valid()) {
        clock_.set(media_pts);
      } else if (clock_.now() < media_pts) {
        break;  // too early to consume this chunk
      } else {
        clock_.set(media_pts);
      }
    }

    av_free(pending_audio_.chunk.data);
    pending_audio_ = AudioItem{};
    has_pending_audio_ = false;
    progressed = true;
  }
  return progressed;
}

bool Player::pump_video(bool* new_frame) {
  const bool paused = (state_.load() == PlayerState::Paused);

  while (true) {
    if (!has_pending_video_) {
      BoundedQueue<VideoItem>::Entry e;
      if (!video_frames_.try_pop(&e)) {
        return false;
      }
      if (is_stale(e.serial)) {
        if (e.item.frame) {
          av_frame_free(&e.item.frame);
        }
        continue;
      }
      note_serial_change(e.serial);
      pending_video_ = e.item;
      has_pending_video_ = true;
    }

    if (pending_video_.eof) {
      video_eof_ = true;
      has_pending_video_ = false;
      pending_video_ = VideoItem{};
      return false;
    }

    const int64_t media_pts = tracker_media_time(pending_video_.pts_ms);

    if (paused) {
      audio_wait_since_ = 0;  // pause does not count in the wait for audio
      if (!force_present_.exchange(false)) {
        return false;
      }
      // Seek during pause: the first frame of the new generation is presented
      // so that the picture matches the position.
    } else {
      FrameAction action = FrameAction::Present;
      const int64_t clock_now = clock_.now();
      if (clock_now < 0 && audio_clock_expected()) {
        // Clock not yet set by the audio: a single primer frame,
        // then wait for the setting so as not to scroll too fast.
        if (first_video_shown_) {
          const uint64_t now = mono_ms();
          if (audio_wait_since_ == 0) {
            audio_wait_since_ = now;
          }
          if (now - audio_wait_since_ < kAudioClockGraceMs) {
            return false;
          }
          // The audio does not come (parameters not found, silent stream): the
          // video leads the clock, as without an audio track. If the audio
          // finally arrives, it takes over again.
          LOG_WARN("Player: no audio %llu ms after the first frame, clock set on"
                   "the video",
                   static_cast<unsigned long long>(kAudioClockGraceMs));
          audio_clock_waived_ = true;
          continue;
        }
      } else {
        action = decide_frame(media_pts, clock_now, SyncPolicy{}, nullptr);
      }
      if (action == FrameAction::Wait) {
        return false;
      }
      if (action == FrameAction::Drop) {
        stat_video_dropped_.fetch_add(1);
        av_frame_free(&pending_video_.frame);
        has_pending_video_ = false;
        pending_video_ = VideoItem{};
        continue;  // frame suivante
      }
    }

    // Present.
    if (cb_.video) {
      cb_.video->submit(pending_video_.frame);
    }
    force_present_ = false;
    first_video_shown_ = true;
    const int64_t clock_now = clock_.now();
    if (clock_now >= 0) {
      // Without an audio track the clock is free-running, set on the video
      // itself: the difference would be meaningless, it is not measured.
      if (has_audio()) {
        int64_t drift = media_pts - clock_now;
        if (drift < 0) {
          drift = -drift;
        }
        stat_drift_measured_ = true;
        int64_t prev = stat_max_drift_.load();
        while (drift > prev &&
               !stat_max_drift_.compare_exchange_weak(prev, drift)) {
        }
      }
    } else if (!audio_clock_expected()) {
      clock_.set(media_pts);  // free-running without audio
    }
    av_frame_free(&pending_video_.frame);
    has_pending_video_ = false;
    pending_video_ = VideoItem{};
    if (new_frame) {
      *new_frame = true;
    }
    return true;  // one frame presented per tick
  }
}

void Player::check_ended() {
  if (seek_in_flight()) {
    return;  // the end flags date from before the seek
  }
  if (!video_eof_.load() || !audio_eof_.load() || has_pending_video_) {
    end_wait_start_ = 0;
    return;
  }
  if (state_.load() != PlayerState::Playing) {
    return;  // when paused, we do not finish
  }
  if (cb_.audio && has_audio() && cb_.audio->queued_bytes() > 0) {
    // Lets the end of the audio play (bounded for dummy drivers).
    if (end_wait_start_ == 0) {
      end_wait_start_ = mono_ms();
      return;
    }
    if (mono_ms() - end_wait_start_ < kEndDrainTimeoutMs) {
      return;
    }
  }
  state_ = PlayerState::Ended;
  apply_output_pause();
  LOG_INFO("Player: end of playback");
}

Player::TickResult Player::tick() {
  TickResult result;
  if (!started_.load()) {
    return result;
  }
  const PlayerState st = state_.load();
  if (st == PlayerState::Stopped) {
    return result;
  }
  if (st == PlayerState::Ended) {
    result.ended = true;
    return result;
  }
  if (restart_pending_.exchange(false)) {
    // Stream restarted after a drop: like a seek on the consumer side.
    drop_pending_items();
    if (cb_.audio) {
      cb_.audio->flush();
    }
    gate_.reset_for_start();
    buffering_ = gate_.buffering();
    buffering_since_ = mono_ms();
  }
  if (st == PlayerState::Failed) {
    apply_output_pause();
    result.failed = true;
    return result;
  }
  result.buffering = update_buffering();
  check_data_wait();
  if (state_.load() == PlayerState::Failed) {
    apply_output_pause();
    result.failed = true;
    return result;
  }
  apply_output_pause();
  if (result.buffering) {
    return result;
  }
  if (st == PlayerState::Playing && has_audio()) {
    pump_audio();
  }
  pump_video(&result.new_video_frame);
  last_position_ms_ = position_ms();
  check_ended();
  if (state_.load() == PlayerState::Ended) {
    result.ended = true;
  }
  return result;
}

// Opening or seek: if the stream is (re)opened but nothing readable
// arrives within the delay, the "Seeking..." state does not last forever.
void Player::check_data_wait() {
  if (cb_.data_wait_timeout_ms <= 0 || state_.load() == PlayerState::Failed) {
    return;
  }
  if (!seek_in_flight() && !gate_.priming()) {
    return;
  }
  const uint64_t since = data_wait_since_.load();
  if (since == 0 || seek_flag_.load()) {
    return;
  }
  const uint64_t waited = mono_ms() - since;
  if (waited < static_cast<uint64_t>(cb_.data_wait_timeout_ms)) {
    return;
  }
  LOG_ERROR("Player: no picture or sound %llu ms after the stream opening at %lld ms:"
            "abandon",
            static_cast<unsigned long long>(waited),
            static_cast<long long>(last_seek_target_.load()));
  fail(FailureKind::Stalled);
}

int64_t Player::buffered_ms() const {
  if (has_audio()) {
    int64_t ms = audio_pkts_.duration_ms() + audio_frames_.duration_ms();
    if (has_pending_audio_ && !pending_audio_.eof) {
      ms += pending_audio_.chunk.duration_ms;
    }
    return ms;
  }
  return video_pkts_.duration_ms() + video_frames_.duration_ms();
}

bool Player::update_buffering() {
  if (!cb_.buffering.enabled()) {
    return false;
  }
  // As long as the demux has not started the seek generation, the queues
  // still contain the old position: we stay in the primer state.
  if (seek_flag_.load() || serial_.load() < min_valid_serial_.load()) {
    buffering_ = true;
    return true;
  }
  BufferingEvent event = BufferingEvent::None;
  const bool queues_full = video_pkts_.full() || audio_pkts_.full();
  const int64_t buffered = buffered_ms();
  const bool hold =
      gate_.update(buffered, demux_eof_sent_.load(), queues_full, &event);
  if (event == BufferingEvent::Started) {
    buffering_since_ = mono_ms();
    LOG_INFO("Player: buffering (reserve exhausted at %lld ms)",
             static_cast<long long>(position_ms()));
  } else if (event == BufferingEvent::Finished) {
    LOG_INFO("Player: playback after %llu ms of waiting (reserve %lld ms)",
             static_cast<unsigned long long>(mono_ms() - buffering_since_),
             static_cast<long long>(buffered));
  }
  buffering_ = hold;
  return hold;
}

// Clock and audio device follow a single state: paused if
// the user paused, if playback is finished or waiting for
// data. Called at every tick: transitions coming from other threads
// (end cancelled by a seek) are caught up.
void Player::apply_output_pause() {
  const bool paused = state_.load() != PlayerState::Playing || buffering_.load();
  if (paused == output_paused_) {
    return;
  }
  output_paused_ = paused;
  clock_.pause(paused);
  if (cb_.audio) {
    cb_.audio->pause(paused);
  }
}

// --- Controls -------------------------------------------------------------------

void Player::set_paused(bool paused) {
  if (paused) {
    PlayerState expected = PlayerState::Playing;
    if (state_.compare_exchange_strong(expected, PlayerState::Paused)) {
      apply_output_pause();
      LOG_INFO("Player: pause at %lld ms",
               static_cast<long long>(position_ms()));
    }
  } else {
    PlayerState expected = PlayerState::Paused;
    if (state_.compare_exchange_strong(expected, PlayerState::Playing)) {
      apply_output_pause();
      LOG_INFO("Player: resume");
    }
  }
}

void Player::toggle_pause() {
  set_paused(state_.load() == PlayerState::Playing);
}

void Player::seek_to(int64_t target_ms) {
  const int64_t clamped = clamp_seek_target(target_ms, duration_ms());
  {
    std::lock_guard<std::mutex> lock(seek_mutex_);
    seek_pending_ = true;
    seek_flag_ = true;
    seek_target_ms_ = clamped;
    data_wait_since_ = 0;  // the stream will be reopened: the delay is not running yet
    // Everything carrying the current generation is now stale, even what
    // the decoders will produce before the demux handles the seek.
    min_valid_serial_ = serial_.load() + 1;
  }
  last_seek_target_ = clamped;
  drop_pending_items();
  gate_.reset_for_start();
  buffering_ = gate_.buffering();
  buffering_since_ = mono_ms();
  if (cb_.audio) {
    cb_.audio->flush();
  }
  LOG_INFO("Player: seek requested -> %lld ms",
           static_cast<long long>(clamped));
}

void Player::seek_relative(int64_t delta_ms) {
  seek_to(position_ms() + delta_ms);
}

bool Player::seek_in_flight() const {
  {
    std::lock_guard<std::mutex> lock(seek_mutex_);
    if (seek_pending_) {
      return true;
    }
  }
  return consumed_serial_ < min_valid_serial_.load();
}

int64_t Player::position_ms() const {
  // During a seek, the clock still describes the old position: a
  // chained seek_relative() must start from the target, not from the clock.
  if (seek_in_flight()) {
    return last_seek_target_.load();
  }
  const int64_t now = clock_.now();
  if (now < 0) {
    const int64_t target = last_seek_target_.load();
    return target >= 0 ? target : 0;
  }
  return clamp_seek_target(now, duration_ms());
}

int64_t Player::duration_ms() const {
  return source_ ? source_->duration_ms() : 0;
}

PlayerStats Player::stats() const {
  PlayerStats s;
  s.video_frames_decoded = stat_video_frames_.load();
  s.audio_frames_decoded = stat_audio_frames_.load();
  s.video_frames_dropped = stat_video_dropped_.load();
  s.max_drift_ms = stat_max_drift_.load();
  s.drift_measured = stat_drift_measured_.load();
  s.reconnections = stat_reconnections_.load();
  s.media_duration_ms = source_ ? source_->duration_ms() : 0;
  return s;
}

// --- Mode headless ------------------------------------------------------------

bool Player::run_headless(PlayerStats* out) {
  if (!start()) {
    return false;
  }
  const double wall_start = chrono_now_ms();
  int64_t last_audio_end_ms = -1;
  int64_t max_drift = 0;
  bool drift_measured = false;

  // Consumption merged by increasing PTS (as the real synchronization
  // would do, but without real-time waiting): the simulated drift then measures
  // the effective alignment of the two streams, not the lead of a queue.
  BoundedQueue<AudioItem>::Entry held_audio;
  bool has_held_audio = false;
  int64_t held_audio_pts = 0;
  BoundedQueue<VideoItem>::Entry held_video;
  bool has_held_video = false;
  int64_t held_video_pts = 0;

  while (!quit_) {
    if (!has_held_audio && !audio_eof_.load()) {
      if (audio_frames_.try_pop(&held_audio)) {
        if (held_audio.item.eof) {
          audio_eof_ = true;
        } else {
          held_audio_pts = tracker_media_time(held_audio.item.chunk.pts_ms);
          has_held_audio = true;
        }
      }
    }
    if (!has_held_video && !video_eof_.load()) {
      if (video_frames_.try_pop(&held_video)) {
        if (held_video.item.eof) {
          video_eof_ = true;
        } else {
          held_video_pts = tracker_media_time(held_video.item.pts_ms);
          has_held_video = true;
        }
      }
    }

    // Consumes the stream whose PTS is the smallest; a finished stream (EOF)
    // lets the other drain freely.
    const bool audio_ready =
        has_held_audio && (!has_held_video ? video_eof_.load()
                                           : held_audio_pts <= held_video_pts);
    const bool video_ready =
        has_held_video && (!has_held_audio ? audio_eof_.load()
                                           : held_video_pts < held_audio_pts);

    if (audio_ready) {
      last_audio_end_ms = held_audio_pts + held_audio.item.chunk.duration_ms;
      av_free(held_audio.item.chunk.data);
      held_audio = BoundedQueue<AudioItem>::Entry{};
      has_held_audio = false;
    } else if (video_ready) {
      if (has_audio() && last_audio_end_ms >= 0) {
        int64_t drift = held_video_pts - last_audio_end_ms;
        if (drift < 0) {
          drift = -drift;
        }
        drift_measured = true;
        if (drift > max_drift) {
          max_drift = drift;
        }
      }
      av_frame_free(&held_video.item.frame);
      held_video = BoundedQueue<VideoItem>::Entry{};
      has_held_video = false;
    } else if (video_eof_.load() && audio_eof_.load()) {
      break;
    } else {
      sleep_ms_portable(1);  // waiting for the decoders
    }
  }

  if (has_held_audio && held_audio.item.chunk.data) {
    av_free(held_audio.item.chunk.data);
  }
  if (has_held_video && held_video.item.frame) {
    av_frame_free(&held_video.item.frame);
  }

  const double wall = chrono_now_ms() - wall_start;
  stop();

  if (out) {
    *out = stats();
    out->max_drift_ms = max_drift;  // simulated drift (PTS differences)
    out->drift_measured = drift_measured;
    out->wall_ms = wall;
    out->video_decode_busy_ms = vdec_.busy_ms();
    out->audio_decode_busy_ms = adec_.busy_ms();
  }
  return true;
}

}  // namespace player
