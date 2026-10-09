// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Tests of text rendering: coverage of the embedded font (titles of the
// real library), measurement, truncation, line wrapping, atlas.

#include <string>
#include <vector>

#include "test_framework.h"
#include "ui/soft_canvas.h"
#include "ui/text_renderer.h"
#include "util/text.h"

using ui::TextRenderer;
using ui::Weight;

static void test_coverage(TextRenderer& tr) {
  // Required characters: Basic Latin, Latin-1, Latin Extended-A, general
  // punctuation and common symbols. No CJK.
  const uint32_t required[] = {
      'A', 'z', '0', '~', 0xE9, 0xE8, 0xEA, 0xE0, 0xE7, 0xF9, 0xFB, 0xEE, 0xF4, 0xEB,
      0xC9, 0xC0, 0x152, 0x153, 0x178, 0x160, 0x17E, 0x141,
      0x2010, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026, 0x2022,
      0xAE, 0x2122, 0x20AC, 0xAB, 0xBB, 0xB0, 0xA9, 0xFFFD};
  for (uint32_t cp : required) {
    if (!tr.covers(cp, Weight::Regular) || !tr.covers(cp, Weight::Bold)) {
      std::fprintf(stderr, "glyphe manquant : U+%04X\n", cp);
      CHECK(false);
    }
  }
  CHECK(!tr.covers(0x4E2D, Weight::Regular));  // 中: out of scope for v1
}

static void test_measure(TextRenderer& tr) {
  CHECK_EQ(tr.measure("", Weight::Regular, 32), 0);
  const int a = tr.measure("Amélie", Weight::Regular, 32);
  CHECK(a > 60 && a < 140);
  CHECK(tr.measure("Amélie", Weight::Bold, 32) >= a);
  CHECK(tr.measure("Amélie", Weight::Regular, 64) > a * 3 / 2);
  // Invisible characters take no room (even without prior cleaning).
  CHECK_EQ(tr.measure("\xE2\x80\x8E" "Amélie", Weight::Regular, 32), a);
  // Uncovered character: fallback glyph, not a zero width.
  CHECK(tr.measure("\xE4\xB8\xAD", Weight::Regular, 32) > 0);
  CHECK(tr.line_height(Weight::Regular, 32) > 32);
  CHECK(tr.ascent(Weight::Regular, 32) > 20);
}

static void test_ellipsize(TextRenderer& tr) {
  const std::string title = "Un titre extrêmement long pour vérifier la troncature";
  CHECK(tr.ellipsize("Court", Weight::Regular, 28, 400) == "Court");
  const std::string cut = tr.ellipsize(title, Weight::Regular, 28, 200);
  CHECK(cut != title);
  CHECK(tr.measure(cut, Weight::Regular, 28) <= 200);
  CHECK(cut.size() >= 3 && cut.compare(cut.size() - 3, 3, "\xE2\x80\xA6") == 0);
  // Never a cut in the middle of a UTF-8 sequence.
  const std::string accents = tr.ellipsize("ééééééééééééééééééé", Weight::Regular, 28, 90);
  size_t pos = 0;
  while (pos < accents.size()) {
    CHECK(util::utf8_next(accents, &pos) != util::kReplacementChar);
  }
}

static void test_wrap(TextRenderer& tr) {
  const std::string text =
      "Un résumé assez long qui doit tenir sur plusieurs lignes dans la fiche du film, "
      "avec des accents et des apostrophes l’air de rien.";
  const std::vector<std::string> lines = tr.wrap(text, Weight::Regular, 28, 400, 0);
  CHECK(lines.size() >= 3);
  for (const std::string& l : lines) {
    CHECK(tr.measure(l, Weight::Regular, 28) <= 400);
    CHECK(!l.empty() && l.front() != ' ' && l.back() != ' ');
  }
  const std::vector<std::string> two = tr.wrap(text, Weight::Regular, 28, 400, 2);
  CHECK_EQ(two.size(), 2u);
  CHECK(two[1].compare(two[1].size() - 3, 3, "\xE2\x80\xA6") == 0);
  CHECK(tr.measure(two[1], Weight::Regular, 28) <= 400);
  // Word longer than the line: cut, nothing overflows.
  const std::vector<std::string> word =
      tr.wrap("Anticonstitutionnellement", Weight::Regular, 40, 150, 0);
  CHECK(word.size() >= 2);
  for (const std::string& l : word) {
    CHECK(tr.measure(l, Weight::Regular, 40) <= 150);
  }
  CHECK(tr.wrap("", Weight::Regular, 28, 400, 3).empty());
}

static void test_draw() {
  ui::SoftCanvas canvas(400, 100);
  TextRenderer tr(&canvas);
  CHECK(tr.init());
  const int w = tr.draw("F1® l’éveil", 10, 10, Weight::Bold, 40, ui::rgba(255, 255, 255));
  CHECK(w > 100);
  int lit = 0;
  for (int y = 0; y < 100; ++y) {
    for (int x = 0; x < 400; ++x) {
      lit += canvas.pixel_rgb(x, y) != 0;
    }
  }
  CHECK(lit > 300);
  CHECK(canvas.pixel_rgb(10 + w + 20, 40) == 0);  // nothing beyond the width
  CHECK_EQ(canvas.texture_count(), 1u);           // a single atlas
}

static void test_atlas_reset() {
  ui::SoftCanvas canvas(64, 64);
  TextRenderer tr(&canvas, 128);  // tiny atlas: must reset without crashing
  CHECK(tr.init());
  for (int px = 40; px < 80; px += 4) {
    tr.draw("ABCDEFGHIJ", 0, 0, Weight::Regular, px, ui::rgba(255, 255, 255));
  }
  CHECK(tr.atlas_resets() > 0);
  CHECK(tr.measure("ABC", Weight::Regular, 40) > 0);
}

int main() {
  ui::SoftCanvas canvas(16, 16);
  TextRenderer tr(&canvas);
  CHECK(tr.init());
  test_coverage(tr);
  test_measure(tr);
  test_ellipsize(tr);
  test_wrap(tr);
  test_draw();
  test_atlas_reset();
  return testfw::test_failures();
}
