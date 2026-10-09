// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UI_SCREENS_SPLASH_SCREEN_H
#define PELAGIA_CORE_UI_SCREENS_SPLASH_SCREEN_H

// Startup with a saved session: checks the token (GET /Users/Me).
// Valid -> home; rejected (401) -> session erased, sign-in screen;
// server unreachable -> "Retry" or "Change server" (the session
// is kept: the server may just be switched off).

#include "ui/focus.h"
#include "ui/screen.h"
#include "ui/session_store.h"

namespace ui {

class SplashScreen final : public Screen {
 public:
  explicit SplashScreen(const SavedSession& session) : session_(session) {}

  const char* name() const override { return "splash"; }
  void enter(App& app) override { restore(app); }
  void handle(App& app, const platform::Input& in) override;
  void draw(App& app, Painter& p) override;
  bool busy() const override { return checking_; }

  bool failed() const { return !checking_; }
  const std::string& message() const { return message_; }
  int focus() const { return focus_.index(); }

 private:
  void restore(App& app);

  SavedSession session_;
  bool checking_ = false;
  std::string message_;
  ListFocus focus_;  // 0: Retry, 1: Change server
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_SCREENS_SPLASH_SCREEN_H
