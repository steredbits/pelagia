// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Integration tests of the playback pipeline on real MP4 files
// synthesized locally (media_synth): full headless decoding,
// files without audio / without video, clean stop in every state,
// end of file, pause and seek clamping. No rendering: null sinks or
// counters, simulated clock injected.

#include "player/player.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "media_synth.h"
#include "test_framework.h"
#include "util/log.h"

namespace {

const char* kPathAV = "synth_av.mp4";     // 3 s video + audio
const char* kPathVideo = "synth_v.mp4";   // 2 s video only
const char* kPathAudio = "synth_a.mp4";   // 2 s audio only
const char* kPathLong = "synth_long.mp4";  // 12 s video + audio

struct FakeTime {
  uint64_t now = 0;
  static uint64_t fn(void* ctx) { return static_cast<FakeTime*>(ctx)->now; }
};

struct CountingVideoSink : player::VideoSink {
  std::atomic<int> count{0};
  bool submit(const AVFrame*) override {
    count.fetch_add(1);
    return true;
  }
};

void sleep_real_ms(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Simulated audio device, driven by the injected clock: consumes in real
// time (192 bytes/ms in S16 48 kHz stereo) as long as it is not paused.
// `hold_bytes` models a device that never manages to play the
// last bytes (stuck buffer): they stay in the queue forever.
struct FakeDeviceSink : player::AudioSink {
  static constexpr uint64_t kBytesPerMs = 192;

  explicit FakeDeviceSink(const FakeTime* time, size_t hold = 0)
      : time_(time), hold_bytes_(hold), last_ms_(time->now) {}

  size_t queue(const void*, size_t bytes) override {
    advance();
    submitted_ += bytes;
    last_change_ms_ = time_->now;
    return bytes;
  }
  size_t queued_bytes() override {
    advance();
    return static_cast<size_t>(submitted_ - consumed_);
  }
  void pause(bool paused) override {
    advance();
    paused_ = paused;
  }
  void flush() override {
    advance();
    if (submitted_ != consumed_) {
      last_change_ms_ = time_->now;
    }
    submitted_ = consumed_;
  }

  // Last instant (simulated time) when the queue content changed.
  uint64_t last_change_ms() {
    advance();
    return last_change_ms_;
  }

 private:
  void advance() {
    const uint64_t now = time_->now;
    if (!paused_ && now > last_ms_) {
      const uint64_t playable =
          submitted_ > hold_bytes_ ? submitted_ - hold_bytes_ : 0;
      uint64_t target = consumed_ + (now - last_ms_) * kBytesPerMs;
      if (target > playable) {
        target = playable > consumed_ ? playable : consumed_;
      }
      if (target != consumed_) {
        last_change_ms_ = now;
      }
      consumed_ = target;
    }
    last_ms_ = now;
  }

  const FakeTime* time_;
  const size_t hold_bytes_;
  uint64_t submitted_ = 0;
  uint64_t consumed_ = 0;
  uint64_t last_ms_ = 0;
  uint64_t last_change_ms_ = 0;
  bool paused_ = false;
};

// --- Full decoding in headless mode --------------------------------------------

void test_headless_av() {
  player::FileSource source(kPathAV);
  player::Player p;
  CHECK(p.open(&source, player::Player::Callbacks{}));
  CHECK(p.has_video());
  CHECK(p.has_audio());
  player::PlayerStats stats;
  CHECK(p.run_headless(&stats));
  // 3 s at 25 fps: 75 frames expected.
  CHECK(stats.video_frames_decoded >= 70 && stats.video_frames_decoded <= 80);
  CHECK(stats.audio_frames_decoded > 0);
  CHECK_EQ(stats.video_frames_dropped, 0u);
  CHECK(stats.media_duration_ms >= 2800 && stats.media_duration_ms <= 3400);
  CHECK(stats.wall_ms > 0.0);
  CHECK(stats.video_decode_busy_ms > 0.0);
  CHECK(stats.audio_decode_busy_ms > 0.0);
  CHECK(stats.drift_measured);  // audio + video: (simulated) drift measured
}

void test_headless_video_only() {
  player::FileSource source(kPathVideo);
  player::Player p;
  CHECK(p.open(&source, player::Player::Callbacks{}));
  CHECK(p.has_video());
  CHECK(!p.has_audio());
  player::PlayerStats stats;
  CHECK(p.run_headless(&stats));
  CHECK(stats.video_frames_decoded >= 45);
  CHECK_EQ(stats.audio_frames_decoded, 0u);
  CHECK(!stats.drift_measured);  // without audio: "n/a", not 0 ms
}

void test_headless_audio_only() {
  player::FileSource source(kPathAudio);
  player::Player p;
  CHECK(p.open(&source, player::Player::Callbacks{}));
  CHECK(!p.has_video());
  CHECK(p.has_audio());
  player::PlayerStats stats;
  CHECK(p.run_headless(&stats));
  CHECK(stats.audio_frames_decoded > 0);
  CHECK_EQ(stats.video_frames_decoded, 0u);
  CHECK(!stats.drift_measured);  // without video: nothing to compare
}

// --- Clean stop in every state -----------------------------------------------
// Each case passes if stop() returns (the ctest TIMEOUT catches any
// block) without leaks (checkable under ASan/valgrind).

void test_stop_during_playback() {
  player::FileSource source(kPathAV);
  player::Player p;
  CHECK(p.open(&source, player::Player::Callbacks{}));
  CHECK(p.start());
  sleep_real_ms(100);
  p.stop();
  CHECK(p.state() == player::PlayerState::Stopped);
}

void test_stop_with_full_queues() {
  player::FileSource source(kPathAV);
  player::Player p;
  CHECK(p.open(&source, player::Player::Callbacks{}));
  CHECK(p.start());
  // Nobody consumes: demux and decoders block on full queues.
  sleep_real_ms(400);
  p.stop();
  CHECK(p.state() == player::PlayerState::Stopped);
}

void test_stop_while_paused() {
  player::FileSource source(kPathAV);
  player::Player p;
  CHECK(p.open(&source, player::Player::Callbacks{}));
  CHECK(p.start());
  p.set_paused(true);
  CHECK(p.state() == player::PlayerState::Paused);
  sleep_real_ms(100);
  p.stop();
  CHECK(p.state() == player::PlayerState::Stopped);
}

void test_stop_during_seeks() {
  player::FileSource source(kPathAV);
  player::Player p;
  CHECK(p.open(&source, player::Player::Callbacks{}));
  CHECK(p.start());
  for (int i = 0; i < 10; ++i) {
    p.seek_to(i * 300);
    sleep_real_ms(5);
  }
  p.stop();
  CHECK(p.state() == player::PlayerState::Stopped);
}

// --- End of file: queues emptied then Ended state ----------------------------

void test_tick_until_ended() {
  FakeTime t;
  CountingVideoSink sink;
  player::FileSource source(kPathVideo);
  player::Player p;
  player::Player::Callbacks cb;
  cb.video = &sink;
  cb.time_fn = &FakeTime::fn;
  cb.time_ctx = &t;
  CHECK(p.open(&source, cb));
  CHECK(p.start());

  int guard = 0;
  while (p.state() != player::PlayerState::Ended && guard < 20000) {
    p.tick();
    t.now += 5;  // 5 ms simulated per tick
    sleep_real_ms(1);
    ++guard;
  }
  CHECK(p.state() == player::PlayerState::Ended);
  const player::PlayerStats stats = p.stats();
  // 2 s at 25 fps: 50 frames decoded, all presented or dropped.
  CHECK(stats.video_frames_decoded >= 45);
  CHECK(sink.count.load() + static_cast<int>(stats.video_frames_dropped) >= 45);
  // Without an audio track: nothing to compare, the drift must be "n/a".
  CHECK(!stats.drift_measured);
  CHECK_EQ(stats.max_drift_ms, 0);
  p.stop();
}

// --- End of playback with audio: waits for the device to drain ---------------
// Injected clock + simulated device consuming in simulated real time:
// the Ended state must not arrive as long as the device has data
// pending (audio_queued_bytes() > 0), and must arrive once drained.

void test_end_waits_for_device_drain() {
  FakeTime t;
  FakeDeviceSink device(&t);
  CountingVideoSink sink;
  player::FileSource source(kPathAV);
  player::Player p;
  player::Player::Callbacks cb;
  cb.video = &sink;
  cb.audio = &device;
  cb.time_fn = &FakeTime::fn;
  cb.time_ctx = &t;
  CHECK(p.open(&source, cb));
  CHECK(p.start());

  uint64_t ended_at = 0;
  for (int guard = 0; guard < 20000; ++guard) {
    p.tick();
    if (p.state() == player::PlayerState::Ended) {
      ended_at = t.now;
      break;
    }
    t.now += 5;
    sleep_real_ms(1);
  }
  CHECK(p.state() == player::PlayerState::Ended);
  CHECK_EQ(device.queued_bytes(), 0u);  // end only once drained
  // 3 s of media: never declared finished before the end of the sound (the ~250 ms
  // audio buffer must not be cut).
  CHECK(ended_at >= 2900);
  CHECK(ended_at <= 3400);

  const player::PlayerStats stats = p.stats();
  CHECK(stats.drift_measured);       // audio + video: drift measured
  CHECK(stats.max_drift_ms <= 80);   // bounded by the sync policy
  p.stop();
}

// Device that never manages to play its last 100 ms: the 2 s safeguard
// must end playback, neither before (cut) nor never (block).
void test_end_guard_when_device_never_drains() {
  FakeTime t;
  FakeDeviceSink device(&t, /*hold=*/19200);
  CountingVideoSink sink;
  player::FileSource source(kPathAV);
  player::Player p;
  player::Player::Callbacks cb;
  cb.video = &sink;
  cb.audio = &device;
  cb.time_fn = &FakeTime::fn;
  cb.time_ctx = &t;
  CHECK(p.open(&source, cb));
  CHECK(p.start());

  uint64_t ended_at = 0;
  for (int guard = 0; guard < 40000; ++guard) {
    p.tick();
    if (p.state() == player::PlayerState::Ended) {
      ended_at = t.now;
      break;
    }
    t.now += 5;
    sleep_real_ms(1);
  }
  CHECK(p.state() == player::PlayerState::Ended);
  CHECK(device.queued_bytes() > 0);  // end declared despite the stuck buffer
  const uint64_t stalled_for = ended_at - device.last_change_ms();
  CHECK(stalled_for >= 1900);  // did wait for the safeguard (2 s)
  CHECK(stalled_for <= 2400);
  p.stop();
}

// File without video with a device: clean end, drift "n/a".
void test_audio_only_ends_and_drift_na() {
  FakeTime t;
  FakeDeviceSink device(&t);
  player::FileSource source(kPathAudio);
  player::Player p;
  player::Player::Callbacks cb;
  cb.audio = &device;
  cb.time_fn = &FakeTime::fn;
  cb.time_ctx = &t;
  CHECK(p.open(&source, cb));
  CHECK(p.start());
  for (int guard = 0;
       guard < 20000 && p.state() != player::PlayerState::Ended; ++guard) {
    p.tick();
    t.now += 5;
    sleep_real_ms(1);
  }
  CHECK(p.state() == player::PlayerState::Ended);
  CHECK_EQ(device.queued_bytes(), 0u);
  CHECK(!p.stats().drift_measured);
  p.stop();
}

// --- Pause: no more frames while playback is paused --------------------------

void test_pause_blocks_frames() {
  FakeTime t;
  CountingVideoSink sink;
  player::FileSource source(kPathVideo);
  player::Player p;
  player::Player::Callbacks cb;
  cb.video = &sink;
  cb.time_fn = &FakeTime::fn;
  cb.time_ctx = &t;
  CHECK(p.open(&source, cb));
  CHECK(p.start());

  int guard = 0;
  while (sink.count.load() < 3 && guard < 5000) {
    p.tick();
    t.now += 5;
    sleep_real_ms(1);
    ++guard;
  }
  CHECK(sink.count.load() >= 3);

  p.set_paused(true);
  const int frozen = sink.count.load();
  const int64_t pos_at_pause = p.position_ms();
  for (int i = 0; i < 300; ++i) {
    p.tick();
    t.now += 10;  // 3 s simulated in total
    if (i % 50 == 0) {
      sleep_real_ms(1);
    }
  }
  CHECK_EQ(sink.count.load(), frozen);          // no frame while paused
  CHECK_EQ(p.position_ms(), pos_at_pause);      // the position is frozen

  p.set_paused(false);
  guard = 0;
  while (sink.count.load() <= frozen && guard < 5000) {
    p.tick();
    t.now += 5;
    sleep_real_ms(1);
    ++guard;
  }
  CHECK(sink.count.load() > frozen);  // playback resumes
  p.stop();
}

// --- Seek clamping ------------------------------------------------------------

void test_seek_clamping() {
  player::FileSource source(kPathAV);
  player::Player p;
  CHECK(p.open(&source, player::Player::Callbacks{}));
  CHECK(p.start());
  CHECK(p.duration_ms() > 0);

  p.seek_to(10000000);  // far beyond the duration
  CHECK(p.position_ms() <= p.duration_ms());

  p.seek_relative(-99999999);  // far before 0
  CHECK_EQ(p.position_ms(), 0);

  p.stop();
}

// --- Stream that misbehaves after a seek ---------------------------------------
// Source that delegates to a local file, then after the first seek:
//   NoData  : returns nothing anymore (EAGAIN), like a mute server;
//   NoAudio : drops the audio packets (track whose parameters are
//             not found, "0 channels": its packets are not mapped).
class MisbehavingSource : public player::MediaSource {
 public:
  enum class Mode { Normal, NoData, NoAudio };
  std::atomic<Mode> mode{Mode::Normal};

  explicit MisbehavingSource(const char* path) : inner_(path) {}
  void set_interrupt(player::InterruptFn fn, void* ctx) override {
    inner_.set_interrupt(fn, ctx);
  }
  bool open() override { return inner_.open(); }
  void close() override { inner_.close(); }
  int read(AVPacket* pkt) override {
    for (;;) {
      if (seeked_ && mode.load() == Mode::NoData) {
        sleep_real_ms(1);
        return AVERROR(EAGAIN);
      }
      const int err = inner_.read(pkt);
      if (err < 0 || !seeked_ || mode.load() != Mode::NoAudio || !is_audio(pkt)) {
        return err;
      }
      av_packet_unref(pkt);
    }
  }
  bool seek(int64_t target_ms) override {
    seeked_ = true;
    return inner_.seek(target_ms);
  }
  bool timestamps_absolute() const override { return true; }
  AVFormatContext* format_context() override { return inner_.format_context(); }
  int64_t duration_ms() const override { return inner_.duration_ms(); }
  int64_t start_time_ms() const override { return inner_.start_time_ms(); }

 private:
  bool is_audio(const AVPacket* pkt) {
    AVFormatContext* ctx = inner_.format_context();
    return pkt->stream_index >= 0 && pkt->stream_index < static_cast<int>(ctx->nb_streams) &&
           ctx->streams[pkt->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO;
  }

  player::FileSource inner_;
  std::atomic<bool> seeked_{false};
};

// Tick until pred() is true (simulated clock, 5 ms per turn).
template <typename Pred>
bool tick_until(player::Player* p, FakeTime* t, Pred pred, int max_ticks = 20000) {
  for (int i = 0; i < max_ticks; ++i) {
    p->tick();
    if (pred()) {
      return true;
    }
    t->now += 5;
    sleep_real_ms(1);
  }
  return false;
}

// A seek whose stream never delivers anything does not stay on "Seeking...":
// Failed (Stalled) after the delay, and a new seek retries.
void test_seek_without_data_times_out_then_retries() {
  FakeTime t;
  CountingVideoSink sink;
  MisbehavingSource source(kPathLong);
  player::Player p;
  player::Player::Callbacks cb;
  cb.video = &sink;
  cb.time_fn = &FakeTime::fn;
  cb.time_ctx = &t;
  cb.data_wait_timeout_ms = 20000;
  CHECK(p.open(&source, cb));
  CHECK(p.start());
  CHECK(tick_until(&p, &t, [&] { return sink.count >= 3; }));

  source.mode = MisbehavingSource::Mode::NoData;
  p.seek_to(2000);
  const uint64_t seek_at = t.now;
  CHECK(tick_until(&p, &t, [&] { return p.state() == player::PlayerState::Failed; }));
  CHECK(p.state() == player::PlayerState::Failed);
  CHECK(p.failure_kind() == player::FailureKind::Stalled);
  CHECK(t.now - seek_at >= 20000);  // not before the delay
  CHECK(t.now - seek_at < 22000);
  CHECK(p.seek_in_flight());

  // The stream comes back: a seek starts a new attempt ("retry" key).
  source.mode = MisbehavingSource::Mode::Normal;
  const int before = sink.count;
  p.seek_to(2000);
  CHECK(tick_until(&p, &t, [&] { return sink.count >= before + 10; }));
  CHECK(p.state() == player::PlayerState::Playing);
  CHECK(p.failure_kind() == player::FailureKind::None);
  CHECK(!p.seek_in_flight());
  p.stop();
}

// No delay requested (local file): the wait is never abandoned.
void test_no_timeout_without_policy() {
  FakeTime t;
  CountingVideoSink sink;
  MisbehavingSource source(kPathLong);
  player::Player p;
  player::Player::Callbacks cb;
  cb.video = &sink;
  cb.time_fn = &FakeTime::fn;
  cb.time_ctx = &t;
  CHECK(p.open(&source, cb));
  CHECK(p.start());
  CHECK(tick_until(&p, &t, [&] { return sink.count >= 3; }));
  source.mode = MisbehavingSource::Mode::NoData;
  p.seek_to(2000);
  CHECK(!tick_until(&p, &t, [&] { return p.state() == player::PlayerState::Failed; }, 6000));
  p.stop();
}

// Audio track declared but no packet arrives after a seek: after
// 3 s the clock locks on the video and the picture scrolls (before: a single
// picture, then nothing).
void test_seek_without_audio_packets_falls_back_to_video_clock() {
  FakeTime t;
  CountingVideoSink sink;
  MisbehavingSource source(kPathLong);
  player::Player p;
  player::Player::Callbacks cb;
  cb.video = &sink;
  cb.time_fn = &FakeTime::fn;
  cb.time_ctx = &t;
  CHECK(p.open(&source, cb));
  CHECK(p.has_audio());
  CHECK(p.start());
  CHECK(tick_until(&p, &t, [&] { return sink.count >= 3; }));

  source.mode = MisbehavingSource::Mode::NoAudio;
  const int before = sink.count;
  p.seek_to(1000);
  const uint64_t seek_at = t.now;
  CHECK(tick_until(&p, &t, [&] { return sink.count >= before + 30; }));
  CHECK(!p.seek_in_flight());
  CHECK(p.state() == player::PlayerState::Playing);
  CHECK(t.now - seek_at >= 3000);  // the grace delay was respected
  const int64_t pos = p.position_ms();
  CHECK(pos >= 1000 && pos < 8000);
  p.stop();
}

}  // namespace

int main() {
  util::log_set_level(util::LogLevel::Warn);  // tests silent except on failure

  testmedia::SynthSpec av;
  av.seconds = 3.0;
  testmedia::SynthSpec video_only;
  video_only.seconds = 2.0;
  video_only.with_audio = false;
  testmedia::SynthSpec audio_only;
  audio_only.seconds = 2.0;
  audio_only.with_video = false;

  testmedia::SynthSpec long_av;
  long_av.seconds = 12.0;

  if (!testmedia::synth_media(kPathAV, av) || !testmedia::synth_media(kPathLong, long_av) ||
      !testmedia::synth_media(kPathVideo, video_only) ||
      !testmedia::synth_media(kPathAudio, audio_only)) {
    CHECK(!"test file generation");
    return testfw::test_failures();
  }

  test_headless_av();
  test_headless_video_only();
  test_headless_audio_only();
  test_stop_during_playback();
  test_stop_with_full_queues();
  test_stop_while_paused();
  test_stop_during_seeks();
  test_tick_until_ended();
  test_end_waits_for_device_drain();
  test_end_guard_when_device_never_drains();
  test_audio_only_ends_and_drift_na();
  test_pause_blocks_frames();
  test_seek_clamping();
  test_seek_without_data_times_out_then_retries();
  test_no_timeout_without_policy();
  test_seek_without_audio_packets_falls_back_to_video_clock();
  return testfw::test_failures();
}
