// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "ui/virtual_keyboard.h"

#include "ui/painter.h"
#include "ui/theme.h"
#include "util/i18n.h"
#include "util/text.h"

namespace ui {

namespace {

using platform::InputEvent;

const char* const kDigits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
const char* const kSymbolRows[3][10] = {
    {"!", "?", "#", "$", "%", "&", "*", "+", "=", "~"},
    {"(", ")", "[", "]", "{", "}", "<", ">", "'", "\""},
    {";", ",", "^", "`", "|", "\\", "ê", "ù", "ô", "î"}};

// The 10 characters (UTF-8) of a letter row, from the string catalog.
std::vector<std::string> split_row(const char* text) {
  const std::string s = text;
  std::vector<std::string> out;
  size_t pos = 0;
  while (pos < s.size()) {
    std::string ch;
    util::utf8_append(&ch, util::utf8_next(s, &pos));
    out.push_back(ch);
  }
  return out;
}

}  // namespace

VirtualKeyboard::VirtualKeyboard() { build_layouts(); }

// The letter layout (AZERTY / QWERTY) and the key names come from the string
// catalog: rebuilt when the interface language changes.
void VirtualKeyboard::build_layouts() const {
  built_language_ = util::current_language();
  using util::Str;
  const Str kLower[3] = {Str::KbLower1, Str::KbLower2, Str::KbLower3};
  const Str kUpper[3] = {Str::KbUpper1, Str::KbUpper2, Str::KbUpper3};
  auto build = [](const Str* letters, const char* const symbol_rows[3][10],
                  const char* toggle_label, Action toggle) {
    Rows rows;
    std::vector<Key> digits;
    for (const char* d : kDigits) digits.push_back(Key{d, d, Action::Insert, 1});
    rows.push_back(digits);
    for (int r = 0; r < 3; ++r) {
      std::vector<Key> row;
      if (letters) {
        for (const std::string& ch : split_row(util::tr(letters[r]))) {
          row.push_back(Key{ch, ch, Action::Insert, 1});
        }
      } else {
        for (int c = 0; c < 10; ++c) {
          row.push_back(Key{symbol_rows[r][c], symbol_rows[r][c], Action::Insert, 1});
        }
      }
      rows.push_back(row);
    }
    // Punctuation of addresses and user names ("http://192.168.1.2:8096").
    rows.push_back({Key{util::tr(Str::KbShift), "", Action::Shift, 2}, Key{".", ".", Action::Insert, 1},
                    Key{"-", "-", Action::Insert, 1}, Key{"_", "_", Action::Insert, 1},
                    Key{"@", "@", Action::Insert, 1}, Key{"/", "/", Action::Insert, 1},
                    Key{":", ":", Action::Insert, 1}, Key{util::tr(Str::KbErase), "", Action::Erase, 2}});
    rows.push_back({Key{toggle_label, "", toggle, 2}, Key{util::tr(Str::KbSpace), " ", Action::Space, 5},
                    Key{util::tr(Str::KbDone), "", Action::Done, 3}});
    return rows;
  };
  lower_ = build(kLower, nullptr, "&?123", Action::Symbols);
  upper_ = build(kUpper, nullptr, "&?123", Action::Symbols);
  symbols_ = build(nullptr, kSymbolRows, "abc", Action::Symbols);
}

const VirtualKeyboard::Rows& VirtualKeyboard::rows() const {
  if (built_language_ != util::current_language()) {
    build_layouts();
  }
  return layout_ == Layout::Upper ? upper_ : (layout_ == Layout::Symbols ? symbols_ : lower_);
}

size_t VirtualKeyboard::row_count() const { return rows().size(); }

size_t VirtualKeyboard::key_count(int row) const { return rows()[row].size(); }

std::string VirtualKeyboard::focused_label() const { return rows()[row_][col_].label; }

void VirtualKeyboard::open(std::string* target, const std::string& title, bool secret,
                           size_t max_bytes) {
  target_ = target;
  title_ = title;
  secret_ = secret;
  max_bytes_ = max_bytes;
  layout_ = Layout::Lower;
  row_ = 1;  // first row of letters
  col_ = 0;
}

int VirtualKeyboard::key_center(int row, int col) const {
  int start = 0;
  for (int c = 0; c < col; ++c) start += rows()[row][c].width;
  return 2 * start + rows()[row][col].width;
}

void VirtualKeyboard::clamp_col() {
  const int n = static_cast<int>(rows()[row_].size());
  if (col_ >= n) col_ = n - 1;
}

void VirtualKeyboard::move_vertical(int step) {
  const int target = row_ + step;
  if (target < 0 || target >= static_cast<int>(rows().size())) {
    return;
  }
  // Key of the target row that covers the center of the current key.
  const int center = key_center(row_, col_);
  int start = 0;
  int pick = 0;
  for (size_t c = 0; c < rows()[target].size(); ++c) {
    const int end = start + rows()[target][c].width;
    // Center on a boundary (wide key above two keys):
    // the left key.
    if (center > 2 * start && center <= 2 * end) {
      pick = static_cast<int>(c);
      break;
    }
    start = end;
  }
  row_ = target;
  col_ = pick;
}

void VirtualKeyboard::insert(const std::string& text) {
  if (target_->size() + text.size() <= max_bytes_) {
    target_->append(text);
  }
}

void VirtualKeyboard::erase() { util::utf8_pop_back(target_); }

VirtualKeyboard::Result VirtualKeyboard::activate(const Key& key) {
  switch (key.action) {
    case Action::Insert:
    case Action::Space:
      insert(key.text);
      return Result::Edited;
    case Action::Erase:
      erase();
      return Result::Edited;
    case Action::Shift:
      layout_ = layout_ == Layout::Upper ? Layout::Lower : Layout::Upper;
      return Result::None;
    case Action::Symbols:
      layout_ = layout_ == Layout::Symbols ? Layout::Lower : Layout::Symbols;
      clamp_col();
      return Result::None;
    case Action::Done:
      close();
      return Result::Done;
  }
  return Result::None;
}

VirtualKeyboard::Result VirtualKeyboard::handle(const platform::Input& in) {
  if (!is_open()) {
    return Result::None;
  }
  const int n = static_cast<int>(rows()[row_].size());
  switch (in.event) {
    case InputEvent::Up: move_vertical(-1); return Result::None;
    case InputEvent::Down: move_vertical(1); return Result::None;
    case InputEvent::Left: col_ = (col_ + n - 1) % n; return Result::None;    // bouclage
    case InputEvent::Right: col_ = (col_ + 1) % n; return Result::None;
    case InputEvent::Ok: return activate(rows()[row_][col_]);
    case InputEvent::Erase: erase(); return Result::Edited;
    case InputEvent::SeekFwd: insert(" "); return Result::Edited;
    case InputEvent::PlayPause: close(); return Result::Done;
    case InputEvent::Back:
    case InputEvent::Menu: close(); return Result::Closed;
    case InputEvent::Text: {
      std::string s;
      util::utf8_append(&s, in.codepoint);
      insert(s);
      return Result::Edited;
    }
    default: return Result::None;
  }
}

void VirtualKeyboard::draw(Painter& p) const {
  if (!is_open()) {
    return;
  }
  const int unit = 150;
  const int key_h = 72;
  const int gap = 10;
  const int panel_h = 150 + static_cast<int>(rows().size()) * (key_h + gap) + 60;
  const int top = theme::kScreenH - panel_h;
  p.fill(rect(0, 0, theme::kScreenW, top), theme::scrim());
  p.fill(rect(0, top, theme::kScreenW, panel_h), rgba(26, 29, 40));
  const int left = (theme::kScreenW - 10 * unit) / 2;
  // Field title and current value (masked for a password).
  p.label(title_, left, top + 24, Weight::Bold, theme::kBody, theme::text_dim());
  std::string value = *target_;
  if (secret_) {
    value.clear();
    for (size_t i = 0; i < util::utf8_length(*target_); ++i) value += "\xE2\x80\xA2";
  }
  const Rect field = rect(left, top + 66, 10 * unit - gap, 60);
  p.fill(field, theme::surface());
  const std::string shown =
      p.text().ellipsize(value, Weight::Regular, theme::kBody, field.w - 60);
  const int w = p.label(shown, field.x + 16, field.y + 10, Weight::Regular, theme::kBody,
                        theme::text());
  if ((p.now() / 500) % 2 == 0) {
    p.fill(rect(field.x + 20 + w, field.y + 12, 3, 36), theme::accent());  // curseur
  }
  int y = top + 150;
  for (size_t r = 0; r < rows().size(); ++r) {
    int x = left;
    for (size_t c = 0; c < rows()[r].size(); ++c) {
      const Key& k = rows()[r][c];
      const Rect kr = rect(x, y, k.width * unit - gap, key_h);
      const bool focused = static_cast<int>(r) == row_ && static_cast<int>(c) == col_;
      const bool active = k.action == Action::Shift && layout_ == Layout::Upper;
      p.fill(kr, focused ? theme::text() : (active ? theme::accent() : theme::surface()));
      p.label_in(k.label, kr, k.width > 1 ? Weight::Regular : Weight::Bold,
                 k.width > 1 ? theme::kSmall : theme::kBody,
                 focused ? theme::background() : theme::text(), Align::Center);
      x += k.width * unit;
    }
    y += key_h + gap;
  }
  p.label_in(util::tr(util::Str::KbHint),
             rect(left, y + 6, 10 * unit, 40), Weight::Regular, theme::kSmall, theme::text_dim(),
             Align::Center);
}

}  // namespace ui
