// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UI_JELLYFIN_PLAYBACK_H
#define PELAGIA_CORE_UI_JELLYFIN_PLAYBACK_H

// Playback of a Jellyfin item for the UI: same building blocks as pelagia-play
// - progressive TS stream with server-side seek (NetworkSource +
// JellyfinStreamLocator), buffering and recovery after a drop,
// /Sessions/Playing reports on a thread (PlaybackReporter).

#include <memory>

#include "api/http_curl.h"
#include "api/jellyfin_client.h"
#include "api/jellyfin_reporter.h"
#include "api/jellyfin_stream.h"
#include "player/network_source.h"
#include "ui/playback_session.h"

namespace ui {

class JellyfinPlaybackMedia final : public PlaybackMedia {
 public:
  // client: current session, kept alive during the whole playback.
  JellyfinPlaybackMedia(std::shared_ptr<api::JellyfinClient> client, const api::MediaItem& item,
                        int64_t start_ms, api::StreamAuthMode auth,
                        const api::TrackSelection& tracks = api::TrackSelection());
  ~JellyfinPlaybackMedia() override;

  player::MediaSource* source() override { return source_.get(); }
  void configure(player::Player::Callbacks* cb) override;
  void start_reports() override;
  void report(const player::PlaybackReport& report) override;
  std::string stream_session_id() const override;
  bool selected_tracks(api::TrackSelection* out) const override;
  void set_tracks(const api::TrackSelection& tracks) override;
  void finish_reports(int timeout_ms) override;

 private:
  std::shared_ptr<api::JellyfinClient> client_;
  api::HttpCurlTransport control_;    // DELETE during a seek: short timeouts
  api::HttpCurlTransport reporting_;  // reports: short timeouts
  std::unique_ptr<api::JellyfinStreamLocator> locator_;
  std::unique_ptr<player::NetworkSource> source_;
  std::unique_ptr<api::PlaybackReporter> reporter_;
  bool reporter_started_ = false;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_JELLYFIN_PLAYBACK_H
