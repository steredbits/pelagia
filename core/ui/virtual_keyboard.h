// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UI_VIRTUAL_KEYBOARD_H
#define PELAGIA_CORE_UI_VIRTUAL_KEYBOARD_H

// Virtual keyboard (AZERTY or QWERTY depending on the interface language) for gamepad input (server address,
// user name, password). Three layouts: lowercase, uppercase,
// symbols. D-pad navigation (wide keys are located by their
// center), shortcuts: square erases, R1 inserts a space, Start/triangle
// confirms, circle closes. The physical keyboard types directly (Text
// events). Logic tested without rendering; draw() is separate.

#include <cstdint>
#include <string>
#include <vector>

#include "platform.h"
#include "util/i18n.h"

namespace ui {

class Painter;

class VirtualKeyboard {
 public:
  enum class Result { None, Edited, Done, Closed };
  enum class Layout { Lower, Upper, Symbols };

  VirtualKeyboard();

  // target must stay valid as long as the keyboard is open.
  void open(std::string* target, const std::string& title, bool secret,
            size_t max_bytes = 256);
  void close() { target_ = nullptr; }
  bool is_open() const { return target_ != nullptr; }

  Result handle(const platform::Input& in);

  void draw(Painter& p) const;

  // State (tests and drawing).
  Layout layout() const { return layout_; }
  int row() const { return row_; }
  int col() const { return col_; }
  std::string focused_label() const;
  size_t row_count() const;
  size_t key_count(int row) const;

 private:
  enum class Action { Insert, Shift, Symbols, Erase, Space, Done };
  struct Key {
    std::string label;
    std::string text;  // inserted (Action::Insert)
    Action action = Action::Insert;
    int width = 1;     // in units; 10 units per row
  };
  using Rows = std::vector<std::vector<Key>>;

  const Rows& rows() const;
  void build_layouts() const;
  Result activate(const Key& key);
  void insert(const std::string& text);
  void erase();
  void move_vertical(int step);
  void clamp_col();
  int key_center(int row, int col) const;  // in half-units

  mutable Rows lower_, upper_, symbols_;
  mutable util::Lang built_language_ = util::Lang::En;
  Layout layout_ = Layout::Lower;
  int row_ = 1;
  int col_ = 0;
  std::string* target_ = nullptr;
  std::string title_;
  bool secret_ = false;
  size_t max_bytes_ = 256;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_VIRTUAL_KEYBOARD_H
