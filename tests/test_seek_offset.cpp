// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Tests of the PTS -> media position realignment after a seek.
//
// The "network" scenarios simulate a mocked source: the stream is
// reopened on the server side with startTimeTicks, and we do NOT assume the PTS
// restart from 0 - the offset is frozen at the first PTS received of the new
// generation.

#include "player/seek_offset.h"

#include "test_framework.h"

using player::SeekOffsetTracker;

// Local file: absolute PTS, the offset stays fixed whatever the seek.
static void test_absolute_source() {
  SeekOffsetTracker tr;
  tr.init(/*timestamps_absolute=*/true, /*static_offset_ms=*/0);
  tr.on_seek(0);
  CHECK_EQ(tr.media_time(0), 0);
  CHECK_EQ(tr.media_time(40), 40);

  // Seek to 600 s: av_seek_frame lands on the previous keyframe (598.2 s),
  // the media position remains the PTS itself.
  tr.on_seek(600000);
  CHECK_EQ(tr.media_time(598200), 598200);
  CHECK_EQ(tr.offset_ms(), 0);
}

// Local file whose container does not start at 0 (MPEG-TS, start_time
// 1.4 s): static offset -start_time to display a 0-based position.
static void test_absolute_source_with_start_time() {
  SeekOffsetTracker tr;
  tr.init(true, -1400);
  tr.on_seek(0);
  CHECK_EQ(tr.media_time(1400), 0);
  CHECK_EQ(tr.media_time(1440), 40);
}

// Mocked "network" source, PTS restarting from 0 after the seek.
static void test_network_pts_restart_at_zero() {
  SeekOffsetTracker tr;
  tr.init(/*timestamps_absolute=*/false, 0);
  tr.on_seek(0);
  CHECK_EQ(tr.media_time(0), 0);
  CHECK_EQ(tr.media_time(40), 40);

  // Seek to 600 s: the server restarts the transcoding, first PTS = 0.
  tr.on_seek(600000);
  CHECK_EQ(tr.media_time(0), 600000);
  CHECK_EQ(tr.media_time(40), 600040);
  CHECK_EQ(tr.offset_ms(), 600000);
}

// Mocked "network" source, NON-zero PTS after the seek (TS muxer offset,
// transcoding that does not restart exactly from the target, etc.).
static void test_network_pts_nonzero_after_seek() {
  SeekOffsetTracker tr;
  tr.init(false, 0);
  tr.on_seek(0);
  CHECK_EQ(tr.media_time(1400), 0);  // even the opening is realigned
  CHECK_EQ(tr.media_time(1440), 40);

  // Seek to 600 s: first PTS received 1.4 s -> offset 598.6 s.
  tr.on_seek(600000);
  CHECK_EQ(tr.media_time(1400), 600000);
  CHECK_EQ(tr.media_time(1480), 600080);
  CHECK_EQ(tr.offset_ms(), 598600);

  // Second seek: the offset is frozen again on the new first PTS.
  tr.on_seek(120000);
  CHECK_EQ(tr.media_time(10000), 120000);
  CHECK_EQ(tr.media_time(10040), 120040);
  CHECK_EQ(tr.offset_ms(), 110000);
}

int main() {
  test_absolute_source();
  test_absolute_source_with_start_time();
  test_network_pts_restart_at_zero();
  test_network_pts_nonzero_after_seek();
  return testfw::test_failures();
}
