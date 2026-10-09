// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Integration tests of network playback against the fake Jellyfin server
// (tools/fake_jellyfin_server.py) : vrai HTTP, vrai transcodage ffmpeg.
//
// The server's test pattern has a luminance of 16 + 5 x T: each test checks the
// real position of the displayed content (and not only what the
// player believes), which notably detects a transcoding job reused by mistake.
// The player clock is simulated and accelerated; network delays are real
// but reduced (startup latency ~1 s) to keep the suite fast.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include <fcntl.h>
#include <unistd.h>

extern "C" {
#include <libavutil/log.h>
}

#include "api/http_curl.h"
#include "api/jellyfin_client.h"
#include "api/jellyfin_reporter.h"
#include "api/jellyfin_stream.h"
#include "fake_server.h"
#include "player/av_log_bridge.h"
#include "player/network_source.h"
#include "player/playback_monitor.h"
#include "player/player.h"
#include "test_framework.h"
#include "util/log.h"

namespace {

const char kItem[] = "f00dfeed0000000000000000000000aa";
constexpr long long kTicksPerMs = 10000;

struct FakeTime {
  uint64_t now = 1000;
  static uint64_t fn(void* ctx) { return static_cast<FakeTime*>(ctx)->now; }
};

int64_t real_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void sleep_real(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

double expected_luma(int64_t media_ms) {
  return 16.0 + 5.0 * static_cast<double>(media_ms) / 1000.0;
}

// Mean luminance of the center of each presented frame.
struct LumaSink : player::VideoSink {
  std::atomic<int> frames{0};
  std::atomic<int> frames_since_mark{0};
  double last_luma = -1;
  double first_luma_since_mark = -1;

  bool submit(const AVFrame* f) override {
    long sum = 0;
    int n = 0;
    for (int y = f->height / 4; y < 3 * f->height / 4; y += 4) {
      for (int x = f->width / 4; x < 3 * f->width / 4; x += 4) {
        sum += f->data[0][y * f->linesize[0] + x];
        ++n;
      }
    }
    last_luma = n > 0 ? static_cast<double>(sum) / n : -1;
    if (frames_since_mark.fetch_add(1) == 0) {
      first_luma_since_mark = last_luma;
    }
    frames.fetch_add(1);
    return true;
  }

  void mark() {
    frames_since_mark = 0;
    first_luma_since_mark = -1;
  }
};

struct Env {
  testnet::FakeServer server;
  api::HttpCurlTransport transport;
  api::HttpCurlTransport control;
  api::HttpCurlTransport reporting;
  std::unique_ptr<api::JellyfinClient> client;
  api::MediaItem item;
};
Env* g_env = nullptr;
// Probe limit of the created sources (0 = core default value): 1 MB
// reproduces the old probe, which did not find the audio in direct streaming.
int64_t g_probe_bytes = 0;

testnet::ServerState server_state() {
  testnet::ServerState st;
  testnet::parse_state(g_env->server.state(), &st);
  return st;
}

void configure(const char* json) {
  CHECK(g_env->server.post("/__test/config", json));
}

void reset_server() {
  CHECK(g_env->server.post("/__test/reset", "{}"));
  configure(R"({"startup_delay": 1.0, "stream_auth": "both", "pts_mode": "zero",
                "readrate": 0, "video_kbps": 0, "late_audio": 0})");
}

bool has_delete(const testnet::ServerState& st, const std::string& session) {
  for (const testnet::DeleteInfo& d : st.deletes) {
    if (d.play_session_id == session && d.status == 204) {
      return true;
    }
  }
  return false;
}

// Network stream: the locator must outlive the source (declared before).
struct Stream {
  std::unique_ptr<api::JellyfinStreamLocator> locator;
  std::unique_ptr<player::NetworkSource> source;

  Stream(api::StreamAuthMode mode, int64_t start_ms, int read_timeout_ms) {
    locator.reset(new api::JellyfinStreamLocator(*g_env->client, g_env->control, kItem, mode));
    player::NetworkSourceOptions options;
    options.start_ms = start_ms;
    options.duration_ms = g_env->item.runtime_ticks / kTicksPerMs;
    options.open_timeout_ms = 15000;
    options.read_timeout_ms = read_timeout_ms;
    options.seek_debounce_ms = 100;
    if (g_probe_bytes > 0) {
      options.probe_bytes = g_probe_bytes;
    }
    source.reset(new player::NetworkSource(locator.get(), options));
  }
};

// Thresholds reduced for the tests (the player uses 2 s / 4 s / 250 ms).
player::BufferingPolicy test_buffering_policy() {
  player::BufferingPolicy p;
  p.start_ms = 1000;
  p.resume_ms = 1500;
  p.low_water_ms = 200;
  return p;
}

// Playback rig: the player is destroyed (stopped) before the source.
struct Rig {
  FakeTime time;
  LumaSink sink;
  int64_t start_ms;
  Stream stream;
  player::Player player;
  // Player clock: accelerated x5 (default) or at real pace, for
  // scenarios where the server throughput matters (buffering).
  bool realtime = false;
  int64_t real_base = 0;
  int ticks_buffering = 0;
  int frames_while_buffering = 0;
  player::RetryPolicy recovery;  // disabled unless the test sets it
  bool failed = false;

