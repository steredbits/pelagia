// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// Tests of the keyboard / gamepad mapping of the Linux platform (pure header).

#include "input_map.h"

#include "test_framework.h"

using platform::InputEvent;
using platform_sdl::map_button;
using platform_sdl::map_key;

static void test_keyboard_navigation() {
  // Outside typing: arrows = navigation (the player turns them into seeks).
  CHECK(map_key(SDLK_LEFT, false, false, false) == InputEvent::Left);
  CHECK(map_key(SDLK_RIGHT, false, false, false) == InputEvent::Right);
  CHECK(map_key(SDLK_RETURN, false, false, false) == InputEvent::Ok);
  CHECK(map_key(SDLK_ESCAPE, false, false, false) == InputEvent::Back);
  CHECK(map_key(SDLK_BACKSPACE, false, false, false) == InputEvent::Back);
  CHECK(map_key(SDLK_SPACE, false, false, false) == InputEvent::PlayPause);
  CHECK(map_key(SDLK_COMMA, false, false, false) == InputEvent::SeekBack);
  CHECK(map_key(SDLK_PERIOD, false, false, false) == InputEvent::SeekFwd);
  CHECK(map_key(SDLK_PAGEUP, false, false, false) == InputEvent::SeekFwdLong);
  CHECK(map_key(SDLK_PAGEDOWN, false, false, false) == InputEvent::SeekBackLong);
  CHECK(map_key(SDLK_TAB, false, false, false) == InputEvent::Menu);
  // Q alone no longer quits; Ctrl+Q quits, while typing or not.
  CHECK(map_key(SDLK_q, false, false, false) == InputEvent::None);
  CHECK(map_key(SDLK_q, true, false, false) == InputEvent::Quit);
  CHECK(map_key(SDLK_q, true, true, false) == InputEvent::Quit);
}

static void test_keyboard_text_mode() {
  // While typing, printable keys are not commands (the text
  // arrives through SDL_TEXTINPUT); Backspace erases instead of going back.
  CHECK(map_key(SDLK_q, false, true, false) == InputEvent::None);
  CHECK(map_key(SDLK_SPACE, false, true, false) == InputEvent::None);
  CHECK(map_key(SDLK_COMMA, false, true, false) == InputEvent::None);
  CHECK(map_key(SDLK_PERIOD, false, true, false) == InputEvent::None);
  CHECK(map_key(SDLK_BACKSPACE, false, true, false) == InputEvent::Erase);
  CHECK(map_key(SDLK_DELETE, false, true, false) == InputEvent::Erase);
  // Navigation keys stay active.
  CHECK(map_key(SDLK_DOWN, false, true, false) == InputEvent::Down);
  CHECK(map_key(SDLK_RETURN, false, true, false) == InputEvent::Ok);
  CHECK(map_key(SDLK_ESCAPE, false, true, false) == InputEvent::Back);
}

static void test_keyboard_repeat() {
  CHECK(map_key(SDLK_DOWN, false, false, true) == InputEvent::Down);
  CHECK(map_key(SDLK_BACKSPACE, false, true, true) == InputEvent::Erase);
  CHECK(map_key(SDLK_RETURN, false, false, true) == InputEvent::None);
  CHECK(map_key(SDLK_ESCAPE, false, false, true) == InputEvent::None);
  CHECK(map_key(SDLK_SPACE, false, false, true) == InputEvent::None);
  CHECK(map_key(SDLK_q, true, false, true) == InputEvent::None);
}

static void test_controller() {
  CHECK(map_button(SDL_CONTROLLER_BUTTON_A, true) == InputEvent::Ok);    // croix
  CHECK(map_button(SDL_CONTROLLER_BUTTON_B, true) == InputEvent::Back);  // rond
  CHECK(map_button(SDL_CONTROLLER_BUTTON_X, true) == InputEvent::Erase); // square
  CHECK(map_button(SDL_CONTROLLER_BUTTON_Y, true) == InputEvent::PlayPause);
  CHECK(map_button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, true) == InputEvent::SeekBack);
  CHECK(map_button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, true) == InputEvent::SeekFwd);
  CHECK(map_button(SDL_CONTROLLER_BUTTON_DPAD_LEFT, false) == InputEvent::Left);
  // START: "Options" on PlayStation, "Start" (pause) elsewhere.
  CHECK(map_button(SDL_CONTROLLER_BUTTON_START, true) == InputEvent::Menu);
  CHECK(map_button(SDL_CONTROLLER_BUTTON_START, false) == InputEvent::PlayPause);
  CHECK(map_button(SDL_CONTROLLER_BUTTON_BACK, false) == InputEvent::Menu);
  CHECK(map_button(SDL_CONTROLLER_BUTTON_GUIDE, true) == InputEvent::None);
}

static void test_axis_edge() {
  platform_sdl::AxisEdge axis;
  CHECK_EQ(axis.update(5000), 0);
  CHECK_EQ(axis.update(20000), 1);   // crossing: a single event
  CHECK_EQ(axis.update(32767), 0);
  CHECK_EQ(axis.update(12000), 0);   // between the thresholds: stays pressed
  CHECK_EQ(axis.update(17000), 0);   // no bounce
  CHECK_EQ(axis.update(3000), 0);    // released
  CHECK_EQ(axis.state(), 0);
  CHECK_EQ(axis.update(17000), 1);
  CHECK_EQ(axis.update(-20000), -1); // direct passage to the other side
  CHECK_EQ(axis.state(), -1);
}

static void test_repeat() {
  platform_sdl::RepeatTracker r(400, 100);
  CHECK(r.poll(0) == InputEvent::None);
  r.press(InputEvent::Down, 1000);
  CHECK(r.poll(1200) == InputEvent::None);  // initial delay
  CHECK(r.poll(1400) == InputEvent::Down);
  CHECK(r.poll(1450) == InputEvent::None);
  CHECK(r.poll(1500) == InputEvent::Down);
  CHECK(r.poll(5000) == InputEvent::Down);  // freeze: only one, no catch-up
  CHECK(r.poll(5001) == InputEvent::None);
  r.release(InputEvent::Up);                // other direction: no effect
  CHECK(r.held() == InputEvent::Down);
  r.release(InputEvent::Down);
  CHECK(r.poll(9000) == InputEvent::None);
}

int main() {
  test_keyboard_navigation();
  test_keyboard_text_mode();
  test_keyboard_repeat();
  test_controller();
  test_axis_edge();
  test_repeat();
  return testfw::test_failures();
}
