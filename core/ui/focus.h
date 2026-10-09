// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UI_FOCUS_H
#define PELAGIA_CORE_UI_FOCUS_H

// Gamepad focus and scrolling logic, without rendering (tested).
// Common rules: no wrapping at the edges (you always know where you are),
// an impossible direction returns false (the screen can then pass the focus
// to another area).

#include <vector>

namespace ui {

enum class Dir { Up, Down, Left, Right };

// Vertical list (seasons, episodes, menus, sign-in fields).
class ListFocus {
 public:
  void set_count(int count);
  int count() const { return count_; }
  int index() const { return index_; }
  void set_index(int index);
  bool move(Dir dir);  // Up/Down only
  // First visible element so that index() stays visible (visible >= 1).
  int scroll_top(int visible);

 private:
  int count_ = 0;
  int index_ = 0;
  int top_ = 0;
};

// Poster grid of N columns, scrolling by rows.
class GridFocus {
 public:
  GridFocus(int columns = 7) : columns_(columns) {}
  void set_count(int count);
  int count() const { return count_; }
  int columns() const { return columns_; }
  int index() const { return index_; }
  void set_index(int index);
  int row() const { return index_ / columns_; }
  int col() const { return index_ % columns_; }
  int rows() const { return (count_ + columns_ - 1) / columns_; }
  // Down from a row above an incomplete last row:
  // last element (you do not stay stuck above a hole).
  bool move(Dir dir);
  // First visible row so that the focus row stays visible.
  int scroll_row(int visible_rows);

 private:
  int columns_;
  int count_ = 0;
  int index_ = 0;
  int top_row_ = 0;
};

// Horizontal rows of different sizes (home). Each row keeps
// its position when you leave it and come back.
class RowsFocus {
 public:
  void set_rows(const std::vector<int>& counts);
  int row() const { return row_; }
  int index() const;          // position in the current row
  int index_in(int row) const;
  int row_count() const { return static_cast<int>(counts_.size()); }
  bool move(Dir dir);         // empty rows skipped
  void set_position(int row, int index);
  // First visible element of a row that displays visible elements.
  int scroll_in(int row, int visible);

 private:
  std::vector<int> counts_;
  std::vector<int> indexes_;
  std::vector<int> scrolls_;
  int row_ = 0;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_FOCUS_H