  explicit Rig(api::StreamAuthMode mode = api::StreamAuthMode::Both, int64_t start = 0,
               int read_timeout_ms = 4000)
      : start_ms(start), stream(mode, start, read_timeout_ms) {}

  bool open(bool with_buffering = false) {
    player::Player::Callbacks cb;
    cb.video = &sink;
    cb.time_fn = &FakeTime::fn;
    cb.time_ctx = &time;
    cb.start_position_ms = start_ms;
    if (with_buffering) {
      cb.buffering = test_buffering_policy();
      cb.deep_buffer = true;
    }
    cb.recovery = recovery;
    real_base = real_ms();
    return player.open(stream.source.get(), cb) && player.start();
  }

  void step() {
    const int before = sink.frames;
    const player::Player::TickResult result = player.tick();
    if (result.buffering) {
      ++ticks_buffering;
      frames_while_buffering += sink.frames - before;
    }
    failed = result.failed;
    if (realtime) {
      time.now = 1000 + static_cast<uint64_t>(real_ms() - real_base);
    } else {
      time.now += 5;
    }
    sleep_real(1);
  }

  template <typename Pred>
  bool run_until(Pred pred, int max_real_ms = 15000) {
    const int64_t end = real_ms() + max_real_ms;
    while (real_ms() < end) {
      step();
      if (pred()) {
        return true;
      }
    }
    return false;
  }

