// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_PLAYER_AV_LOG_BRIDGE_H
#define PELAGIA_CORE_PLAYER_AV_LOG_BRIDGE_H

// Redirects ffmpeg (libav*) logs to util/log, with levels and
// secret filtering. Without this bridge, ffmpeg writes directly to stderr and
// shows the stream URL (api_key) and, at debug level, the HTTP headers
// complets (Token MediaBrowser). Idempotent, thread-safe.

namespace player {

// verbose: ffmpeg debug level (detailed HTTP requests) instead of
// warnings and errors only.
void install_ffmpeg_log_bridge(bool verbose);

// Installs the bridge without touching ffmpeg's level (network source created
// outside pelagia-play: no ffmpeg log must bypass the filter).
void ensure_ffmpeg_log_bridge();

}  // namespace player

#endif  // PELAGIA_CORE_PLAYER_AV_LOG_BRIDGE_H
