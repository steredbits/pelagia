// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Tests of the focus logic (list, grid, rows) and scrolling.

#include "test_framework.h"
#include "ui/focus.h"

using ui::Dir;

static void test_list() {
  ui::ListFocus l;
  CHECK(!l.move(Dir::Down));  // liste vide
  l.set_count(3);
  CHECK(!l.move(Dir::Up));    // no wrapping
  CHECK(l.move(Dir::Down) && l.move(Dir::Down));
  CHECK(!l.move(Dir::Down));
  CHECK_EQ(l.index(), 2);
  CHECK(!l.move(Dir::Left));
  l.set_count(1);             // shortened list: focus brought back
  CHECK_EQ(l.index(), 0);
  l.set_count(10);
  l.set_index(7);
  CHECK_EQ(l.scroll_top(4), 4);  // 7 visible at the bottom of the window
  l.set_index(5);
  CHECK_EQ(l.scroll_top(4), 4);  // already visible: no jump
  l.set_index(1);
  CHECK_EQ(l.scroll_top(4), 1);
}

static void test_grid() {
  ui::GridFocus g(7);
  g.set_count(16);  // rows of 7, 7, 2
  CHECK_EQ(g.rows(), 3);
  CHECK(!g.move(Dir::Left) && !g.move(Dir::Up));
  g.set_index(6);
  CHECK(!g.move(Dir::Right));  // bord droit
  CHECK(g.move(Dir::Down));
  CHECK_EQ(g.index(), 13);
  CHECK(g.move(Dir::Down));    // incomplete last row: last element
  CHECK_EQ(g.index(), 15);
  CHECK(!g.move(Dir::Down));
  CHECK(!g.move(Dir::Right));  // no element to the right
  CHECK(g.move(Dir::Up));
  CHECK_EQ(g.index(), 8);
  CHECK_EQ(g.scroll_row(2), 0);
  g.set_index(15);
  CHECK_EQ(g.scroll_row(2), 1);
  g.set_index(0);
  CHECK_EQ(g.scroll_row(2), 0);
  g.set_count(3);
  CHECK(!g.move(Dir::Down));   // a single row
}

static void test_rows() {
  ui::RowsFocus r;
  r.set_rows({3, 0, 5});
  CHECK_EQ(r.row(), 0);
  CHECK(r.move(Dir::Down));    // empty row skipped
  CHECK_EQ(r.row(), 2);
  CHECK(r.move(Dir::Right) && r.move(Dir::Right));
  CHECK(r.move(Dir::Up));
  CHECK_EQ(r.index(), 0);
  CHECK(r.move(Dir::Down));
  CHECK_EQ(r.index(), 2);      // remembered position
  CHECK(!r.move(Dir::Down));
  r.set_rows({0, 0, 5});       // current row kept
  CHECK_EQ(r.row(), 2);
  r.set_rows({0, 4});          // row disappeared: first non-empty one
  CHECK_EQ(r.row(), 1);
  r.set_rows({20});
  r.set_position(0, 12);
  CHECK_EQ(r.scroll_in(0, 7), 6);
  r.set_rows({});
  CHECK(!r.move(Dir::Down));
}

int main() {
  test_list();
  test_grid();
  test_rows();
  return testfw::test_failures();
}