  void run_for(int real_ms_duration) {
    const int64_t end = real_ms() + real_ms_duration;
    while (real_ms() < end) {
      step();
    }
  }
};

// --- Scenarios ---------------------------------------------------------------

void test_play_from_start() {
  reset_server();
  Rig r;
  CHECK(r.open());
  CHECK(r.run_until([&] { return r.sink.frames >= 50; }));
  const int64_t pos = r.player.position_ms();
  CHECK(std::fabs(r.sink.last_luma - expected_luma(pos)) < 6);

  const std::string session = r.stream.source->session_id();
  testnet::ServerState st = server_state();
  CHECK_EQ(st.requests.size(), 1u);
  if (!st.requests.empty()) {
    CHECK_EQ(st.requests[0].start_ticks, 0);
    CHECK(!st.requests[0].reused);
    CHECK(st.requests[0].auth == "query+header");
    CHECK_EQ(st.requests[0].play_session_id.size(), 32u);
    CHECK(st.requests[0].play_session_id == session);
  }

  // End of playback: the source releases the server job.
  r.player.stop();
  r.stream.source.reset();
  st = server_state();
  CHECK(has_delete(st, session));
  CHECK_EQ(st.active_jobs, 0);
}

void test_seek_forward_and_backward() {
  reset_server();
  Rig r;
  CHECK(r.open());
  CHECK(r.run_until([&] { return r.sink.frames >= 10; }));
  const std::string first_session = r.stream.source->session_id();

  // Avant : 20 s.
  r.sink.mark();
  r.player.seek_to(20000);
  CHECK(r.player.seek_in_flight());
  CHECK_EQ(r.player.position_ms(), 20000);
  CHECK(r.run_until([&] { return r.sink.frames_since_mark >= 5; }));
  CHECK(std::fabs(r.sink.first_luma_since_mark - expected_luma(20000)) < 6);
  CHECK(!r.player.seek_in_flight());
  const int64_t pos = r.player.position_ms();
  CHECK(pos >= 19900 && pos < 21500);

  testnet::ServerState st = server_state();
  CHECK_EQ(st.requests.size(), 2u);
  if (st.requests.size() == 2) {
    CHECK_EQ(st.requests[1].start_ticks, 20000 * kTicksPerMs);
    CHECK(!st.requests[1].reused);
    CHECK(st.requests[1].play_session_id != first_session);
  }
  CHECK(has_delete(st, first_session));  // old job stopped before the reopening
  CHECK_EQ(st.active_jobs, 1);

  // Backwards: 5 s.
  r.sink.mark();
  r.player.seek_to(5000);
  CHECK(r.run_until([&] { return r.sink.frames_since_mark >= 5; }));
  CHECK(std::fabs(r.sink.first_luma_since_mark - expected_luma(5000)) < 6);
  st = server_state();
  CHECK_EQ(st.requests.size(), 3u);
  CHECK_EQ(st.active_jobs, 1);
}

void test_seek_burst_only_last_counts() {
  reset_server();
  Rig r;
  CHECK(r.open());
  CHECK(r.run_until([&] { return r.sink.frames >= 10; }));
  const size_t before = server_state().requests.size();

  // Key held down: 5 close relative seeks, each starting from the target
  // of the previous one (and not from the clock, frozen on the old position).
  r.sink.mark();
  int64_t expected = r.player.position_ms();
  for (int i = 0; i < 5; ++i) {
    r.player.seek_relative(3000);
    expected += 3000;
    r.player.tick();
    sleep_real(20);
  }
  CHECK_EQ(r.player.position_ms(), expected);
  CHECK(r.run_until([&] { return r.sink.frames_since_mark >= 5; }));
  CHECK(std::fabs(r.sink.first_luma_since_mark - expected_luma(expected)) < 6);

  const testnet::ServerState st = server_state();
  const size_t opened = st.requests.size() - before;
  CHECK(opened >= 1 && opened <= 2);  // burst grouped, not 5 transcodings
  if (!st.requests.empty()) {
    CHECK_EQ(st.requests.back().start_ticks, expected * kTicksPerMs);
  }
  CHECK_EQ(st.active_jobs, 1);  // no orphan job
}

void test_stop_during_seek() {
  reset_server();
  Rig r;
  CHECK(r.open());
  CHECK(r.run_until([&] { return r.sink.frames >= 10; }));
  configure(R"({"startup_delay": 3.0})");
  r.player.seek_to(30000);
  sleep_real(600);  // request sent, the server "transcodes"

  const int64_t t0 = real_ms();
  r.player.stop();
  const int64_t stop_ms = real_ms() - t0;
  CHECK(stop_ms < 1000);
  r.stream.source.reset();

  const testnet::ServerState st = server_state();
  CHECK(!st.requests.empty() && st.requests.back().start_ticks == 30000 * kTicksPerMs);
  if (!st.requests.empty()) {
    CHECK(has_delete(st, st.requests.back().play_session_id));
  }
  CHECK_EQ(st.active_jobs, 0);
}

void test_abort_initial_open() {
  reset_server();
  configure(R"({"startup_delay": 3.0})");
  Rig r;
  std::atomic<bool> opened{true};
  std::thread opener([&] { opened = r.open(); });
  sleep_real(500);
  const int64_t t0 = real_ms();
  r.player.abort_open();
  opener.join();
  CHECK(real_ms() - t0 < 1000);
  CHECK(!opened);
  r.player.stop();  // always before destroying the source
  r.stream.source.reset();
  CHECK_EQ(server_state().active_jobs, 0);
}

void test_stream_auth_modes() {
  reset_server();
  configure(R"({"stream_auth": "header"})");
  {
    Rig r(api::StreamAuthMode::Header);
    CHECK(r.open());
    CHECK(r.run_until([&] { return r.sink.frames >= 5; }));
    const testnet::ServerState st = server_state();
    CHECK(!st.requests.empty() && st.requests.back().auth == "header");
  }
  {
    // api_key alone refused by this server: fast failure, no waiting.
    Rig r(api::StreamAuthMode::Query);
    const int64_t t0 = real_ms();
    CHECK(!r.open());
    CHECK(real_ms() - t0 < 3000);
  }
}

void test_absolute_pts_after_seek() {
  reset_server();
  configure(R"({"pts_mode": "absolute"})");
  Rig r;
  CHECK(r.open());
  CHECK(r.run_until([&] { return r.sink.frames >= 10; }));
  CHECK(std::fabs(r.sink.last_luma - expected_luma(r.player.position_ms())) < 6);
  r.sink.mark();
  r.player.seek_to(20000);
  CHECK(r.run_until([&] { return r.sink.frames_since_mark >= 5; }));
  CHECK(std::fabs(r.sink.first_luma_since_mark - expected_luma(20000)) < 6);
  const int64_t pos = r.player.position_ms();
  CHECK(pos >= 19900 && pos < 21500);
}

// Start: no frame before the start reserve is built up.
void test_startup_buffering() {
  reset_server();
  Rig r;
  r.realtime = true;
  int64_t buffered_at_first_frame = -1;
  CHECK(r.open(true));
  CHECK(r.player.is_buffering());
  CHECK(r.run_until([&] {
    if (r.sink.frames > 0 && buffered_at_first_frame < 0) {
      buffered_at_first_frame = r.player.buffered_ms();
    }
    return r.sink.frames >= 10;
  }));
  CHECK(buffered_at_first_frame >= test_buffering_policy().start_ms - 200);
  CHECK(!r.player.is_buffering());
  CHECK_EQ(r.frames_while_buffering, 0);
}

// The server stops sending for a few seconds (less than the read
// timeout): playback is suspended without losing frames, then resumes on the
// same connection, at the right place.
void test_stall_rebuffers_and_resumes() {
  reset_server();
  configure(R"({"readrate": 1.3})");  // barely faster than real time
  Rig r;
  r.realtime = true;
  CHECK(r.open(true));
  CHECK(r.run_until([&] { return r.sink.frames >= 25 && !r.player.is_buffering(); }));
  r.ticks_buffering = 0;
  const size_t requests_before = server_state().requests.size();

  CHECK(g_env->server.post("/__test/fault", R"({"stall_s": 3.0})"));
  CHECK(r.run_until([&] { return r.player.is_buffering(); }, 6000));
  const int64_t frozen = r.player.position_ms();
  const int frames_at_freeze = r.sink.frames;
  r.run_for(500);
  CHECK_EQ(r.player.position_ms(), frozen);  // clock frozen during the wait
  CHECK_EQ(r.sink.frames, frames_at_freeze);

  CHECK(r.run_until([&] { return !r.player.is_buffering() && r.sink.frames > frames_at_freeze + 10; },
                    10000));
  CHECK(r.ticks_buffering > 0);
  CHECK_EQ(r.frames_while_buffering, 0);
  CHECK(std::fabs(r.sink.last_luma - expected_luma(r.player.position_ms())) < 6);
  CHECK(r.player.stats().video_frames_dropped < 5);
  CHECK_EQ(server_state().requests.size(), requests_before);  // no reconnection
}

// Stop while playback waits for data: immediate.
void test_stop_while_buffering() {
  reset_server();
  configure(R"({"readrate": 1.3})");
  Rig r;
  r.realtime = true;
  CHECK(r.open(true));
  CHECK(r.run_until([&] { return r.sink.frames >= 10 && !r.player.is_buffering(); }));
  CHECK(g_env->server.post("/__test/fault", R"({"stall_s": 3.0})"));
  CHECK(r.run_until([&] { return r.player.is_buffering(); }, 6000));
  const int64_t t0 = real_ms();
  r.player.stop();
  CHECK(real_ms() - t0 < 1000);
}

// Fast recovery for the tests: 100, 200, 400, 800, 1600 ms (the player
// utilise 1, 2, 4, 8, 15 s).
player::RetryPolicy fast_recovery() {
  player::RetryPolicy p;
  p.max_attempts = 5;
  p.initial_delay_ms = 100;
  p.max_delay_ms = 1600;
  return p;
}

// Playback in progress, at real pace, with buffering and recovery.
bool start_resilient(Rig* r, int max_attempts = 5) {
  r->realtime = true;
  r->recovery = fast_recovery();
  r->recovery.max_attempts = max_attempts;
  return r->open(true) &&
         r->run_until([&] { return r->sink.frames >= 15 && !r->player.is_buffering(); });
}

// Waits for playback to have resumed after a drop (frames again).
bool wait_recovered(Rig* r, int max_real_ms) {
  return r->run_until(
      [&] {
        return r->player.stats().reconnections >= 1 && !r->player.is_buffering() &&
               r->sink.frames_since_mark >= 10;
      },
      max_real_ms);
}

// Abrupt drop (RST): recovery at the playback position, new session,
// old job released.
void test_drop_reconnects_at_position() {
  reset_server();
  configure(R"({"readrate": 1.3})");
  Rig r;
  CHECK(start_resilient(&r));
  const int64_t pos_before = r.player.position_ms();
  const std::string session_before = r.stream.source->session_id();
  const size_t requests_before = server_state().requests.size();

  r.sink.mark();
  CHECK(g_env->server.post("/__test/fault", R"({"drop": true})"));
  CHECK(wait_recovered(&r, 10000));
  CHECK_EQ(r.player.stats().reconnections, 1u);
  CHECK(r.player.state() == player::PlayerState::Playing);
  CHECK(std::fabs(r.sink.last_luma - expected_luma(r.player.position_ms())) < 6);

  const testnet::ServerState st = server_state();
  CHECK_EQ(st.requests.size(), requests_before + 1);
  if (!st.requests.empty()) {
    // Resume where playback was (it went on on the reserve).
    const long long resume_ms = st.requests.back().start_ticks / kTicksPerMs;
    CHECK(resume_ms >= pos_before && resume_ms <= pos_before + 3000);
    CHECK(st.requests.back().play_session_id != session_before);
  }
  CHECK(has_delete(st, session_before));
  CHECK_EQ(st.active_jobs, 1);
}

// Server unreachable for 1.5 s (Wi-Fi cut): the attempts fail then the
// playback resumes.
void test_outage_then_recovery() {
  reset_server();
  configure(R"({"readrate": 1.3})");
  Rig r;
  CHECK(start_resilient(&r));
  r.sink.mark();
  const int64_t t0 = real_ms();
  CHECK(g_env->server.post("/__test/fault", R"({"drop": true, "refuse_s": 1.5})"));
  CHECK(wait_recovered(&r, 15000));
  CHECK(real_ms() - t0 >= 1400);
  CHECK(r.player.state() == player::PlayerState::Playing);
  CHECK(std::fabs(r.sink.last_luma - expected_luma(r.player.position_ms())) < 6);
}

// The server cleanly ends the stream well before the end: drop, not the end.
void test_truncated_stream_is_a_cut() {
  reset_server();
  configure(R"({"readrate": 1.3})");
  Rig r;
  CHECK(start_resilient(&r));
  r.sink.mark();
  CHECK(g_env->server.post("/__test/fault", R"({"truncate": true})"));
  CHECK(wait_recovered(&r, 10000));
  CHECK(r.player.state() == player::PlayerState::Playing);
}

// No more data, connection open (Wi-Fi without RST): read timeout
// exceeded, then resume when the server answers again.
void test_stall_beyond_read_timeout() {
  reset_server();
  configure(R"({"readrate": 1.3})");
  Rig r(api::StreamAuthMode::Both, 0, /*read_timeout_ms=*/1500);
  CHECK(start_resilient(&r));
  r.sink.mark();
  CHECK(g_env->server.post("/__test/fault", R"({"stall_s": 4.0})"));
  CHECK(wait_recovered(&r, 15000));
  CHECK(r.player.state() == player::PlayerState::Playing);
}

// Server lost: Failed state after the attempts, without blocking; a seek
// starts a new attempt once the server is back.
void test_permanent_outage_fails_then_seek_retries() {
  reset_server();
  configure(R"({"readrate": 1.3})");
  Rig r;
  CHECK(start_resilient(&r, 3));  // the number of attempts changes nothing here
  CHECK(g_env->server.post("/__test/fault", R"({"drop": true, "refuse_s": 60})"));
  CHECK(r.run_until([&] { return r.failed; }, 15000));
  CHECK(r.player.state() == player::PlayerState::Failed);

  CHECK(g_env->server.post("/__test/reset", "{}"));  // server back
  configure(R"({"startup_delay": 1.0, "readrate": 1.3})");
  r.sink.mark();
  r.player.seek_to(10000);
  CHECK(r.run_until([&] { return r.sink.frames_since_mark >= 10; }, 10000));
  CHECK(r.player.state() == player::PlayerState::Playing);
  CHECK(std::fabs(r.sink.first_luma_since_mark - expected_luma(10000)) < 6);
}

// The real end of the movie is not a drop: Ended state, no recovery.
void test_natural_end_is_not_a_cut() {
  reset_server();
  Rig r(api::StreamAuthMode::Both, 34000);
  r.recovery = fast_recovery();
  CHECK(r.open(true));
  CHECK(r.run_until([&] { return r.player.state() == player::PlayerState::Ended; }));
  CHECK_EQ(r.player.stats().reconnections, 0u);
  CHECK_EQ(server_state().requests.size(), 1u);
  CHECK(std::fabs(r.sink.first_luma_since_mark - expected_luma(34000)) < 6);
}

// Direct streaming (finding on PS5): the video keeps its original bitrate and the
// first audio packet arrives after several MB of video, well beyond a
// 1 MB probe ("Could not find codec parameters for stream 1 ... 0 channels").
// Opening and seek must find the audio, never stay on "Seeking...".
constexpr const char kDirectStream[] = R"({"video_kbps": 12000, "late_audio": 3.0})";

void test_direct_stream_late_audio_open() {
  reset_server();
  configure(kDirectStream);
  Rig r;
  CHECK(r.open(true));
  CHECK(r.player.has_audio());  // probe wide enough to find the audio
  CHECK(r.run_until([&] {
    return r.sink.frames >= 50 && r.player.stats().audio_frames_decoded >= 20;
  }));
  CHECK(!r.player.is_buffering());
}

void test_direct_stream_late_audio_seek() {
  reset_server();
  configure(R"({"video_kbps": 12000})");
  Rig r;
  CHECK(r.open(true));
  CHECK(r.player.has_audio());
  CHECK(r.run_until([&] { return r.sink.frames >= 10; }));

  // Recoveries after a seek reopen the stream: late audio this time.
  configure(kDirectStream);
  const uint64_t audio_before = r.player.stats().audio_frames_decoded;
  r.sink.mark();
  r.player.seek_to(20000);
  CHECK(r.run_until(
      [&] {
        return r.sink.frames_since_mark >= 50 && !r.player.seek_in_flight() &&
               !r.player.is_buffering() &&
               r.player.stats().audio_frames_decoded >= audio_before + 20;
      },
      25000));
  CHECK(r.player.state() == player::PlayerState::Playing);
  // The fake stream also shifts the audio PTS (+3 s): the clock, set by
  // the first audio packet, makes the first ~3 seconds of
  // video be dropped. The content must stay that of the requested position, up to this
  // offset.
  CHECK(r.sink.first_luma_since_mark >= expected_luma(19900) &&
        r.sink.first_luma_since_mark <= expected_luma(20000 + 6000));
  const int64_t pos = r.player.position_ms();
  CHECK(pos >= 19900 && pos < 30000);
}

// Probe too short (1 MB, audio at 6 MB) on a reopening: the packets of
// the audio track without parameters are kept with the first stream's decoder
// instead of being dropped (before: the audio never arrived, endless "Seeking...").
// The player's clock fallback and the give-up delay are also tested
// in test_player.
void test_direct_stream_unresolved_audio_after_seek() {
  reset_server();
  configure(R"({"video_kbps": 12000})");
  g_probe_bytes = 1000000;
  Rig r;
  g_probe_bytes = 0;
  CHECK(r.open(true));
  CHECK(r.player.has_audio());  // audio near the start: found even with 1 MB
  CHECK(r.run_until([&] { return r.sink.frames >= 10; }));

  configure(kDirectStream);
  const uint64_t audio_before = r.player.stats().audio_frames_decoded;
  r.sink.mark();
  r.player.seek_to(20000);
  CHECK(r.run_until(
      [&] {
        return r.sink.frames_since_mark >= 50 && !r.player.seek_in_flight() &&
               r.player.stats().audio_frames_decoded >= audio_before + 20;
      },
      25000));
  CHECK(r.player.state() == player::PlayerState::Playing);
  CHECK(r.sink.first_luma_since_mark >= expected_luma(19900));
}

// Audio not found from the opening: clear message (log), playback of the
// video without sound, no infinite wait.
void test_direct_stream_unresolved_audio_at_open() {
  reset_server();
  configure(kDirectStream);
  g_probe_bytes = 1000000;
  Rig r;
  g_probe_bytes = 0;
  CHECK(r.open(true));
  CHECK(!r.player.has_audio());
  CHECK(r.player.has_video());
  CHECK(r.run_until([&] { return r.sink.frames >= 50; }));
  CHECK(!r.player.is_buffering());
  CHECK(r.player.state() == player::PlayerState::Playing);
}

// Playback reports driven as in pelagia-play (monitor + sending
// thread), on a playback rig.
struct Reporting {
  player::PlaybackMonitor monitor{1000};  // progress every simulated second
  api::PlaybackReporter reporter;
  std::vector<player::PlaybackReport> out;

