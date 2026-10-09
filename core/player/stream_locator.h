// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_PLAYER_STREAM_LOCATOR_H
#define PELAGIA_CORE_PLAYER_STREAM_LOCATOR_H

// What the network source needs to know about the server, without depending on the Jellyfin
// API: where to request the stream for a given position, and how to release
// the server resources of a playback session. The Jellyfin implementation
// is in core/api/jellyfin_stream (progressive TS + startTimeTicks); a
// switch to HLS would only affect this implementation and NetworkSource.

#include <cstdint>
#include <string>

namespace player {

struct StreamRequest {
  std::string url;
  std::string headers;  // additional HTTP headers, "Name: value\r\n" each
  std::string log_url;  // URL that can be shown in logs (secrets masked)
};

class StreamLocator {
 public:
  virtual ~StreamLocator() {}

  // New identifier for a playback session (one per opening/seek).
  virtual std::string new_session_id() = 0;

  // Prepares the request of the stream starting at start_ms for session_id.
  virtual bool locate(int64_t start_ms, const std::string& session_id,
                      StreamRequest* out) = 0;

  // Releases the stream of session_id on the server side (stops the transcoding job).
  // Best effort, bounded duration: called from the demux thread.
  virtual void release(const std::string& session_id) = 0;
};

}  // namespace player

#endif  // PELAGIA_CORE_PLAYER_STREAM_LOCATOR_H
