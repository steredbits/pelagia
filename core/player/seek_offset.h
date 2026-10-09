// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_PLAYER_SEEK_OFFSET_H
#define PELAGIA_CORE_PLAYER_SEEK_OFFSET_H

// Mapping between the stream PTS and the displayed media position.
//
// - Absolute-timestamp source (local file): the PTS are already
//   media positions, the offset is fixed (-start_time of the container).
// - Relative-timestamp source (Jellyfin stream reopened with
//   startTimeTicks): we do NOT assume the PTS restart from 0.
//   The offset is recomputed at the first PTS received after each seek:
//   offset = requested_target - first_pts. A stream restarting from 0 or from
//   any value (muxer TS offset, etc.) gives the same position.

#include <cstdint>

namespace player {

class SeekOffsetTracker {
 public:
  // `timestamps_absolute`: true if the stream PTS are media positions.
  // `static_offset_ms`: fixed offset for absolute sources
  // (typically -start_time_ms of the container, 0 for a regular MP4).
  void init(bool timestamps_absolute, int64_t static_offset_ms);

  // To be called at each seek (and at opening, with the start position).
  void on_seek(int64_t target_ms);

  // Converts a stream PTS into a media position. At the first call after an
  // on_seek() on a non-absolute source, freezes the offset.
  int64_t media_time(int64_t stream_pts_ms);

  int64_t offset_ms() const { return offset_; }

 private:
  bool absolute_ = true;
  int64_t static_offset_ = 0;
  int64_t offset_ = 0;
  int64_t pending_target_ = -1;  // target waiting for the first PTS (non-absolute)
};

}  // namespace player

#endif  // PELAGIA_CORE_PLAYER_SEEK_OFFSET_H
