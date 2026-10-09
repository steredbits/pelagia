// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_PLAYER_PLAYER_H
#define PELAGIA_CORE_PLAYER_PLAYER_H

// Playback pipeline: 3 internal threads (demux, video decoding, audio
// decoding) + a tick() pump called from the main loop (rendering).
//
//                  ┌─> video packets ─> [video thread] ─> video frames ─┐
//  [demux thread] ─┤                                                    ├─> tick()
//                  └─> audio packets ─> [audio thread] ─> audio frames ─┘
//
// The master clock is the audio (free-running without an audio track). Rendering and
// audio output go through abstract sinks: no platform include here,
// headless mode and the tests use null sinks + an injected clock.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

#include "player/av_sync.h"
#include "player/bounded_queue.h"
#include "player/buffering.h"
#include "player/clock.h"
#include "player/decoder.h"
#include "player/media_source.h"
#include "player/retry_policy.h"
#include "player/seek_offset.h"

namespace player {

struct PlayerStats {
  uint64_t video_frames_decoded = 0;
  uint64_t audio_frames_decoded = 0;
  uint64_t video_frames_dropped = 0;
  // Max observed |A/V drift| in ms. In headless mode, SIMULATED value (PTS
  // difference as consumption goes on, not a real playback measurement).
  int64_t max_drift_ms = 0;
  // False if no measurement took place (no audio track or no video:
  // nothing to compare). The display must then show "n/a", not 0 ms.
  bool drift_measured = false;
  int64_t media_duration_ms = 0;
  uint64_t reconnections = 0;  // successful resumes after a network drop
  // Filled in by run_headless():
  double wall_ms = 0.0;               // actual duration of the full decoding
  double video_decode_busy_ms = 0.0;  // total time in the video decoder
  double audio_decode_busy_ms = 0.0;  // total time in the audio decoder
};

// Video sink: receives YUV420p frames ready to display.
struct VideoSink {
  virtual ~VideoSink() {}
  virtual bool submit(const AVFrame* frame) = 0;
};

// Audio sink: interleaved S16 PCM queue (position = submitted - pending).
struct AudioSink {
  virtual ~AudioSink() {}
  virtual size_t queue(const void* samples, size_t bytes) = 0;
  virtual size_t queued_bytes() = 0;
  virtual void pause(bool paused) = 0;
  // Drops pending audio (seek): otherwise ~250 ms of stale sound would be
  // played after the seek and would skew the clock setting.
  virtual void flush() {}
};

// Failed: network drop not recovered after all attempts (a seek
// starts a new attempt).
enum class PlayerState : uint8_t { Stopped, Playing, Paused, Ended, Failed };

// Cause of a Failed state: network drop not recovered, or stream open but
// with nothing readable (no picture or sound) within the allowed time.
enum class FailureKind : uint8_t { None, Network, Stalled };

class Player {
 public:
  struct Callbacks {
    VideoSink* video = nullptr;  // nullptr = no rendering (headless/tests)
    AudioSink* audio = nullptr;  // nullptr = no audio output
    TimeFn time_fn = nullptr;    // nullptr = horloge monotone interne
    void* time_ctx = nullptr;
    int audio_sample_rate = 48000;
    int audio_channels = 2;
    // Media position at opening (network resume): displayed before the
    // first frame and base of the stream's PTS offset.
    int64_t start_position_ms = 0;
    // Buffering (disabled by default: local file).
    BufferingPolicy buffering;
    // Deep packet queues (~20 s) to absorb network jitter.
    bool deep_buffer = false;
    // Recovery after a drop (disabled by default: local file).
    RetryPolicy recovery;
    // Maximum delay (ms) allowed for an opening or a seek, once the stream is
    // (re)opened, to provide enough to resume playback; beyond it the state
    // becomes Failed (FailureKind::Stalled) instead of waiting forever.
    // 0 = no limit (local file). Network value: the server starts
    // a transcoding in ~2 to 10 s; 20 s with nothing readable is no longer a wait.
    int64_t data_wait_timeout_ms = 0;
    static constexpr int64_t kNetworkDataWaitMs = 20000;
  };

  ~Player();

  // Opens the source (open() is called on the source) and prepares decoders
  // and queues. Fails if there is no usable audio or video stream.
  bool open(MediaSource* source, const Callbacks& callbacks);

  // Interrupts an opening in progress from another thread (open() returns
  // as soon as possible and fails). No effect on a local source.
  void abort_open() { abort_requested_ = true; }

