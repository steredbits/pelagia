// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// SRT parsing and lookup of the active cues.

#include "util/subtitles.h"

#include <string>
#include <vector>

#include "test_framework.h"

using util::SubtitleCue;

static void test_basic_srt() {
  const std::string srt =
      "1\n00:00:02,000 --> 00:00:05,000\nBonjour\n\n"
      "2\n00:00:07,500 --> 00:00:09,250\nDeux lignes\nsuite\n\n"
      "3\n01:02:03,004 --> 01:02:04,999\nFin\n";
  std::vector<SubtitleCue> cues;
  CHECK(util::parse_srt(srt, &cues));
  CHECK_EQ(cues.size(), 3u);
  CHECK_EQ(cues[0].start_ms, 2000);
  CHECK_EQ(cues[0].end_ms, 5000);
  CHECK(cues[0].text == "Bonjour");
  CHECK_EQ(cues[1].start_ms, 7500);
  CHECK_EQ(cues[1].end_ms, 9250);
  CHECK(cues[1].text == "Deux lignes\nsuite");
  CHECK_EQ(cues[2].start_ms, ((1 * 60 + 2) * 60 + 3) * 1000 + 4);
  CHECK_EQ(cues[2].end_ms, ((1 * 60 + 2) * 60 + 4) * 1000 + 999);
}

static void test_formats_and_tolerance() {
  // BOM, CRLF, lone CR, missing numbers, "." before the ms, ms on 1 digit, coordinates.
  std::string srt = "\xEF\xBB\xBF" "00:00:01.5 --> 00:00:02.5 X1:10 X2:20\r\nSans numéro\r\n\r\n"
                    "7\r00:00:03,000 --> 00:00:04,000\rMac\r\r"
                    "texte sans horaire\n\n"
                    "9\n00:00:06,000 --> 00:00:05,000\nFin avant le début\n\n"
                    "10\n00:00:08,000 --> 00:00:09,000\nOK\n";
  std::vector<SubtitleCue> cues;
  CHECK(util::parse_srt(srt, &cues));
  CHECK_EQ(cues.size(), 3u);
  CHECK_EQ(cues[0].start_ms, 1500);
  CHECK(cues[0].text == "Sans numéro");
  CHECK(cues[1].text == "Mac");
  CHECK(cues[2].text == "OK");

  // Nothing usable: false, empty output; empty input too.
  CHECK(!util::parse_srt("pas un fichier srt\n", &cues));
  CHECK(cues.empty());
  CHECK(!util::parse_srt("", &cues));
  // Unordered cues: sorted by start.
  CHECK(util::parse_srt("1\n00:00:10,000 --> 00:00:11,000\nB\n\n2\n00:00:01,000 --> 00:00:02,000\nA\n",
                        &cues));
  CHECK(cues[0].text == "A" && cues[1].text == "B");
}

static void test_text_cleaning() {
  std::vector<SubtitleCue> cues;
  // Output of the server's ASS -> SRT conversion: position, italic, bold, font, entities.
  const std::string srt =
      "1\n00:00:01,000 --> 00:00:02,000\n{\\an8}<i>En italique</i>\n\n"
      "2\n00:00:03,000 --> 00:00:04,000\n<font color=\"#ff0000\"><b>Rouge</b></font> &amp; bleu &lt;3&nbsp;!\n\n"
      "3\n00:00:05,000 --> 00:00:06,000\nUne\\Ndeux{\\i1} trois\n\n"
      "4\n00:00:07,000 --> 00:00:08,000\n  \xE2\x80\x8E" "Invisible au début  \n\n"
      "5\n00:00:09,000 --> 00:00:10,000\n<i></i>\n\n"
      "6\n00:00:11,000 --> 00:00:12,000\nUn 1 < 2 et 3 > 2\n";
  CHECK(util::parse_srt(srt, &cues));
  CHECK_EQ(cues.size(), 5u);  // cue 5 is empty after cleaning
  CHECK(cues[0].text == "En italique");
  CHECK(cues[1].text == "Rouge & bleu <3 !");
  CHECK(cues[2].text == "Une\ndeux trois");
  CHECK(cues[3].text == "Invisible au début");
  CHECK(cues[4].text == "Un 1 < 2 et 3 > 2");  // "<" followed by a digit is not a tag
}

static void test_active_cues() {
  util::SubtitleTrack track;
  CHECK(track.active_at(1000).empty());  // empty track
  std::vector<SubtitleCue> cues;
  CHECK(util::parse_srt(
      "1\n00:00:02,000 --> 00:00:05,000\nA\n\n"
      "2\n00:00:04,000 --> 00:00:06,000\nB\n\n"
      "3\n00:00:20,000 --> 00:00:21,000\nC\n\n"
      "4\n00:00:00,500 --> 00:01:00,000\nLongue\n", &cues));
  track.set_cues(cues);
  CHECK_EQ(track.size(), 4u);
  auto texts = [&](int64_t pos) {
    std::string out;
    for (const SubtitleCue* c : track.active_at(pos)) {
      if (!out.empty()) out += "|";
      out += c->text;
    }
    return out;
  };
  CHECK(texts(0).empty());
  CHECK(texts(499).empty());
  CHECK(texts(500) == "Longue");       // start included
  CHECK(texts(1999) == "Longue");
  CHECK(texts(2000) == "Longue|A");
  CHECK(texts(4500) == "Longue|A|B");  // overlap: by increasing start
  CHECK(texts(5000) == "Longue|B");    // fin exclue
  CHECK(texts(6000) == "Longue");
  CHECK(texts(20500) == "Longue|C");
  CHECK(texts(59999) == "Longue");
  CHECK(texts(60000).empty());
  CHECK(texts(3600000).empty());
}

int main() {
  test_basic_srt();
  test_formats_and_tolerance();
  test_text_cleaning();
  test_active_cues();
  return testfw::test_failures();
}
