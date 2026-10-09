// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UI_SCREENS_LOGIN_SCREEN_H
#define PELAGIA_CORE_UI_SCREENS_LOGIN_SCREEN_H

// Sign-in: server address, user name, password,
// "Sign in" button. Cross on a field: on-screen keyboard; with a physical keyboard,
// you type directly in the field that has focus. The password is
// never saved or logged.

#include <string>

#include "ui/focus.h"
#include "ui/screen.h"
#include "ui/virtual_keyboard.h"

namespace ui {

class LoginScreen final : public Screen {
 public:
  enum Field { kServer = 0, kUser = 1, kPassword = 2, kSubmit = 3 };

  LoginScreen(const std::string& server, const std::string& message);

  const char* name() const override { return "login"; }
  void handle(App& app, const platform::Input& in) override;
  void draw(App& app, Painter& p) override;
  bool wants_text_input() const override;
  bool busy() const override { return connecting_; }

  const std::string& field(int i) const { return fields_[i]; }
  int focus() const { return focus_.index(); }
  const std::string& message() const { return message_; }
  bool error() const { return error_; }
  bool connecting() const { return connecting_; }
  const VirtualKeyboard& keyboard() const { return keyboard_; }

 private:
  void submit(App& app);
  void edit_field(const platform::Input& in);

  std::string fields_[3];
  ListFocus focus_;
  VirtualKeyboard keyboard_;
  std::string message_;
  bool error_ = false;
  bool connecting_ = false;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_SCREENS_LOGIN_SCREEN_H
