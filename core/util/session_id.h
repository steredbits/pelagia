// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UTIL_SESSION_ID_H
#define PELAGIA_CORE_UTIL_SESSION_ID_H

#include <string>

namespace util {

// 32-character hexadecimal identifier, unique per call within the
// process and very unlikely to repeat across processes (clock + counter).
// Used as playSessionId: it does not need to be unpredictable, only
// distinct at each playback/seek so that the server does not reuse a job.
std::string make_session_id();

}  // namespace util

#endif  // PELAGIA_CORE_UTIL_SESSION_ID_H
