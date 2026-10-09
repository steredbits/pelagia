// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "ui/jellyfin_playback.h"

#include "ui/format.h"

namespace ui {

JellyfinPlaybackMedia::JellyfinPlaybackMedia(std::shared_ptr<api::JellyfinClient> client,
                                             const api::MediaItem& item, int64_t start_ms,
                                             api::StreamAuthMode auth,
                                             const api::TrackSelection& tracks)
    : client_(std::move(client)) {
  control_.set_timeouts(3, 5);
  reporting_.set_timeouts(3, 5);
  locator_.reset(new api::JellyfinStreamLocator(*client_, control_, item.id, auth, tracks));
  reporter_.reset(
      new api::PlaybackReporter(*client_, reporting_, item.id, item.media_source_id));
  player::NetworkSourceOptions options;
  options.duration_ms = item.runtime_ticks / kTicksPerMs;
  options.start_ms = start_ms;
  source_.reset(new player::NetworkSource(locator_.get(), options));
}

JellyfinPlaybackMedia::~JellyfinPlaybackMedia() {
  // Normally already done by PlaybackSession (bounded wait).
  if (reporter_started_) {
    reporter_->finish(0);
  }
}

void JellyfinPlaybackMedia::configure(player::Player::Callbacks* cb) {
  cb->buffering = player::BufferingPolicy::network();
  cb->deep_buffer = true;
  cb->recovery = player::RetryPolicy::network();
  cb->data_wait_timeout_ms = player::Player::Callbacks::kNetworkDataWaitMs;
}

void JellyfinPlaybackMedia::start_reports() {
  reporter_->start();
  reporter_started_ = true;
}

void JellyfinPlaybackMedia::report(const player::PlaybackReport& report) {
  if (reporter_started_) {
    reporter_->submit(report);
  }
}

std::string JellyfinPlaybackMedia::stream_session_id() const { return source_->session_id(); }

bool JellyfinPlaybackMedia::selected_tracks(api::TrackSelection* out) const {
  *out = locator_->tracks();
  return !out->media_source_id.empty() || out->audio_index >= 0;
}

void JellyfinPlaybackMedia::set_tracks(const api::TrackSelection& tracks) {
  locator_->set_tracks(tracks);
}

void JellyfinPlaybackMedia::finish_reports(int timeout_ms) {
  if (reporter_started_) {
    reporter_->finish(timeout_ms);
    reporter_started_ = false;
  }
}

}  // namespace ui