  Reporting()
      : reporter(*g_env->client, g_env->reporting, kItem, g_env->item.media_source_id) {
    reporter.start();
  }

  player::PlaybackObservation observe(Rig* r) {
    player::PlaybackObservation obs;
    obs.now_ms = r->time.now;
    obs.position_ms = r->player.position_ms();
    obs.paused = r->player.state() == player::PlayerState::Paused;
    obs.seek_in_flight = r->player.seek_in_flight();
    obs.session_id = r->stream.source->session_id();
    return obs;
  }

  void flush() {
    for (const player::PlaybackReport& report : out) {
      reporter.submit(report);
    }
    out.clear();
  }

  template <typename Pred>
  bool run_until(Rig* r, Pred pred, int max_real_ms = 15000) {
    return r->run_until(
        [&] {
          monitor.update(observe(r), &out);
          flush();
          return pred();
        },
        max_real_ms);
  }
};

bool body_has(const std::string& body, const std::string& needle) {
  return body.find(needle) != std::string::npos;
}

long long body_position_ms(const std::string& body) {
  const size_t at = body.find("\"PositionTicks\":");
  return at == std::string::npos ? -1 : std::atoll(body.c_str() + at + 16) / kTicksPerMs;
}

// Start, progress, pause/resume, seek, stop: sequence received by the
// server; then the saved position allows resuming at the right place.
void test_reporting_and_resume() {
  reset_server();
  CHECK(g_env->server.post("/__test/user_data",
                           R"({"PlaybackPositionTicks": 0, "Played": false})"));
  int64_t stopped_at = 0;
  {
    Rig r;
    Reporting rep;
    CHECK(r.open());
    CHECK(rep.run_until(&r, [&] { return r.sink.frames >= 60; }));  // > 2 simulated s
    r.player.set_paused(true);
    CHECK(rep.run_until(&r, [&] { return true; }));
    r.player.set_paused(false);
    r.sink.mark();
    r.player.seek_to(20000);
    CHECK(rep.run_until(&r, [&] { return r.sink.frames_since_mark >= 10; }));
    const std::string session_after_seek = r.stream.source->session_id();

    rep.monitor.stop(rep.observe(&r), &rep.out);
    stopped_at = rep.out.empty() ? 0 : rep.out.back().position_ms;
    rep.flush();
    r.player.stop();
    CHECK(rep.reporter.finish(3000));
    CHECK_EQ(rep.reporter.failed_count(), 0);

    const testnet::ServerState st = server_state();
    CHECK(st.report_kinds.size() >= 5);
    if (st.report_kinds.size() >= 5) {
      CHECK(st.report_kinds.front() == "start");
      CHECK(st.report_kinds.back() == "stopped");
      bool paused_seen = false;
      bool unpaused_after_pause = false;
      bool seek_reported = false;
      for (size_t i = 0; i < st.report_kinds.size(); ++i) {
        const std::string& body = st.report_bodies[i];
        CHECK(body_has(body, std::string("\"ItemId\":\"") + kItem + "\""));
        if (st.report_kinds[i] != "progress") {
          continue;
        }
        CHECK(body_has(body, "\"PlayMethod\":\"Transcode\""));
        if (body_has(body, "\"IsPaused\":true")) {
          paused_seen = true;
        } else if (paused_seen) {
          unpaused_after_pause = true;
        }
        const long long pos = body_position_ms(body);
        if (pos >= 20000 && pos < 22000 &&
            body_has(body, "\"PlaySessionId\":\"" + session_after_seek + "\"")) {
          seek_reported = true;  // attached to the job of the new stream
        }
      }
      CHECK(paused_seen);
      CHECK(unpaused_after_pause);
      CHECK(seek_reported);
      CHECK(std::llabs(body_position_ms(st.report_bodies.back()) - stopped_at) <= 1);
    }
    // Resume position saved (between 5 and 90% of the duration).
    CHECK(std::llabs(st.resume_position_ticks / kTicksPerMs - stopped_at) <= 1);
  }

  // --resume: the resume read from the server opens the stream at the right place.
  api::MediaItem item;
  CHECK(g_env->client->fetch_item(kItem, &item).ok());
  const int64_t resume_ms = item.playback_position_ticks / kTicksPerMs;
  CHECK(std::llabs(resume_ms - stopped_at) <= 1);
  Rig r(api::StreamAuthMode::Both, resume_ms);
  CHECK(r.open());
  CHECK_EQ(r.player.position_ms(), resume_ms);  // before the first frame
  CHECK(r.run_until([&] { return r.sink.frames >= 5; }));
  CHECK(std::fabs(r.sink.first_luma_since_mark - expected_luma(resume_ms)) < 6);
  const testnet::ServerState st = server_state();
  CHECK(!st.requests.empty() && st.requests.back().start_ticks == resume_ms * kTicksPerMs);
}

// Redirects the process's stderr to a file for the duration of a scenario: everything
// that is written (util/log, ffmpeg, other) is captured, not only what
// goes through the logging module.
class StderrCapture {
 public:
  explicit StderrCapture(const std::string& path) : path_(path) {
    std::fflush(stderr);
    saved_ = dup(STDERR_FILENO);
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(fd, STDERR_FILENO);
    close(fd);
  }
  std::string finish() {
    std::fflush(stderr);
    dup2(saved_, STDERR_FILENO);
    close(saved_);
    return testnet::read_file(path_);
  }

