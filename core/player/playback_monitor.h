// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_PLAYER_PLAYBACK_MONITOR_H
#define PELAGIA_CORE_PLAYER_PLAYBACK_MONITOR_H

// Decides when to report the playback state to the server (start, progress,
// stop) from observations of the player. Pure logic with an injected clock:
// the network sending is elsewhere (core/api/jellyfin_reporter).
//
// Rules:
// - Started at the first update() (playback open, start position);
// - Progress every interval_ms, and immediately on pause, on
//   resume, at the end of a seek (new position, new session) and when
//   the chosen tracks change;
// - Stopped exactly once, at stop or at the end.

#include <cstdint>
#include <string>
#include <vector>

namespace player {

enum class ReportKind { Started, Progress, Stopped };

struct PlaybackReport {
  ReportKind kind = ReportKind::Progress;
  int64_t position_ms = 0;
  bool paused = false;
  std::string session_id;  // current stream: the server attaches its job to it
  // Current tracks (source, Jellyfin index; -1 = none/unknown). The server
  // remembers them ("remember choices" profile) from the Progress reports.
  bool has_tracks = false;
  std::string media_source_id;
  int audio_index = -1;
  int subtitle_index = -1;
};

struct PlaybackObservation {
  uint64_t now_ms = 0;
  int64_t position_ms = 0;
  bool paused = false;
  bool seek_in_flight = false;
  std::string session_id;
  bool has_tracks = false;
  std::string media_source_id;
  int audio_index = -1;
  int subtitle_index = -1;
};

class PlaybackMonitor {
 public:
  explicit PlaybackMonitor(int progress_interval_ms = 10000)
      : interval_ms_(progress_interval_ms) {}

  void update(const PlaybackObservation& obs, std::vector<PlaybackReport>* out);
  void stop(const PlaybackObservation& obs, std::vector<PlaybackReport>* out);

 private:
  void emit(ReportKind kind, const PlaybackObservation& obs,
            std::vector<PlaybackReport>* out);

  int interval_ms_;
  bool started_ = false;
  bool stopped_ = false;
  bool last_paused_ = false;
  bool last_seek_in_flight_ = false;
  int last_audio_index_ = -1;
  int last_subtitle_index_ = -1;
  uint64_t last_report_ms_ = 0;
};

}  // namespace player

#endif  // PELAGIA_CORE_PLAYER_PLAYBACK_MONITOR_H