  // Reopens playback at the current position, for example after a track
  // change (the source already received the new tracks): same mechanism as
  // a seek, hence a new session on the server side. If the new stream has
  // another audio codec, another rate or another number of channels, the
  // decoder is reopened (at the generation change, in its own thread).
  void reopen_at_position() { seek_to(position_ms()); }

  // Starts the pipeline threads. State: Playing.
  bool start();

  // Stops the threads and releases the queues. Idempotent, callable in
  // any state (playing, paused, seek in progress, full queues).
  void stop();

  struct TickResult {
    bool new_video_frame = false;
    bool ended = false;
    bool buffering = false;
    bool failed = false;
  };

  // Main-loop pump: feeds the audio sink, sets the clock,
  // presents/drops video frames according to the sync.
  TickResult tick();

  void set_paused(bool paused);
  void toggle_pause();

  // Seek clamped to [0, duration]. Handled by the demux thread (queue flush,
  // new generation, clock realignment at the first PTS).
  void seek_to(int64_t target_ms);
  void seek_relative(int64_t delta_ms);

  int64_t position_ms() const;
  // True between a seek_to() and the first data of the new position
  // (during a network seek, several seconds of transcoding).
  bool seek_in_flight() const;

  // Playback suspended waiting for data (start, seek or shortage).
  bool is_buffering() const { return buffering_.load(); }
  // Media reserve available downstream of the source (ms): packets + frames of the
  // main stream (the audio if there is one). Called from the tick() thread.
  int64_t buffered_ms() const;
  int64_t duration_ms() const;
  PlayerState state() const { return state_.load(); }
  FailureKind failure_kind() const { return failure_.load(); }
  bool has_video() const { return video_stream_ >= 0; }
  bool has_audio() const { return audio_stream_ >= 0; }

  PlayerStats stats() const;
  // Number of decoder reopenings caused by a track change (tests).
  uint64_t codec_reopens() const { return stat_codec_reopens_.load(); }

  // Headless mode: consumes the whole file without rendering or real-time
  // waiting and fills in the stats. Calls start() and stop() internally.
  bool run_headless(PlayerStats* out);

 private:
  struct VideoItem {
    AVFrame* frame = nullptr;  // nullptr if eof
    int64_t pts_ms = 0;
    bool eof = false;
  };
  struct AudioItem {
    AudioChunk chunk;  // data == nullptr if eof
    bool eof = false;
  };

  static bool source_interrupt(void* ctx);
  bool is_stale(int serial) const;
  void drop_pending_items();
  bool update_buffering();
  void apply_output_pause();
  static int64_t packet_duration_ms(const AVPacket* pkt, AVRational time_base,
                                    int64_t* prev_dts_ms);
  void demux_loop();
  void video_loop();
  void audio_loop();
  bool perform_seek(int64_t target_ms, bool recovery);
  bool recover_source();
  bool wait_demux(int ms);
  bool push_packet(BoundedQueue<AVPacket*>* queue, AVPacket* pkt, int serial,
                   int64_t duration_ms);
  void push_eof_markers(int serial);
  void drain_queues();
  int64_t tracker_media_time(int64_t stream_pts_ms);
  void note_serial_change(int serial);
  uint64_t mono_ms() const;
  size_t audio_bytes_per_second() const;
  bool pump_audio();
  bool pump_video(bool* new_frame);
  void check_ended();
  bool audio_clock_expected() const;
  void check_data_wait();
  void fail(FailureKind kind);
  void note_stream_change();
  bool reopen_audio_decoder();
  bool reopen_video_decoder();

  MediaSource* source_ = nullptr;
  Callbacks cb_;
  VideoDecoder vdec_;
  AudioDecoder adec_;
  int video_stream_ = -1;
  int audio_stream_ = -1;

  // Queues: ~2 s of 1080p H.264 on the packet side; 4 YUV420p 1080p frames
  // (~3.1 MB each, ~12.5 MB) and ~0.7 s of audio on the frame side.
  BoundedQueue<AVPacket*> video_pkts_{64, 16u * 1024 * 1024};
  BoundedQueue<AVPacket*> audio_pkts_{128, 4u * 1024 * 1024};
  BoundedQueue<VideoItem> video_frames_{4, 64u * 1024 * 1024};
  BoundedQueue<AudioItem> audio_frames_{32, 8u * 1024 * 1024};