 private:
  std::string path_;
  int saved_ = -1;
};

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

// The token appears in no output, even with ffmpeg's most
// detailed logs (full HTTP requests), in a scenario that opens,
// seeks, drops, resumes and reports.
void test_token_never_logged() {
  reset_server();
  const std::string token = g_env->client->session().access_token;
  CHECK_EQ(token.size(), 32u);
  util::log_set_level(util::LogLevel::Debug);
  player::install_ffmpeg_log_bridge(true);
  av_log_set_level(AV_LOG_TRACE);

  StderrCapture capture(testnet::workdir() + "/token_scenario.log");
  {
    Rig r;
    r.recovery = fast_recovery();
    Reporting rep;
    CHECK(r.open(true));
    CHECK(rep.run_until(&r, [&] { return r.sink.frames >= 15; }));
    r.sink.mark();
    r.player.seek_to(15000);
    CHECK(rep.run_until(&r, [&] { return r.sink.frames_since_mark >= 5; }));
    CHECK(g_env->server.post("/__test/fault", R"({"drop": true})"));
    CHECK(rep.run_until(&r, [&] { return r.player.stats().reconnections >= 1 &&
                                         r.sink.frames_since_mark >= 15; }));
    rep.monitor.stop(rep.observe(&r), &rep.out);
    rep.flush();
    r.player.stop();
    rep.reporter.finish(3000);
  }
  const std::string output = capture.finish();
  util::log_set_level(std::getenv("PELAGIA_TEST_VERBOSE") ? util::LogLevel::Debug
                                                           : util::LogLevel::Warn);
  av_log_set_level(AV_LOG_WARNING);

  CHECK(output.size() > 10000);  // the detailed logs were indeed produced
  CHECK(!contains(output, token));
  CHECK(!contains(output, token.substr(0, 12)));
  // Positive control: the URL and the authentication header went through the
  // logs (otherwise the test would prove nothing), masked.
  CHECK(contains(output, "api_key=********"));
  CHECK(contains(output, "Token=\"********\""));
  CHECK(contains(output, "[http @"));
}

// Le vrai binaire : pelagia-play --item --resume --verbose (SDL factice),
// resume near the end, reports, clean exit, no token in its output.
void test_cli_item_resume() {
  reset_server();
  CHECK(g_env->server.post("/__test/user_data",
                           R"({"PlaybackPositionTicks": 360000000, "Played": false})"));
  setenv("JELLYFIN_URL", g_env->server.base_url().c_str(), 1);
  setenv("JELLYFIN_USER", "test", 1);
  setenv("JELLYFIN_PASS", "test", 1);
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  setenv("SDL_AUDIODRIVER", "dummy", 1);
  const std::string log_path = testnet::workdir() + "/cli_item.log";
  const int code = testnet::run_process(
      {PELAGIA_PLAY_BIN, "--item", kItem, "--resume", "--verbose"}, log_path, 30000);
  const std::string output = testnet::read_file(log_path);
  CHECK_EQ(code, 0);
  CHECK(contains(output, "Resume at 0:00:36"));
  CHECK(contains(output, "End of playback"));
  CHECK(!contains(output, g_env->client->session().access_token));
  // Default: header only. The token is in no URL (even masked); it
  // only appears in the header of ffmpeg's HTTP request (debug), masked.
  CHECK(contains(output, "stream authentication: header"));
  CHECK(!contains(output, "api_key"));
  CHECK(contains(output, "Token=\"********\""));

  const testnet::ServerState st = server_state();
  CHECK(!st.requests.empty() && st.requests.front().auth == "header");
  CHECK(!st.requests.empty() && st.requests.front().start_ticks == 36000 * kTicksPerMs);
  CHECK(!st.report_kinds.empty() && st.report_kinds.front() == "start");
  CHECK(!st.report_kinds.empty() && st.report_kinds.back() == "stopped");
  CHECK(st.played);  // stopped at the end: marked "played", resume cleared
  CHECK_EQ(st.resume_position_ticks, 0);
  CHECK_EQ(st.active_jobs, 0);
  if (code != 0) {
    std::fprintf(stderr, "--- sortie de pelagia-play ---\n%s\n", output.c_str());
  }
}

// The three stream authentication modes remain available through the CLI
// (the default, header, is checked by test_cli_item_resume).
void test_cli_stream_auth_modes() {
  reset_server();
  configure(R"({"startup_delay": 0.2})");  // three full playbacks: minimal latency
  setenv("JELLYFIN_URL", g_env->server.base_url().c_str(), 1);
  setenv("JELLYFIN_USER", "test", 1);
  setenv("JELLYFIN_PASS", "test", 1);
  const std::string log_path = testnet::workdir() + "/cli_auth.log";
  for (const char* mode : {"query", "both", "header"}) {
    CHECK_EQ(testnet::run_process({PELAGIA_PLAY_BIN, "--headless", "--item", kItem,
                                   "--stream-auth", mode},
                                  log_path, 30000),
             0);
  }
  const testnet::ServerState st = server_state();
  CHECK_EQ(st.requests.size(), 3u);
  if (st.requests.size() == 3) {
    CHECK(st.requests[0].auth == "query");
    CHECK(st.requests[1].auth == "query+header");
    CHECK(st.requests[2].auth == "header");
  }
}

// pelagia-cli: the identifier of each movie and series is displayed (value of
// pelagia-play --item), --episodes lists the episodes with theirs.
void test_cli_lists_ids() {
  reset_server();
  setenv("JELLYFIN_URL", g_env->server.base_url().c_str(), 1);
  setenv("JELLYFIN_USER", "test", 1);
  setenv("JELLYFIN_PASS", "test", 1);
  const std::string token = g_env->client->session().access_token;
  const std::string log_path = testnet::workdir() + "/cli_ids.log";
  const std::string series = "f00dfeed0000000000000000000000cc";

  CHECK_EQ(testnet::run_process({PELAGIA_CLI_BIN}, log_path, 20000), 0);
  std::string out = testnet::read_file(log_path);
  CHECK(contains(out, "Test pattern (2026)  [" + std::string(kItem) + "]"));
  CHECK(contains(out, "Series (1):"));
  CHECK(contains(out, "Test series (2026)  [" + series + "]"));
  CHECK(contains(out, "pelagia-play --item <id>"));
  CHECK(contains(out, "pelagia-cli --episodes <id>"));
  CHECK(!contains(out, token));
  CHECK(!contains(out, "api_key"));  // the example URL does not carry the token

  CHECK_EQ(testnet::run_process({PELAGIA_CLI_BIN, "--episodes", series}, log_path, 20000), 0);
  out = testnet::read_file(log_path);
  CHECK(contains(out, "Episodes (2):"));
  CHECK(contains(out, "S01E01  Episode 1"));
  CHECK(contains(out, "S01E02  Episode 2"));
  CHECK(contains(out, "f00dfeed0000000000000000000000e1"));
  CHECK(contains(out, "f00dfeed0000000000000000000000e2"));

  // Errors: unknown series (server) -> 1; incorrect arguments -> 2; help -> 0.
  CHECK_EQ(testnet::run_process({PELAGIA_CLI_BIN, "--episodes", "inconnue"}, log_path, 20000), 1);
  CHECK_EQ(testnet::run_process({PELAGIA_CLI_BIN, "--episodes"}, log_path, 20000), 2);
  CHECK_EQ(testnet::run_process({PELAGIA_CLI_BIN, "--bogus"}, log_path, 20000), 2);
  CHECK_EQ(testnet::run_process({PELAGIA_CLI_BIN, "--bogus", "--help"}, log_path, 20000), 0);
  CHECK(contains(testnet::read_file(log_path), "--episodes <id>"));

  // pelagia-play --item on a series: message that points to the episodes.
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  setenv("SDL_AUDIODRIVER", "dummy", 1);
  CHECK_EQ(testnet::run_process({PELAGIA_PLAY_BIN, "--headless", "--item", series}, log_path,
                                20000), 1);
  out = testnet::read_file(log_path);
  CHECK(contains(out, "is not a playable media"));
  CHECK(contains(out, "pelagia-cli --episodes " + series));
}

}  // namespace

