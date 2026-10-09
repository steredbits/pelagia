// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UTIL_APP_INFO_H
#define PELAGIA_CORE_UTIL_APP_INFO_H

// Application identity: name and device name reported to the Jellyfin server.
// The displayed texts (tagline, license, trademark notice) are in the string
// catalog (util/i18n/strings_*.def).
// Jellyfin trademark rules: the name "Jellyfin" only appears as a
// compatibility mention, never in the application's name.

namespace util {

constexpr const char kAppName[] = "Pelagia";
// Device name shown in the server dashboard.
constexpr const char kDeviceName[] = "Pelagia (PS5)";

}  // namespace util

#endif  // PELAGIA_CORE_UTIL_APP_INFO_H
