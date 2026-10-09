// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "ui/playback_session.h"

#include <chrono>
#include <vector>

#include "util/log.h"

namespace ui {

namespace {

constexpr int kReportFinishMs = 3000;  // like pelagia-play

uint64_t monotonic_ms() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

}  // namespace

PlaybackSession::PlaybackSession(std::unique_ptr<PlaybackMedia> media,
                                 const api::MediaItem& item, int64_t start_ms)
    : media_(std::move(media)), item_(item), start_ms_(start_ms) {}

PlaybackSession::~PlaybackSession() {
  const int state = state_.load();
  if (state == kOpening) {
    player_.abort_open();
  }
  if (state == kPlaying && !reported_stop_) {
    // Application closing in the middle of playback: position reported.
    send_stop_report(monotonic_ms());
  }
  join_threads();
  player_.stop();
  if (started_ && media_) {
    media_->finish_reports(kReportFinishMs);
  }
}

void PlaybackSession::join_threads() {
  // The stop thread joins opener_ itself: join it first here, then
  // only opener_ if it remains (never two threads on the same std::thread).
  if (stopper_.joinable()) {
    stopper_.join();
  }
  if (opener_.joinable()) {
    opener_.join();
  }
}

bool PlaybackSession::tracks(api::TrackSelection* out) const {
  return media_ && media_->selected_tracks(out);
}

bool PlaybackSession::change_tracks(const api::TrackSelection& next) {
  api::TrackSelection current;
  if (state_.load() != kPlaying || !media_ || !media_->selected_tracks(&current)) {
    return false;
  }
  if (next == current) {
    return false;
  }
  media_->set_tracks(next);
  if (api::stream_must_reopen(current, next)) {
    LOG_INFO("Track change: reopening the stream at %lld ms",
             static_cast<long long>(player_.position_ms()));
    player_.reopen_at_position();
  }
  return true;
}

PlaybackSession::Phase PlaybackSession::phase() const {
  switch (state_.load()) {
    case kOpening:
    case kOpened:
      return Phase::Opening;
    case kPlaying:
      return Phase::Playing;
    case kOpenFailed:
      return Phase::OpenFailed;
    case kStopping:
      return Phase::Stopping;
    default:
      return Phase::Stopped;
  }
}

void PlaybackSession::begin(player::VideoSink* video, player::AudioSink* audio) {
  player::Player::Callbacks cb;
  cb.video = video;
  cb.audio = audio;
  cb.start_position_ms = start_ms_;
  media_->configure(&cb);
  LOG_INFO("Playing \"%s\" from %lld ms", item_.name.c_str(),
           static_cast<long long>(start_ms_));
  // Player::open blocks (connection, transcoding start): dedicated thread.
  opener_ = std::thread([this, cb]() {
    const bool ok = player_.open(media_->source(), cb);
    int expected = kOpening;
    state_.compare_exchange_strong(expected, ok ? kOpened : kOpenFailed);
    if (!ok) {
      LOG_WARN("Cannot open the playback: %s", item_.name.c_str());
    }
  });
}

player::PlaybackObservation PlaybackSession::observe(uint64_t now_ms) const {
  player::PlaybackObservation obs;
  obs.now_ms = now_ms;
  obs.position_ms = player_.position_ms();
  obs.paused = player_.state() == player::PlayerState::Paused;
  obs.seek_in_flight = player_.seek_in_flight();
  obs.session_id = media_->stream_session_id();
  api::TrackSelection tracks;
  if (media_->selected_tracks(&tracks)) {
    obs.has_tracks = true;
    obs.media_source_id = tracks.media_source_id;
    obs.audio_index = tracks.audio_index;
    obs.subtitle_index = tracks.subtitle_index;
  }
  return obs;
}

void PlaybackSession::send_stop_report(uint64_t now_ms) {
  std::vector<player::PlaybackReport> reports;
  monitor_.stop(observe(now_ms), &reports);
  for (const player::PlaybackReport& r : reports) {
    media_->report(r);
  }
  reported_stop_ = true;
}

player::Player::TickResult PlaybackSession::tick(uint64_t now_ms) {
  int expected = kOpened;
  if (state_.compare_exchange_strong(expected, kPlaying)) {
    if (opener_.joinable()) {
      opener_.join();  // already finished: does not block
    }
    // Start on the main thread (sinks used from this thread).
    player_.start();
    started_ = true;
    media_->start_reports();
  }
  if (state_.load() != kPlaying) {
    return player::Player::TickResult();
  }
  const player::Player::TickResult r = player_.tick();
  std::vector<player::PlaybackReport> reports;
  monitor_.update(observe(now_ms), &reports);
  for (const player::PlaybackReport& rep : reports) {
    media_->report(rep);
  }
  return r;
}

void PlaybackSession::stop_async(MainQueue* main, std::function<void()> done) {
  const int state = state_.exchange(kStopping);
  if (state == kStopping || state == kStopped) {
    state_ = state;
    return;
  }
  if (state == kOpening) {
    player_.abort_open();  // the opening thread returns as soon as possible
  }
  if (state == kPlaying) {
    send_stop_report(monotonic_ms());
  }
  const bool started = started_;
  stopper_ = std::thread([this, main, done, started]() {
    if (opener_.joinable()) {
      opener_.join();
    }
    player_.stop();
    if (started) {
      media_->finish_reports(kReportFinishMs);
    }
    state_ = kStopped;
    main->post(done);
  });
}

}  // namespace ui
