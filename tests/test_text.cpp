// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Tests of UTF-8 text: decoding, title cleaning, sort key.

#include <algorithm>
#include <string>
#include <vector>

#include "test_framework.h"
#include "util/text.h"

using util::clean_display_text;
using util::sort_key;

static std::vector<uint32_t> decode_all(const std::string& s) {
  std::vector<uint32_t> out;
  for (size_t pos = 0; pos < s.size();) {
    out.push_back(util::utf8_next(s, &pos));
  }
  return out;
}

static void test_decode() {
  // ASCII, 2, 3 and 4 bytes.
  const std::vector<uint32_t> cps = decode_all("a\xC3\xA9\xE2\x80\x99\xF0\x9F\x8E\xAC");
  CHECK_EQ(cps.size(), 4u);
  CHECK_EQ(cps[0], 'a');
  CHECK_EQ(cps[1], 0xE9u);
  CHECK_EQ(cps[2], 0x2019u);
  CHECK_EQ(cps[3], 0x1F3ACu);

  // Invalid sequences: U+FFFD and we advance one byte (never a loop).
  CHECK(decode_all("\xC3") == std::vector<uint32_t>{0xFFFD});
  CHECK(decode_all("\xFF" "a") == (std::vector<uint32_t>{0xFFFD, 'a'}));
  CHECK(decode_all("\xC0\xAF") == (std::vector<uint32_t>{0xFFFD, 0xFFFD}));  // surlong
  CHECK(decode_all("\xED\xA0\x80").front() == 0xFFFD);                       // surrogate
  CHECK(decode_all("\xE2\x80" "a") == (std::vector<uint32_t>{0xFFFD, 0xFFFD, 'a'}));

  CHECK_EQ(util::utf8_length("Été"), 3u);
}

static void test_append_roundtrip() {
  const uint32_t samples[] = {0x41, 0xE9, 0x2010, 0x20AC, 0x1F3AC};
  for (uint32_t cp : samples) {
    std::string s;
    util::utf8_append(&s, cp);
    size_t pos = 0;
    CHECK_EQ(util::utf8_next(s, &pos), cp);
    CHECK_EQ(pos, s.size());
  }
  std::string bad;
  util::utf8_append(&bad, 0xD800);
  CHECK(bad == "\xEF\xBF\xBD");
}

static void test_pop_back() {
  std::string s = "aé€";
  util::utf8_pop_back(&s);
  CHECK(s == "aé");
  util::utf8_pop_back(&s);
  CHECK(s == "a");
  util::utf8_pop_back(&s);
  CHECK(s.empty());
  util::utf8_pop_back(&s);  // no effect on an empty string
  CHECK(s.empty());
  std::string broken = "a\xE2\x80";  // truncated sequence: one byte at a time
  util::utf8_pop_back(&broken);
  CHECK(broken == "a\xE2");
}

static void test_clean_title() {
  // Finding on a real server: title starting with U+200E.
  CHECK(clean_display_text("\xE2\x80\x8E" "Amélie") == "Amélie");
  CHECK(clean_display_text("\xEF\xBB\xBF  Titre\xE2\x80\x8F ") == "Titre");
  CHECK(clean_display_text("Le\xC2\xA0\xC2\xA0" "Film\t\n2") == "Le Film 2");
  CHECK(clean_display_text("Zero\xE2\x80\x8BWidth") == "ZeroWidth");
  // The visible characters of a real library are kept.
  CHECK(clean_display_text("Crazy Kung\xE2\x80\x90" "Fu") == "Crazy Kung‐Fu");
  CHECK(clean_display_text("L\xE2\x80\x99" "éveil") == "L’éveil");
  CHECK(clean_display_text("F1\xC2\xAE") == "F1®");
  CHECK(clean_display_text("« Été » 20° — 5 €™…") == "« Été » 20° — 5 €™…");
  CHECK(clean_display_text("\xE2\x80\x8E\xE2\x80\x8E").empty());
  CHECK(clean_display_text("a\xFF" "b") == "a\xEF\xBF\xBD" "b");
}

static void test_sort_key() {
  CHECK(sort_key("\xE2\x80\x8E" "Amélie") == "amelie");
  CHECK(sort_key("Été") == "ete");
  CHECK(sort_key("Œuvre ÆSTUS Straße") == "oeuvre aestus strasse");
  CHECK(sort_key("Łódź Ž") == "lodz z");
  CHECK(sort_key("× ÷") == "× ÷");

  // Sort: the title prefixed with U+200E no longer goes to the end of the list.
  std::vector<std::string> titles = {"Zodiac", "\xE2\x80\x8E" "Amélie", "Batman", "élan"};
  std::sort(titles.begin(), titles.end(), [](const std::string& a, const std::string& b) {
    return sort_key(a) < sort_key(b);
  });
  CHECK(clean_display_text(titles[0]) == "Amélie");
  CHECK(titles[1] == "Batman");
  CHECK(titles[2] == "élan");
  CHECK(titles[3] == "Zodiac");
}

int main() {
  test_decode();
  test_append_roundtrip();
  test_pop_back();
  test_clean_title();
  test_sort_key();
  return testfw::test_failures();
}
