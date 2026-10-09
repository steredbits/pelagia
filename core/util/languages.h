// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UTIL_LANGUAGES_H
#define PELAGIA_CORE_UTIL_LANGUAGES_H

// Language codes of Jellyfin tracks (ISO 639-2, e.g. "fre", "eng") and of
// user preferences (ISO 639-1 or 639-2, e.g. "fr", "fre", "fra",
// "fr-FR"): comparison and display names. Pure functions.

#include <string>

#include "util/i18n.h"

namespace util {

// Missing or undetermined language: empty, "und", "unknown", "undetermined",
// "mul", "zxx" (same values as the Jellyfin server, case-insensitive).
bool language_undefined(const std::string& code);

// Canonical form for comparison: the lowercase ISO 639-2/B code when the
// language is known ("fr", "fra", "fre", "fr-FR" -> "fre"), otherwise the
// input code in lowercase (without region suffix).
std::string canonical_language(const std::string& code);

// Two codes designate the same language. An empty code matches nothing.
bool same_language(const std::string& a, const std::string& b);

// Display name ("French" / "Français", "English" / "Anglais"...) in the given
// language (or in the interface language). Language unknown to the table: the
// code in uppercase; undetermined: "Unknown language" / "Langue inconnue".
std::string language_name(const std::string& code, Lang lang);
std::string language_name(const std::string& code);

}  // namespace util

#endif  // PELAGIA_CORE_UTIL_LANGUAGES_H
