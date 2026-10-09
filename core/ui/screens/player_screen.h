// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UI_SCREENS_PLAYER_SCREEN_H
#define PELAGIA_CORE_UI_SCREENS_PLAYER_SCREEN_H

// Full-screen player. Asynchronous opening ("Loading..."), then video
// with a banner: title, progress bar, elapsed / total time, state
// (pause, buffering, seek in progress - a seek takes ~4 s in
// 1080p, more in 4K, hence explicit visual feedback). The banner
// hides 4 s after the last key press, except while paused or waiting.
//
// Keys: cross/triangle/Start pause; left/right and L1/R1 +-10 s;
// L2/R2 +-60 s; Options: track menu (audio, subtitles); circle: stop
// (stop report sent, back to the page).
//
// Tracks: a change of audio or burned-in subtitles reopens the
// stream at the current position ("Changing track...", like a seek); subtitles
// rendered by the client change immediately.
//
// Subtitles rendered by the client (text): the track is downloaded separately as
// SRT (outside the display loop, once per track) and drawn over
// the video according to the playback position; the stream is not touched.

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ui/playback_session.h"
#include "util/subtitles.h"
#include "ui/screen.h"
#include "ui/screens/screens.h"

namespace ui {

class PlayerScreen final : public Screen {
 public:
  PlayerScreen(const api::MediaItem& item, int64_t start_ms,
               const api::TrackSelection& tracks = api::TrackSelection());
  ~PlayerScreen() override;

  const char* name() const override { return "player"; }
  void enter(App& app) override;
  void update(App& app) override;
  void handle(App& app, const platform::Input& in) override;
  void draw(App& app, Painter& p) override;
  bool shows_video() const override;
  bool busy() const override;
  bool wants_fast_ticks() const override { return true; }

  // State (tests).
  bool opening() const;
  bool open_failed() const;
  bool overlay_visible(uint64_t now_ms) const;
  // Stream reopening caused by a track change, in progress (tests).
  bool track_change_pending() const { return track_change_; }
  // Client subtitles (tests): wanted track, loaded tracks, texts on screen.
  int wanted_subtitle() const { return wanted_subtitle_; }
  bool subtitles_loaded(int index) const { return subtitle_tracks_.count(index) != 0; }
  bool subtitles_loading() const { return loading_subtitle_ >= 0; }
  std::vector<std::string> subtitle_lines(int64_t position_ms) const;
  const PlaybackSession* session() const { return session_.get(); }

 private:
  struct CanvasVideoSink final : player::VideoSink {
    Canvas* canvas = nullptr;
    bool submit(const AVFrame* frame) override;
  };

  void finish(App& app);
  void open_options(App& app);
  void open_audio_list(App& app);
  void open_subtitle_list(App& app);
  void apply_tracks(App& app, const api::TrackSelection& next);
  void sync_subtitles(App& app);
  void draw_subtitles(Painter& p);
  const api::MediaSourceInfo* track_source(api::TrackSelection* current) const;
  void seek(int64_t delta_ms);
  void draw_overlay(App& app, Painter& p);

  api::MediaItem item_;
  int64_t start_ms_;
  api::TrackSelection initial_tracks_;
  std::unique_ptr<PlaybackSession> session_;
  CanvasVideoSink video_sink_;
  uint64_t last_input_ms_ = 0;
  bool network_failed_ = false;
  bool finishing_ = false;
  bool no_media_ = false;
  bool track_change_ = false;
  // Client subtitles: wanted track (-1: none), tracks already downloaded,
  // download in progress (a failure is retried at the next choice of the track).
  int wanted_subtitle_ = -1;
  std::map<int, util::SubtitleTrack> subtitle_tracks_;
  int loading_subtitle_ = -1;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_SCREENS_PLAYER_SCREEN_H
