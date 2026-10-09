// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UTIL_TEXT_H
#define PELAGIA_CORE_UTIL_TEXT_H

// UTF-8 text: decoding, title cleaning for display, and sort key.
//
// Finding from a test on a real server: some titles start with
// invisible format characters (e.g. U+200E LEFT-TO-RIGHT MARK), which
// shifts the display and breaks the sort (the title goes to the end of the list).

#include <cstddef>
#include <cstdint>
#include <string>

namespace util {

constexpr uint32_t kReplacementChar = 0xFFFD;

// Decodes the code point that starts at s[*pos] and advances *pos. An
// invalid sequence (lone byte, truncated, overlong, surrogate) gives
// U+FFFD and advances one byte: decoding never loops.
uint32_t utf8_next(const std::string& s, size_t* pos);

// Appends the UTF-8 encoding of cp (U+FFFD if cp is invalid).
void utf8_append(std::string* out, uint32_t cp);

// Number of code points (invalid sequences counted one per byte).
size_t utf8_length(const std::string& s);

// Removes the last code point (erasing in an input field).
void utf8_pop_back(std::string* s);

// Invisible format character to remove: direction marks (U+200E/F,
// U+202A-U+202E, U+2066-U+2069), zero-width spaces, BOM, soft hyphen
// conditionnel, etc.
bool is_invisible_char(uint32_t cp);

// Space (ASCII, no-break, typographic spaces U+2000-U+200A, U+3000...).
bool is_space_char(uint32_t cp);

// Title ready for display: invisible characters removed, leading and trailing spaces
// removed, runs of spaces reduced to one. Invalid UTF-8 -> U+FFFD.
std::string clean_display_text(const std::string& s);

// Sort key: cleaned text, lowercase, Latin accents removed
// ("Ete" <- "Été", "oeuvre" <- "Œuvre"). Byte comparison.
std::string sort_key(const std::string& s);

}  // namespace util

#endif  // PELAGIA_CORE_UTIL_TEXT_H
