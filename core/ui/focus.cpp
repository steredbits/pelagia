// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "ui/focus.h"

namespace ui {

namespace {

int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Minimal scrolling: the window [top, top + visible) follows position.
int follow(int top, int position, int visible, int count) {
  if (visible < 1) {
    visible = 1;
  }
  if (position < top) {
    top = position;
  } else if (position >= top + visible) {
    top = position - visible + 1;
  }
  return clamp(top, 0, count > visible ? count - visible : 0);
}

}  // namespace

void ListFocus::set_count(int count) {
  count_ = count < 0 ? 0 : count;
  index_ = clamp(index_, 0, count_ > 0 ? count_ - 1 : 0);
}

void ListFocus::set_index(int index) { index_ = clamp(index, 0, count_ > 0 ? count_ - 1 : 0); }

bool ListFocus::move(Dir dir) {
  if (dir == Dir::Up && index_ > 0) {
    --index_;
    return true;
  }
  if (dir == Dir::Down && index_ + 1 < count_) {
    ++index_;
    return true;
  }
  return false;
}

int ListFocus::scroll_top(int visible) {
  top_ = follow(top_, index_, visible, count_);
  return top_;
}

void GridFocus::set_count(int count) {
  count_ = count < 0 ? 0 : count;
  index_ = clamp(index_, 0, count_ > 0 ? count_ - 1 : 0);
}

void GridFocus::set_index(int index) { index_ = clamp(index, 0, count_ > 0 ? count_ - 1 : 0); }

bool GridFocus::move(Dir dir) {
  if (count_ == 0) {
    return false;
  }
  switch (dir) {
    case Dir::Left:
      if (col() == 0) return false;
      --index_;
      return true;
    case Dir::Right:
      if (col() == columns_ - 1 || index_ + 1 >= count_) return false;
      ++index_;
      return true;
    case Dir::Up:
      if (row() == 0) return false;
      index_ -= columns_;
      return true;
    case Dir::Down:
      if (row() + 1 >= rows()) return false;
      index_ = index_ + columns_ < count_ ? index_ + columns_ : count_ - 1;
      return true;
  }
  return false;
}

int GridFocus::scroll_row(int visible_rows) {
  top_row_ = follow(top_row_, row(), visible_rows, rows());
  return top_row_;
}

void RowsFocus::set_rows(const std::vector<int>& counts) {
  counts_ = counts;
  indexes_.resize(counts.size(), 0);
  scrolls_.resize(counts.size(), 0);
  for (size_t r = 0; r < counts.size(); ++r) {
    indexes_[r] = clamp(indexes_[r], 0, counts[r] > 0 ? counts[r] - 1 : 0);
  }
  row_ = clamp(row_, 0, counts.empty() ? 0 : static_cast<int>(counts.size()) - 1);
  // Current row empty: first non-empty row.
  if (!counts_.empty() && counts_[row_] == 0) {
    for (size_t r = 0; r < counts_.size(); ++r) {
      if (counts_[r] > 0) {
        row_ = static_cast<int>(r);
        break;
      }
    }
  }
}

int RowsFocus::index() const { return index_in(row_); }

int RowsFocus::index_in(int row) const {
  return row >= 0 && row < row_count() ? indexes_[row] : 0;
}

void RowsFocus::set_position(int row, int index) {
  if (row < 0 || row >= row_count()) {
    return;
  }
  row_ = row;
  indexes_[row] = clamp(index, 0, counts_[row] > 0 ? counts_[row] - 1 : 0);
}

bool RowsFocus::move(Dir dir) {
  if (counts_.empty()) {
    return false;
  }
  if (dir == Dir::Left || dir == Dir::Right) {
    int& i = indexes_[row_];
    if (dir == Dir::Left && i > 0) {
      --i;
      return true;
    }
    if (dir == Dir::Right && i + 1 < counts_[row_]) {
      ++i;
      return true;
    }
    return false;
  }
  const int step = dir == Dir::Up ? -1 : 1;
  for (int r = row_ + step; r >= 0 && r < row_count(); r += step) {
    if (counts_[r] > 0) {
      row_ = r;
      return true;
    }
  }
  return false;
}

int RowsFocus::scroll_in(int row, int visible) {
  if (row < 0 || row >= row_count()) {
    return 0;
  }
  scrolls_[row] = follow(scrolls_[row], indexes_[row], visible, counts_[row]);
  return scrolls_[row];
}

}  // namespace ui
