// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Audio and subtitle tracks end to end against the fake server (catalog
// "tracks"): profile default tracks, stream actually served (a sine wave
// per audio track, a red square for burn-in), track change during
// playback by reopening at the current position, remembered reports.
//
// Same rig as test_network_playback: simulated and accelerated player clock,
// real but reduced network delays. The "audio device" is simulated to
// measure the frequency actually played.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "api/http_curl.h"
#include "api/jellyfin_client.h"
#include "api/jellyfin_reporter.h"
#include "api/jellyfin_stream.h"
#include "api/track_selection.h"
#include "fake_server.h"
#include "player/network_source.h"
#include "player/playback_monitor.h"
#include "player/player.h"
#include "test_framework.h"
#include "util/log.h"
#include "util/subtitles.h"

namespace {

const char kVostfr[] = "de300000000000000000000000000301";
const char k1917[] = "de300000000000000000000000000300";
constexpr long long kTicksPerMs = 10000;

// Atomic: read by the demux thread (deadlines), written by the test loop.
struct FakeTime {
  std::atomic<uint64_t> now{1000};
  static uint64_t fn(void* ctx) { return static_cast<FakeTime*>(ctx)->now.load(); }
};

int64_t real_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Presented picture: center luminance and V chroma of the top-left corner (the
// fake server's burn-in square puts V ~ 240 there; the pattern is 128).
struct VideoProbe : player::VideoSink {
  std::atomic<int> frames{0};
  double corner_v = -1;
  double center_luma = -1;

  bool submit(const AVFrame* f) override {
    long sum = 0;
    int n = 0;
    for (int y = 2; y < 18; ++y) {
      for (int x = 2; x < 30; ++x) {
        sum += f->data[2][y * f->linesize[2] + x];
        ++n;
      }
    }
    corner_v = static_cast<double>(sum) / n;
    center_luma = f->data[0][(f->height / 2) * f->linesize[0] + f->width / 2];
    frames.fetch_add(1);
    return true;
  }
};

// Simulated audio device: drains at the pace of the simulated clock and keeps
// the last samples (S16 stereo 48 kHz) to measure the frequency.
struct SimAudio : player::AudioSink {
  FakeTime* time = nullptr;
  uint64_t last_ms = 0;
  double backlog = 0;  // pending bytes
  bool paused = false;
  std::vector<int16_t> left;  // left channel since the last mark()
  std::atomic<uint64_t> total_bytes{0};

  void drain() {
    const uint64_t now = time->now;
    if (!paused && now > last_ms) {
      backlog -= (now - last_ms) * 192.0;  // 48000 * 2 * 2 octets/s
      if (backlog < 0) backlog = 0;
    }
    last_ms = now;
  }
  size_t queue(const void* samples, size_t bytes) override {
    drain();
    const int16_t* s = static_cast<const int16_t*>(samples);
    for (size_t i = 0; i + 1 < bytes / 2; i += 2) {
      left.push_back(s[i]);
    }
    backlog += bytes;
    total_bytes += bytes;
    return bytes;
  }
  size_t queued_bytes() override {
    drain();
    return static_cast<size_t>(backlog);
  }
  void pause(bool p) override {
    drain();
    paused = p;
  }
  void flush() override {
    drain();
    backlog = 0;
  }
  void mark() { left.clear(); }
  // Frequency (Hz) of the kept samples, rising zero crossings.
  double frequency() const {
    if (left.size() < 24000) return -1;  // less than 0.5 s
    int crossings = 0;
    for (size_t i = 1; i < left.size(); ++i) {
      if (left[i - 1] < 0 && left[i] >= 0) ++crossings;
    }
    return crossings * 48000.0 / left.size();
  }
};

struct Env {
  testnet::FakeServer server;
  api::HttpCurlTransport transport;
  api::HttpCurlTransport control;
  api::HttpCurlTransport reporting;
  std::unique_ptr<api::JellyfinClient> client;
};
Env* g_env = nullptr;

testnet::ServerState server_state() {
  testnet::ServerState st;
  testnet::parse_state(g_env->server.state(), &st);
  return st;
}

void reset_server() {
  CHECK(g_env->server.post("/__test/reset", "{}"));
  CHECK(g_env->server.post("/__test/config",
                           R"({"startup_delay": 0.3, "omit_defaults": false, "subtitle_mode": "Default",
                               "subtitle_language": "", "remember_selections": true})"));
}

bool fetch(const char* id, api::MediaItem* item) {
  return g_env->client->fetch_item(id, item).ok() && !item->media_sources.empty();
}

api::TrackSelection default_tracks(const api::MediaItem& item) {
  return api::default_selection(item.media_sources[0], g_env->client->session().preferences);
}

struct Rig {
  FakeTime time;
  VideoProbe video;
  SimAudio audio;
  std::string item_id;
  std::unique_ptr<api::JellyfinStreamLocator> locator;
  std::unique_ptr<player::NetworkSource> source;
  player::Player player;

  Rig(const std::string& id, const api::MediaItem& item, const api::TrackSelection& tracks,
      int64_t start_ms = 0)
      : item_id(id) {
    audio.time = &time;
    audio.last_ms = time.now;
    locator.reset(new api::JellyfinStreamLocator(*g_env->client, g_env->control, id,
                                                 api::StreamAuthMode::Header, tracks));
    player::NetworkSourceOptions options;
    options.start_ms = start_ms;
    options.duration_ms = item.runtime_ticks / kTicksPerMs;
    options.open_timeout_ms = 15000;
    options.seek_debounce_ms = 100;
    source.reset(new player::NetworkSource(locator.get(), options));
    start = start_ms;
  }
  int64_t start = 0;

  bool open() {
    player::Player::Callbacks cb;
    cb.video = &video;
    cb.audio = &audio;
    cb.time_fn = &FakeTime::fn;
    cb.time_ctx = &time;
    cb.start_position_ms = start;
    return player.open(source.get(), cb) && player.start();
  }

  void step() {
    player.tick();
    time.now += 5;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  template <typename Pred>
  bool run_until(Pred pred, int max_real_ms = 20000) {
    const int64_t end = real_ms() + max_real_ms;
    while (real_ms() < end) {
      step();
      if (pred()) return true;
    }
    return false;
  }

  void run_for(int ms) {
    const int64_t end = real_ms() + ms;
    while (real_ms() < end) step();
  }

  // Plays until there is enough to measure the audio frequency (~0.6 s of sound) since the last mark().
  double measure_frequency() {
    audio.mark();
    run_until([&] { return audio.left.size() > 30000; }, 20000);
    return audio.frequency();
  }

  // Track change during playback: new tracks for the
  // next opening, then reopening at the current position.
  void change_tracks(const api::TrackSelection& tracks) {
    locator->set_tracks(tracks);
    player.reopen_at_position();
  }

  bool reopened() {
    return !player.seek_in_flight();
  }
};

// --- Scenarios ---------------------------------------------------------------

void test_profile_and_defaults() {
  reset_server();
  const api::UserPreferences& prefs = g_env->client->session().preferences;
  CHECK(prefs.play_default_audio);
  CHECK(prefs.audio_language.empty());
  CHECK(prefs.subtitle_mode == api::SubtitleMode::Default);

  // "1917": server defaults = TrueHD/E-AC3 -> track 2, PGS 4, burned in.
  api::MediaItem film;
  CHECK(fetch(k1917, &film));
  CHECK_EQ(film.media_sources[0].streams.size(), 5u);
  api::TrackSelection sel = default_tracks(film);
  CHECK_EQ(sel.audio_index, 2);
  CHECK_EQ(sel.subtitle_index, 4);
  CHECK(sel.subtitle_delivery == api::SubtitleDelivery::Encode);

  // Subbed movie: English audio by default, forced French subtitles (text, client).
  api::MediaItem vostfr;
  CHECK(fetch(kVostfr, &vostfr));
  sel = default_tracks(vostfr);
  CHECK_EQ(sel.audio_index, 1);
  CHECK_EQ(sel.subtitle_index, 6);
  CHECK(sel.subtitle_delivery == api::SubtitleDelivery::Client);

  // The server no longer announces default tracks: local fallback, same result here.
  CHECK(g_env->server.post("/__test/config", R"({"omit_defaults": true})"));
  CHECK(fetch(kVostfr, &vostfr));
  CHECK(!vostfr.media_sources[0].server_defaults);
  sel = default_tracks(vostfr);
  CHECK_EQ(sel.audio_index, 1);
  CHECK_EQ(sel.subtitle_index, 6);
  CHECK(g_env->server.post("/__test/config", R"({"omit_defaults": false})"));
}

void test_stream_follows_chosen_audio() {
  reset_server();
  api::MediaItem item;
  CHECK(fetch(kVostfr, &item));
  api::TrackSelection sel = default_tracks(item);
  CHECK_EQ(sel.audio_index, 1);
  Rig r(kVostfr, item, sel);
  CHECK(r.open());
  const double f1 = r.measure_frequency();
  CHECK(std::fabs(f1 - 440) < 30);
  // The request carries the source and the track, not the client-rendered subtitles.
  testnet::ServerState st = server_state();
  CHECK_EQ(st.requests.size(), 1u);
  if (!st.requests.empty()) {
    CHECK_EQ(st.requests[0].media_source_id, std::string(kVostfr));
    CHECK_EQ(st.requests[0].audio_index, "1");
    CHECK(st.requests[0].subtitle_index.empty());
    CHECK(!st.requests[0].burn);
  }
  CHECK(r.video.corner_v < 140);  // nothing burned in
  r.player.stop();
}

void test_switch_audio_during_playback() {
  reset_server();
  api::MediaItem item;
  CHECK(fetch(kVostfr, &item));
  const api::MediaSourceInfo& src = item.media_sources[0];
  api::TrackSelection sel = default_tracks(item);
  Rig r(kVostfr, item, sel);
  CHECK(r.open());
  CHECK(std::fabs(r.measure_frequency() - 440) < 30);
  const std::string first_session = r.source->session_id();
  const int64_t before = r.player.position_ms();

  // Dubbed: same format, other track.
  r.change_tracks(api::with_audio(src, g_env->client->session().preferences, sel, 2));
  CHECK(r.run_until([&] { return r.player.seek_in_flight(); }, 2000) || true);
  CHECK(r.run_until([&] { return !r.player.seek_in_flight(); }));
  const double f2 = r.measure_frequency();
  CHECK(std::fabs(f2 - 660) < 30);
  const int64_t after = r.player.position_ms();
  CHECK(after >= before - 1000);        // we resume where we were (no return to the start)
  CHECK(after < before + 15000);
  CHECK(r.source->session_id() != first_session);
  testnet::ServerState st = server_state();
  CHECK_EQ(st.requests.size(), 2u);
  if (st.requests.size() == 2) {
    CHECK_EQ(st.requests[1].audio_index, "2");
    CHECK(st.requests[1].start_ticks > 0);
    CHECK(!st.requests[1].reused);
  }
  bool released = false;
  for (const testnet::DeleteInfo& d : st.deletes) {
    released = released || (d.play_session_id == first_session && d.status == 204);
  }
  CHECK(released);            // the old job is released before the reopening
  CHECK_EQ(st.active_jobs, 1);  // a single transcoding at a time

  // Copyable track: AAC mono 44.1 kHz -> the audio decoder is reopened, the
  // output stays 48 kHz stereo and the frequency is the right one.
  const uint64_t reopens = r.player.codec_reopens();
  r.change_tracks(api::with_audio(src, g_env->client->session().preferences, sel, 3));
  r.run_for(300);
  CHECK(r.run_until([&] { return !r.player.seek_in_flight(); }));
  const double f3 = r.measure_frequency();
  CHECK(std::fabs(f3 - 880) < 30);
  CHECK(r.player.codec_reopens() > reopens);
  // And back to a transcoded track: another change of parameters.
  r.change_tracks(sel);
  r.run_for(300);
  CHECK(r.run_until([&] { return !r.player.seek_in_flight(); }));
  CHECK(std::fabs(r.measure_frequency() - 440) < 30);
  CHECK(r.player.state() == player::PlayerState::Playing);
  r.player.stop();
  r.source.reset();
  CHECK_EQ(server_state().active_jobs, 0);
}

void test_burned_subtitles() {
  reset_server();
  // "1917": the server's default choice is a PGS, hence burned in.
  api::MediaItem item;
  CHECK(fetch(k1917, &item));
  const api::MediaSourceInfo& src = item.media_sources[0];
  api::TrackSelection sel = default_tracks(item);
  Rig r(k1917, item, sel);
  CHECK(r.open());
  CHECK(r.run_until([&] { return r.video.frames >= 25; }));
  CHECK(r.video.corner_v > 200);
  testnet::ServerState st = server_state();
  CHECK_EQ(st.requests.size(), 1u);
  if (!st.requests.empty()) {
    CHECK_EQ(st.requests[0].audio_index, "2");
    CHECK_EQ(st.requests[0].subtitle_index, "4");
    CHECK_EQ(st.requests[0].subtitle_method, "Encode");
    CHECK(st.requests[0].burn);
  }

  // "None": the stream is reopened without burn-in.
  r.change_tracks(api::with_subtitle(src, sel, -1));
  r.run_for(300);
  CHECK(r.run_until([&] { return !r.player.seek_in_flight(); }));
  const int frames = r.video.frames;
  CHECK(r.run_until([&] { return r.video.frames >= frames + 25; }));
  CHECK(r.video.corner_v < 140);
  st = server_state();
  CHECK_EQ(st.requests.size(), 2u);
  if (st.requests.size() == 2) {
    CHECK(st.requests[1].subtitle_index.empty());
    CHECK(!st.requests[1].burn);
    CHECK_EQ(st.requests[1].audio_index, "2");
  }
  // The other PGS: burned in again.
  r.change_tracks(api::with_subtitle(src, sel, 3));
  r.run_for(300);
  CHECK(r.run_until([&] { return !r.player.seek_in_flight(); }));
  const int frames2 = r.video.frames;
  CHECK(r.run_until([&] { return r.video.frames >= frames2 + 25; }));
  CHECK(r.video.corner_v > 200);
  r.player.stop();
}

void test_reports_remembered_by_server() {
  reset_server();
  api::MediaItem item;
  CHECK(fetch(kVostfr, &item));
  const api::MediaSourceInfo& src = item.media_sources[0];
  api::TrackSelection sel = default_tracks(item);  // audio 1, forced 6
  CHECK(sel.audio_index == 1);

  // Tracks reported by a client (monitor + reporter), as in the application.
  api::PlaybackReporter reporter(*g_env->client, g_env->reporting, kVostfr, src.id);
  reporter.start();
  player::PlaybackMonitor monitor;
  std::vector<player::PlaybackReport> reports;
  player::PlaybackObservation obs;
  obs.now_ms = 1000;
  obs.position_ms = 5000;
  obs.session_id = "sess";
  obs.has_tracks = true;
  obs.media_source_id = src.id;
  obs.audio_index = 2;
  obs.subtitle_index = 5;
  monitor.update(obs, &reports);  // Started
  obs.now_ms = 2000;
  obs.audio_index = 3;  // track change -> immediate Progress
  monitor.update(obs, &reports);
  CHECK_EQ(reports.size(), 2u);
  for (const player::PlaybackReport& rep : reports) {
    reporter.submit(rep);
  }
  obs.now_ms = 3000;
  monitor.stop(obs, &reports);
  reporter.submit(reports.back());
  CHECK(reporter.finish(5000));

  testnet::ServerState st = server_state();
  bool saw_tracks = false;
  for (const std::string& body : st.report_bodies) {
    saw_tracks = saw_tracks || (body.find("\"AudioStreamIndex\":3") != std::string::npos &&
                                body.find("\"SubtitleStreamIndex\":5") != std::string::npos &&
                                body.find("\"MediaSourceId\":\"" + src.id + "\"") != std::string::npos);
  }
  CHECK(saw_tracks);
  // The server now returns these tracks as default tracks.
  api::MediaItem again;
  CHECK(fetch(kVostfr, &again));
  api::TrackSelection next = default_tracks(again);
  CHECK_EQ(next.audio_index, 3);
  CHECK_EQ(next.subtitle_index, 5);
}

// Text subtitles: downloaded separately as SRT (ASS converted), image refused, and
// the stream is neither requested with subtitles nor reopened when the
// text track changes (direct streaming remains possible).
void test_client_subtitles_keep_the_stream() {
  reset_server();
  api::MediaItem item;
  CHECK(fetch(kVostfr, &item));
  const api::MediaSourceInfo& src = item.media_sources[0];
  api::TrackSelection sel = default_tracks(item);  // audio 1, forced 6 (text)
  CHECK(sel.subtitle_delivery == api::SubtitleDelivery::Client);

  std::string srt;
  CHECK(g_env->client->fetch_subtitle(kVostfr, src.id, 6, &srt).ok());
  std::vector<util::SubtitleCue> cues;
  CHECK(util::parse_srt(srt, &cues));
  CHECK_EQ(cues.size(), 8u);  // one cue every 5 s over the 40 s of the test media
  CHECK(cues[0].start_ms == 0 && cues[0].end_ms == 3500);
  CHECK(cues[0].text == "[fre#6] cue 0");
  // ASS converted by the server: position and italic tags removed.
  CHECK(g_env->client->fetch_subtitle(kVostfr, src.id, 7, &srt).ok());
  CHECK(util::parse_srt(srt, &cues));
  CHECK(cues[1].text == "In italics");
  CHECK(cues[2].text == "Two lines bold\n[fre#7] continued");
  // Image (PGS): the server cannot extract it as text -> burn-in mandatory.
  const api::ApiResult pgs = g_env->client->fetch_subtitle(kVostfr, src.id, 8, &srt);
  CHECK(pgs.error == api::ApiError::Http && pgs.http_status == 404);
  CHECK(api::find_stream(src, 8) && !api::find_stream(src, 8)->is_text);
  testnet::ServerState st = server_state();
  CHECK_EQ(st.subtitle_downloads.size(), 3u);

  // Playback with client subtitles: a single stream request, without a
  // subtitle parameter; changing the text track reopens nothing.
  Rig r(kVostfr, item, sel);
  CHECK(r.open());
  r.run_for(1500);
  const api::TrackSelection other = api::with_subtitle(src, sel, 4);
  CHECK(other.subtitle_delivery == api::SubtitleDelivery::Client);
  CHECK(!api::stream_must_reopen(sel, other));
  r.locator->set_tracks(other);  // instant change: no reopen_at_position()
  r.run_for(1500);
  CHECK(!r.player.seek_in_flight());
  CHECK(r.video.corner_v < 140);
  st = server_state();
  CHECK_EQ(st.requests.size(), 1u);
  if (!st.requests.empty()) {
    CHECK(st.requests[0].subtitle_index.empty());
    CHECK(st.requests[0].subtitle_method.empty());
    CHECK(!st.requests[0].burn);
  }
  r.player.stop();
}

bool contains(const std::string& s, const std::string& part) {
  return s.find(part) != std::string::npos;
}

void test_cli_tracks() {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  setenv("SDL_AUDIODRIVER", "dummy", 1);
  setenv("JELLYFIN_URL", g_env->server.base_url().c_str(), 1);
  setenv("JELLYFIN_USER", "test", 1);
  setenv("JELLYFIN_PASS", "test", 1);
  const std::string log_path = testnet::workdir() + "/tracks_cli.log";
  reset_server();

  CHECK_EQ(testnet::run_process({PELAGIA_PLAY_BIN, "--item", kVostfr, "--list-tracks"}, log_path,
                                30000), 0);
  std::string out = testnet::read_file(log_path);
  CHECK(contains(out, "[1] audio      English (original) · E-AC3 5.1  (file default)  <- chosen"));
  CHECK(contains(out, "[3] audio      Japanese · AAC Mono · Commentary"));
  CHECK(contains(out, "[6] subtitle   French · SRT · forced  [text, rendered by the client]  <- chosen"));
  CHECK(contains(out, "[8] subtitle   French · PGS · SDH  [burned in by the server]"));

  CHECK_EQ(testnet::run_process({PELAGIA_PLAY_BIN, "--item", k1917, "--list-tracks"}, log_path,
                                30000), 0);
  out = testnet::read_file(log_path);
  CHECK(contains(out, "<- chosen"));
  CHECK(contains(out, "[4] subtitle   French · PGS  [burned in by the server]  (file default)  <- chosen"));

  // Wrong index: code 2, without starting anything.
  CHECK_EQ(testnet::run_process({PELAGIA_PLAY_BIN, "--item", kVostfr, "--audio", "6",
                                 "--list-tracks"}, log_path, 30000), 2);
  CHECK(contains(testnet::read_file(log_path), "not an audio track"));
  CHECK_EQ(testnet::run_process({PELAGIA_PLAY_BIN, "--audio", "x", "--item", kVostfr}, log_path,
                                10000), 2);
  CHECK_EQ(testnet::run_process({PELAGIA_PLAY_BIN, "--audio", "1", "fichier.mp4"}, log_path,
                                10000), 2);
}

}  // namespace

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
  if (!env.server.start(media, {"--min-resume-duration", "0", "--catalog", "tracks"})) {
    CHECK(!"fake server start");
    return testfw::test_failures();
  }
  env.control.set_timeouts(3, 5);
  env.reporting.set_timeouts(3, 5);
  env.client.reset(new api::JellyfinClient(env.transport, env.server.base_url(),
                                           "pelagia-test"));
  CHECK(env.client->authenticate("test", "test").ok());

  RUN(test_profile_and_defaults);
  RUN(test_stream_follows_chosen_audio);
  RUN(test_switch_audio_during_playback);
  RUN(test_burned_subtitles);
  RUN(test_reports_remembered_by_server);
  RUN(test_client_subtitles_keep_the_stream);
  RUN(test_cli_tracks);
  return testfw::test_failures();
}
