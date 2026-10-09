// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_PLAYER_NETWORK_SOURCE_H
#define PELAGIA_CORE_PLAYER_NETWORK_SOURCE_H

// Network source: progressive HTTP stream (MPEG-TS transcoded by the server),
// opened through libavformat. The server does not allow seeking in the stream
// (Accept-Ranges: none): each seek closes the connection, releases the
// server job of the previous session, then reopens the stream at the wanted position
// with a new session identifier. The PTS of the new stream are
// arbitrary (timestamps_absolute() == false): the player realigns the display
// on the first PTS received (SeekOffsetTracker).
//
// No operation blocks without a limit: libavformat's interruption
// callback combines the player's test (stop, more recent seek) and its own
// deadlines (transcoding start, data being read).

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "player/media_source.h"
#include "player/stream_locator.h"

namespace player {

struct NetworkSourceOptions {
  int64_t start_ms = 0;           // position d'ouverture (reprise)
  int64_t duration_ms = 0;        // duration known from the metadata
  int open_timeout_ms = 60000;    // until the first data (transcoding)
  int read_timeout_ms = 10000;    // without any data during playback
  int seek_debounce_ms = 250;     // groups bursts of seeks
  // A stream end earlier than (duration - tolerance) is a drop, not the
  // end of the movie: the connection was closed cleanly but too early.
  int end_tolerance_ms = 10000;
  // libavformat probe (avformat_find_stream_info): it stops as soon as
  // all streams are known, so these limits cost nothing when the
  // stream is well interleaved. In direct streaming the video keeps its original
  // bitrate (much more than 8 Mb/s): a 1 MB probe may contain no
  // audio packet ("0 channels"), hence the wide limits.
  int64_t probe_bytes = 32ll * 1024 * 1024;
  int64_t probe_duration_us = 10ll * 1000000;
};

class NetworkSource : public MediaSource {
 public:
  NetworkSource(StreamLocator* locator, const NetworkSourceOptions& options);
  ~NetworkSource() override;

  bool open() override;
  void close() override;
  int read(AVPacket* pkt) override;
  bool seek(int64_t target_ms) override;
  bool timestamps_absolute() const override { return false; }
  bool recoverable() const override { return true; }
  AVFormatContext* format_context() override { return ctx_; }
  int64_t duration_ms() const override { return options_.duration_ms; }
  int64_t start_time_ms() const override { return 0; }
  void set_interrupt(InterruptFn fn, void* ctx) override;
  bool stream_info(AVMediaType type, AVCodecParameters* par, AVRational* time_base,
                   AVRational* frame_rate) override;

  // Playback session of the current stream (empty if no stream is open).
  std::string session_id() const;

 private:
  bool open_at(int64_t start_ms);
  void close_stream();
  bool wait_interruptible(int ms);
  bool externally_interrupted() const;
  void build_stream_map();
  int pick_stream(AVMediaType type) const;
  static int ff_interrupt(void* opaque);

  StreamLocator* locator_;
  NetworkSourceOptions options_;
  AVFormatContext* ctx_ = nullptr;
  InterruptFn interrupt_fn_ = nullptr;
  void* interrupt_ctx_ = nullptr;

  // Deadline (monotonic ms) of the blocking operation in progress, 0 = none.
  std::atomic<int64_t> deadline_ms_{0};
  std::atomic<bool> timed_out_{false};

  // Stream index of the first opened context: reopenings are
  // remapped onto it (the player opened its decoders on these indexes).
  int ref_video_index_ = -1;
  int ref_audio_index_ = -1;
  // Indexes of the streams kept in the current context (may change at each
  // reopening: the server only returns the requested audio track).
  int cur_video_index_ = -1;
  int cur_audio_index_ = -1;
  int map_[16];

  // Media position of the last packet read: start of the current stream + PTS
  // difference since its first packet (to recognize a premature end).
  int64_t stream_start_ms_ = 0;
  int64_t first_pts_ms_ = -1;
  int64_t last_media_ms_ = 0;

  mutable std::mutex session_mutex_;
  std::string session_id_;
};

}  // namespace player

#endif  // PELAGIA_CORE_PLAYER_NETWORK_SOURCE_H
