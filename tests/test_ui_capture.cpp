// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Complete walk through the interface, in English then in French, against the fake server ("demo" catalog)
// with pelagia-capture: sign-in, home, grid, page, series, player
// (loading, playback, seek, pause), stop then page re-read. Checks
// that each screen is reached, that the PNGs are readable 1920x1080
// images and that the server received the playback stop report.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "fake_server.h"
#include "test_framework.h"
#include "ui/image_codec.h"

namespace {

const char* const kShots[] = {
    "01-sign-in", "02-virtual-keyboard", "03-home", "04-movie-grid",
    "04b-movie-grid-scrolled", "05-movie-page", "06-series-episodes", "07-episode-page",
    "08-options", "08b-about", "08c-language", "09-player-loading", "10-player", "11-player-seeking",
    "12-player-paused", "13-page-after-playback", "14-home-after-playback"};

// "tracks" scenario: page with tracks, lists, "1917" page.
const char* const kTrackShots[] = {"15-movie-tracks", "16-audio-menu",
                                   "17-subtitle-menu", "18-tracks-burned-in",
                                   "19-player-tracks", "20-player-options",
                                   "21-player-audio-list", "22-player-track-change",
                                   "23-page-after-track-change", "24-player-subtitles"};

const char kCrazy[] = "de300000000000000000000000000101";

// Whole walkthrough in one interface language; the fake server serves its demo
// content in the same language.
int run_language(const std::string& media, const std::string& language) {
  testnet::FakeServer server;
  // No duration threshold: the test pattern position (40 s) is recorded.
  const std::string token = "capture0token0secret0123456789ab";
  if (!server.start(media, {"--catalog", "demo", "--language", language, "--min-resume-duration",
                            "0", "--token", token})) {
    return 1;
  }
  const std::string work = testnet::workdir() + "/capture-" + language;
  const std::string out = work + "/png";
  const std::string prepare = "rm -rf '" + work + "' && mkdir -p '" + out + "'";
  CHECK(std::system(prepare.c_str()) == 0);

  const std::string log = work + "/capture.log";
  const int code = testnet::run_process(
      {PELAGIA_CAPTURE_BIN, "--server", server.base_url(), "--work-dir", work + "/state",
       "--out", out, "--language", language},
      log, 90000);
  CHECK_EQ(code, 0);
  if (code != 0) {
    std::fprintf(stderr, "%s\n", testnet::read_file(log).c_str());
  }
  for (const char* name : kShots) {
    const std::string png = testnet::read_file(out + "/" + name + ".png");
    ui::Image img;
    const bool ok = ui::decode_image(png, 0, 0, &img) && img.w == 1920 && img.h == 1080;
    if (!ok) std::fprintf(stderr, "missing or invalid capture : %s\n", name);
    CHECK(ok);
    CHECK(png.size() < 400u * 1024);  // readable on GitHub
  }
  // The token appears in no output of the tool (logs masked).
  CHECK(testnet::read_file(log).find(token) == std::string::npos);
  testnet::ServerState state;
  CHECK(testnet::parse_state(server.state(), &state));
  bool stopped = false;
  for (size_t i = 0; i < state.report_kinds.size(); ++i) {
    if (state.report_kinds[i] == "stopped" &&
        state.report_bodies[i].find(kCrazy) != std::string::npos) {
      stopped = true;
    }
  }
  CHECK(stopped);  // stop report of "Crazy Kung-Fu" received
  CHECK(state.active_jobs == 0);  // no transcoding left on the server

  // Tracks scenario: "tracks" catalog.
  server.stop();
  if (!server.start(media, {"--catalog", "tracks", "--language", language, "--min-resume-duration",
                            "0", "--token", token})) {
    return 1;
  }
  const std::string tracks_log = work + "/capture-tracks.log";
  const int tracks_code = testnet::run_process(
      {PELAGIA_CAPTURE_BIN, "--server", server.base_url(), "--work-dir", work + "/state-tracks",
       "--out", out, "--scenario", "tracks", "--language", language},
      tracks_log, 90000);
  CHECK_EQ(tracks_code, 0);
  if (tracks_code != 0) {
    std::fprintf(stderr, "%s\n", testnet::read_file(tracks_log).c_str());
  }
  for (const char* name : kTrackShots) {
    const std::string png = testnet::read_file(out + "/" + name + ".png");
    ui::Image img;
    const bool ok = ui::decode_image(png, 0, 0, &img) && img.w == 1920 && img.h == 1080;
    if (!ok) std::fprintf(stderr, "missing or invalid capture : %s\n", name);
    CHECK(ok);
    CHECK(png.size() < 400u * 1024);
  }
  CHECK(testnet::read_file(tracks_log).find(token) == std::string::npos);
  return 0;
}

}  // namespace

int main() {
  std::string media;
  if (!testnet::ensure_test_media(&media)) {
    std::fprintf(stderr, "python3/ffmpeg unavailable: test skipped\n");
    return testnet::kSkip;
  }
  CHECK_EQ(run_language(media, "en"), 0);
  CHECK_EQ(run_language(media, "fr"), 0);
  return testfw::test_failures();
}