// PELAGIA_TEST_ONLY=<name>: only runs that scenario (debugging).
#define RUN(test)                                                     \
  do {                                                                \
    const char* only = std::getenv("PELAGIA_TEST_ONLY");             \
    if (!only || std::strcmp(only, #test) == 0) {                     \
      const int64_t t0 = real_ms();                                   \
      test();                                                         \
      std::fprintf(stderr, "  %-46s %5.1f s\n", #test,                \
                   (real_ms() - t0) / 1000.0);                        \
    }                                                                 \
  } while (0)

int main() {
  util::log_set_level(std::getenv("PELAGIA_TEST_VERBOSE") ? util::LogLevel::Debug
                                                           : util::LogLevel::Warn);
  std::string media;
  if (!testnet::ensure_test_media(&media)) {
    std::fprintf(stderr, "python3/ffmpeg unavailable: test skipped\n");
    return testnet::kSkip;
  }
  Env env;
  g_env = &env;
  if (!env.server.start(media, {"--min-resume-duration", "0"})) {
    CHECK(!"fake server start");
    return testfw::test_failures();
  }
  env.control.set_timeouts(3, 5);
  env.reporting.set_timeouts(3, 5);
  env.client.reset(new api::JellyfinClient(env.transport, env.server.base_url(),
                                           "pelagia-test"));
  CHECK(env.client->authenticate("test", "test").ok());
  CHECK(env.client->fetch_item(kItem, &env.item).ok());

  RUN(test_play_from_start);
  RUN(test_seek_forward_and_backward);
  RUN(test_seek_burst_only_last_counts);
  RUN(test_stop_during_seek);
  RUN(test_abort_initial_open);
  RUN(test_stream_auth_modes);
  RUN(test_absolute_pts_after_seek);
  RUN(test_startup_buffering);
  RUN(test_stall_rebuffers_and_resumes);
  RUN(test_stop_while_buffering);
  RUN(test_drop_reconnects_at_position);
  RUN(test_outage_then_recovery);
  RUN(test_truncated_stream_is_a_cut);
  RUN(test_stall_beyond_read_timeout);
  RUN(test_permanent_outage_fails_then_seek_retries);
  RUN(test_natural_end_is_not_a_cut);
  RUN(test_direct_stream_late_audio_open);
  RUN(test_direct_stream_late_audio_seek);
  RUN(test_direct_stream_unresolved_audio_after_seek);
  RUN(test_direct_stream_unresolved_audio_at_open);
  RUN(test_reporting_and_resume);
  RUN(test_token_never_logged);
  RUN(test_cli_item_resume);
  RUN(test_cli_stream_auth_modes);
  RUN(test_cli_lists_ids);
  return testfw::test_failures();
}
