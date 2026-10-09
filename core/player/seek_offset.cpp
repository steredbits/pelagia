// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "player/seek_offset.h"

namespace player {

void SeekOffsetTracker::init(bool timestamps_absolute, int64_t static_offset_ms) {
  absolute_ = timestamps_absolute;
  static_offset_ = static_offset_ms;
  offset_ = timestamps_absolute ? static_offset_ms : 0;
  pending_target_ = -1;
}

void SeekOffsetTracker::on_seek(int64_t target_ms) {
  if (absolute_) {
    // PTS stay media positions: nothing to recompute.
    offset_ = static_offset_;
    pending_target_ = -1;
    return;
  }
  pending_target_ = target_ms;
}

int64_t SeekOffsetTracker::media_time(int64_t stream_pts_ms) {
  if (pending_target_ >= 0) {
    offset_ = pending_target_ - stream_pts_ms;
    pending_target_ = -1;
  }
  return stream_pts_ms + offset_;
}

}  // namespace player