  std::thread demux_thread_;
  std::thread video_thread_;
  std::thread audio_thread_;
  std::atomic<bool> quit_{false};
  std::atomic<bool> started_{false};

  std::atomic<PlayerState> state_{PlayerState::Stopped};
  std::atomic<FailureKind> failure_{FailureKind::None};
  // Start (player clock) of the wait for data of the current
  // generation: start, or end of the reopening of a seek. 0 = not yet
  // (seek requested, stream not reopened): the timeout is not running.
  std::atomic<uint64_t> data_wait_since_{0};
  std::atomic<int> serial_{0};
  std::atomic<bool> video_eof_{false};
  std::atomic<bool> audio_eof_{false};
  std::atomic<bool> demux_eof_sent_{false};
  std::atomic<bool> force_present_{false};  // 1 frame to present while paused
  std::atomic<bool> abort_requested_{false};
  // Atomic copy of seek_pending_, read by the source's interruption
  // callback (called very often: no mutex).
  std::atomic<bool> seek_flag_{false};
  // Smallest valid generation: set by seek_to() even before the
  // demux changed generation, so that the consumer drops everything
  // preceding the seek (including what arrives in the meantime).
  std::atomic<int> min_valid_serial_{0};

  // Duration of the packets (queue total): time_base of the original streams and
  // last DTS seen, for packets without a duration (common in MPEG-TS).
  AVRational video_tb_{1, 1000};
  AVRational audio_tb_{1, 1000};

  // Stream change at reopening (other audio track: other codec,
  // rate, channels). The demux compares the new stream's parameters with those
  // of the decoders and publishes the new ones; each decoding thread
  // applies them at the generation change, before its first packet.
  AVCodecParameters* audio_params_ = nullptr;  // last published state (demux thread)
  AVCodecParameters* video_params_ = nullptr;
  std::mutex codec_mutex_;
  AVCodecParameters* audio_pending_ = nullptr;  // sous codec_mutex_
  AVCodecParameters* video_pending_ = nullptr;
  AVRational audio_pending_tb_{1, 1000};
  AVRational video_pending_tb_{1, 1000};
  AVRational video_pending_fr_{0, 1};
  uint64_t audio_codec_version_ = 0;
  uint64_t video_codec_version_ = 0;
  uint64_t audio_codec_applied_ = 0;  // decoding threads
  uint64_t video_codec_applied_ = 0;
  std::atomic<bool> audio_decoder_ok_{true};
  std::atomic<bool> video_decoder_ok_{true};
  std::atomic<uint64_t> stat_codec_reopens_{0};
  int64_t video_prev_dts_ms_ = -1;
  int64_t audio_prev_dts_ms_ = -1;

  // Buffering (state of the tick() thread).
  BufferingGate gate_;
  std::atomic<bool> buffering_{false};
  // Recovery after a drop: position known to the consumer (read by the
  // demux) and "stream restarted" signal to be handled by the consumer.
  std::atomic<int64_t> last_position_ms_{0};
  std::atomic<bool> restart_pending_{false};
  std::atomic<uint64_t> stat_reconnections_{0};
  bool output_paused_ = false;
  uint64_t buffering_since_ = 0;

  // Pending seek (consumed by the demux thread) + offset tracker.
  mutable std::mutex seek_mutex_;
  bool seek_pending_ = false;
  int64_t seek_target_ms_ = 0;
  SeekOffsetTracker tracker_;
  std::atomic<int64_t> last_seek_target_{-1};

  // Consumer state (tick / headless, one thread at a time).
  MasterClock clock_;
  int consumed_serial_ = 0;
  VideoItem pending_video_;
  bool has_pending_video_ = false;
  AudioItem pending_audio_;
  bool has_pending_audio_ = false;
  bool first_video_shown_ = false;
  // No audio despite a declared track: clock set on the video.
  uint64_t audio_wait_since_ = 0;
  bool audio_clock_waived_ = false;
  uint64_t end_wait_start_ = 0;

  // Stats (decoding threads + consumer).
  std::atomic<uint64_t> stat_video_frames_{0};
  std::atomic<uint64_t> stat_audio_frames_{0};
  std::atomic<uint64_t> stat_video_dropped_{0};
  std::atomic<int64_t> stat_max_drift_{0};
  std::atomic<bool> stat_drift_measured_{false};
};

}  // namespace player

#endif  // PELAGIA_CORE_PLAYER_PLAYER_H
