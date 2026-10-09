// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UI_SCREEN_H
#define PELAGIA_CORE_UI_SCREEN_H

// A UI screen: state machine driven by abstract events.
// Never makes a blocking call: loading goes through App::run().
// Navigation (push/pop) is deferred by App: a screen may request its own
// closing from handle() without being destroyed during the call.

#include "platform.h"
#include "ui/painter.h"
#include "ui/tasks.h"

namespace ui {

class App;

class Screen {
 public:
  virtual ~Screen() {}

  // Nom stable (tests, logs).
  virtual const char* name() const = 0;
  // Added on top of the stack.
  virtual void enter(App& app) { (void)app; }
  // Became the top screen again (return from a child screen).
  virtual void resume(App& app) { (void)app; }
  virtual void handle(App& app, const platform::Input& in) = 0;
  // Once per frame, after the asynchronous results are applied.
  virtual void update(App& app) { (void)app; }
  virtual void draw(App& app, Painter& p) = 0;

  // A text field has focus: the physical keyboard types text.
  virtual bool wants_text_input() const { return false; }
  // Loading or operation in progress (capture mode: wait for the end).
  virtual bool busy() const { return false; }
  // The screen draws the video full screen (no plain background).
  virtual bool shows_video() const { return false; }
  // Frequent pump (player): the loop runs fast and only redraws
  // on request (App::mark_dirty) or at regular intervals.
  virtual bool wants_fast_ticks() const { return false; }

  const Lifetime& lifetime() const { return lifetime_; }

 private:
  Lifetime lifetime_;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_SCREEN_H
