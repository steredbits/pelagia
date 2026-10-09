// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Tests of the playback report decision (PlaybackMonitor), injected
// clock, no network.

#include "player/playback_monitor.h"

#include "test_framework.h"

using player::PlaybackMonitor;
using player::PlaybackObservation;
using player::PlaybackReport;
using player::ReportKind;

namespace {

PlaybackObservation obs(uint64_t now, int64_t pos, bool paused = false,
                        bool seeking = false, const char* session = "s1") {
  PlaybackObservation o;
  o.now_ms = now;
  o.position_ms = pos;
  o.paused = paused;
  o.seek_in_flight = seeking;
  o.session_id = session;
  return o;
}

}  // namespace

int main() {
  PlaybackMonitor monitor(10000);
  std::vector<PlaybackReport> out;

  // Start: a single Started, at the opening position (resume).
  monitor.update(obs(0, 120000), &out);
  CHECK_EQ(out.size(), 1u);
  CHECK(out[0].kind == ReportKind::Started);
  CHECK_EQ(out[0].position_ms, 120000);
  CHECK(out[0].session_id == "s1");

  // Nothing before the interval, then a progress report every 10 s.
  out.clear();
  monitor.update(obs(5000, 125000), &out);
  CHECK(out.empty());
  monitor.update(obs(10000, 130000), &out);
  CHECK_EQ(out.size(), 1u);
  CHECK(out[0].kind == ReportKind::Progress);
  CHECK_EQ(out[0].position_ms, 130000);

  // Pause then resume: immediate, with the state.
  out.clear();
  monitor.update(obs(11000, 131000, true), &out);
  CHECK_EQ(out.size(), 1u);
  CHECK(out[0].paused);
  monitor.update(obs(12000, 131000, true), &out);
  CHECK_EQ(out.size(), 1u);  // no repetition
  monitor.update(obs(13000, 131000, false), &out);
  CHECK_EQ(out.size(), 2u);
  CHECK(!out[1].paused);

  // Seek: nothing while it is in flight (target, session not known yet),
  // then an immediate report at the new position and the new session.
  out.clear();
  monitor.update(obs(14000, 600000, false, true, "s1"), &out);
  monitor.update(obs(25000, 600000, false, true, ""), &out);
  CHECK(out.empty());
  monitor.update(obs(26000, 600100, false, false, "s2"), &out);
  CHECK_EQ(out.size(), 1u);
  CHECK_EQ(out[0].position_ms, 600100);
  CHECK(out[0].session_id == "s2");

  // Track change (here client-rendered subtitles: no seek):
  // immediate report with the indexes, only once.
  out.clear();
  PlaybackObservation with_tracks = obs(27000, 600200, false, false, "s2");
  with_tracks.has_tracks = true;
  with_tracks.media_source_id = "src";
  with_tracks.audio_index = 2;
  with_tracks.subtitle_index = 4;
  monitor.update(with_tracks, &out);
  CHECK_EQ(out.size(), 1u);  // first known tracks
  CHECK(out[0].has_tracks && out[0].audio_index == 2 && out[0].subtitle_index == 4);
  CHECK(out[0].media_source_id == "src");
  with_tracks.now_ms = 27500;
  monitor.update(with_tracks, &out);
  CHECK_EQ(out.size(), 1u);  // nothing changed
  with_tracks.subtitle_index = -1;
  with_tracks.now_ms = 28000;
  monitor.update(with_tracks, &out);
  CHECK_EQ(out.size(), 2u);
  CHECK(out[1].kind == ReportKind::Progress && out[1].subtitle_index == -1);

  // Stop: a single Stopped, nothing afterwards.
  out.clear();
  monitor.stop(obs(27000, 601000, false, false, "s2"), &out);
  monitor.stop(obs(28000, 601000), &out);
  monitor.update(obs(40000, 601000), &out);
  CHECK_EQ(out.size(), 1u);
  CHECK(out[0].kind == ReportKind::Stopped);
  CHECK_EQ(out[0].position_ms, 601000);

  // Stop without start (failed opening): nothing to report.
  PlaybackMonitor never_started;
  out.clear();
  never_started.stop(obs(0, 0), &out);
  CHECK(out.empty());

  return testfw::test_failures();
}
