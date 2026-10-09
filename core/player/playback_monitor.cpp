// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "player/playback_monitor.h"

namespace player {

void PlaybackMonitor::emit(ReportKind kind, const PlaybackObservation& obs,
                           std::vector<PlaybackReport>* out) {
  PlaybackReport report;
  report.kind = kind;
  report.position_ms = obs.position_ms;
  report.paused = obs.paused;
  report.session_id = obs.session_id;
  report.has_tracks = obs.has_tracks;
  report.media_source_id = obs.media_source_id;
  report.audio_index = obs.audio_index;
  report.subtitle_index = obs.subtitle_index;
  out->push_back(report);
  last_report_ms_ = obs.now_ms;
  last_audio_index_ = obs.audio_index;
  last_subtitle_index_ = obs.subtitle_index;
}

void PlaybackMonitor::update(const PlaybackObservation& obs,
                             std::vector<PlaybackReport>* out) {
  if (stopped_) {
    return;
  }
  if (!started_) {
    started_ = true;
    last_paused_ = obs.paused;
    last_seek_in_flight_ = obs.seek_in_flight;
    emit(ReportKind::Started, obs, out);
    return;
  }
  // During a seek, the position is the target and the session will change:
  // we report once the seek has completed.
  if (obs.seek_in_flight) {
    last_seek_in_flight_ = true;
    return;
  }
  const bool seek_done = last_seek_in_flight_;
  const bool pause_changed = obs.paused != last_paused_;
  const bool tracks_changed = obs.has_tracks && (obs.audio_index != last_audio_index_ ||
                                                 obs.subtitle_index != last_subtitle_index_);
  last_seek_in_flight_ = false;
  last_paused_ = obs.paused;
  if (seek_done || pause_changed || tracks_changed ||
      obs.now_ms - last_report_ms_ >= static_cast<uint64_t>(interval_ms_)) {
    emit(ReportKind::Progress, obs, out);
  }
}

void PlaybackMonitor::stop(const PlaybackObservation& obs,
                           std::vector<PlaybackReport>* out) {
  if (stopped_ || !started_) {
    return;
  }
  stopped_ = true;
  emit(ReportKind::Stopped, obs, out);
}

}  // namespace player
