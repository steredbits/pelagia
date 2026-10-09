// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UI_PLAYBACK_SESSION_H
#define PELAGIA_CORE_UI_PLAYBACK_SESSION_H

// A playback as seen by the UI. Nothing blocking in the display loop:
// - opening (Player::open: connection, transcoding start, several
//   seconds on a real server) on a dedicated, interruptible thread
//   (abort_open) if the user goes back;
// - player start, pump (tick) and progress reports on the
//   thread principal ;
// - asynchronous stop: Stopped report (resume position), stop of the
//   player threads and bounded wait for the reports to be sent on a
//   dedicated thread; the end is signaled on the main thread.
//
// The source and the reports come from the backend (PlaybackMedia): Jellyfin
// for real, local file in the tests.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "api/jellyfin_models.h"
#include "api/track_selection.h"
#include "player/playback_monitor.h"
#include "player/player.h"
#include "ui/tasks.h"

namespace ui {

// Resources of a playback provided by the backend. The report methods
// do not block (send queue), except finish_reports (bounded).
class PlaybackMedia {
 public:
  virtual ~PlaybackMedia() {}
  virtual player::MediaSource* source() = 0;
  // Player settings (buffering, recovery after a drop...).
  virtual void configure(player::Player::Callbacks* cb) { (void)cb; }
  virtual void start_reports() {}
  virtual void report(const player::PlaybackReport& report) { (void)report; }
  // Current stream session (attaches the reports to the server job).
  virtual std::string stream_session_id() const { return ""; }
  // Current tracks (false: source without chosen tracks). The reports
  // send them to the server, which remembers them for the profile.
  virtual bool selected_tracks(api::TrackSelection* out) const {
    (void)out;
    return false;
  }
  // New tracks for the next stream opening (the player then triggers
  // the reopening at the current position).
  virtual void set_tracks(const api::TrackSelection& tracks) { (void)tracks; }
  // Waits for the queued reports to be sent, at most timeout_ms.
  virtual void finish_reports(int timeout_ms) { (void)timeout_ms; }
};

class PlaybackSession {
 public:
  enum class Phase { Opening, Playing, OpenFailed, Stopping, Stopped };

  PlaybackSession(std::unique_ptr<PlaybackMedia> media, const api::MediaItem& item,
                  int64_t start_ms);
  // Synchronous stop if needed (application closing in the middle of
  // playback: the position is still reported).
  ~PlaybackSession();
  PlaybackSession(const PlaybackSession&) = delete;
  PlaybackSession& operator=(const PlaybackSession&) = delete;

  // Main thread. Starts the opening; video/audio may be null.
  void begin(player::VideoSink* video, player::AudioSink* audio);
  // Main thread, at each frame: starts the player once opened,
  // pumps audio/video, sends the reports.
  player::Player::TickResult tick(uint64_t now_ms);
  // Main thread. done is executed on the main thread (through main)
  // when everything is stopped and the reports are sent (or the timeout elapsed).
  void stop_async(MainQueue* main, std::function<void()> done);

  Phase phase() const;
  // Current tracks (false if the source has not chosen any).
  bool tracks(api::TrackSelection* out) const;
  // Main thread, during playback. Changes the tracks: reopening of the
  // stream at the current position if needed (audio, burned-in subtitles),
  // otherwise immediate (subtitles rendered by the client). False if nothing to change.
  bool change_tracks(const api::TrackSelection& next);
  player::Player& player() { return player_; }
  const player::Player& player() const { return player_; }
  const api::MediaItem& item() const { return item_; }
  int64_t start_ms() const { return start_ms_; }

 private:
  enum : int { kOpening, kOpened, kPlaying, kOpenFailed, kStopping, kStopped };
  player::PlaybackObservation observe(uint64_t now_ms) const;
  void send_stop_report(uint64_t now_ms);
  void join_threads();

  // Destruction order: threads stopped (destructor), player, then media.
  std::unique_ptr<PlaybackMedia> media_;
  api::MediaItem item_;
  int64_t start_ms_;
  player::Player player_;
  player::PlaybackMonitor monitor_;
  std::thread opener_;
  std::thread stopper_;
  std::atomic<int> state_{kOpening};
  bool started_ = false;   // player started (main thread)
  bool reported_stop_ = false;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_PLAYBACK_SESSION_H
