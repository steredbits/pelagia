// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_PLATFORM_LINUX_INPUT_MAP_H
#define PELAGIA_PLATFORM_LINUX_INPUT_MAP_H

// Mapping of SDL keyboard / gamepad -> abstract events, and repetition
// of held directions. Pure logic (SDL constants only, no
// SDL call): tested by tests/test_input_map.cpp.

#include <SDL.h>

#include <cstdint>

#include "platform.h"

namespace platform_sdl {

using platform::InputEvent;

// Keyboard. text_mode: a text field has focus (printable keys are
// then typed through SDL_TEXTINPUT and are not commands). repeat:
// system auto-repeat (key held down).
inline InputEvent map_key(SDL_Keycode key, bool ctrl, bool text_mode, bool repeat) {
  if (ctrl) {
    return (key == SDLK_q && !repeat) ? InputEvent::Quit : InputEvent::None;
  }
  InputEvent ev = InputEvent::None;
  switch (key) {
    case SDLK_UP: ev = InputEvent::Up; break;
    case SDLK_DOWN: ev = InputEvent::Down; break;
    case SDLK_LEFT: ev = InputEvent::Left; break;
    case SDLK_RIGHT: ev = InputEvent::Right; break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: ev = InputEvent::Ok; break;
    case SDLK_ESCAPE:
    case SDLK_AC_BACK: ev = InputEvent::Back; break;
    case SDLK_BACKSPACE: ev = text_mode ? InputEvent::Erase : InputEvent::Back; break;
    case SDLK_DELETE: ev = text_mode ? InputEvent::Erase : InputEvent::None; break;
    case SDLK_TAB: ev = InputEvent::Menu; break;
    case SDLK_PAGEUP: ev = InputEvent::SeekFwdLong; break;
    case SDLK_PAGEDOWN: ev = InputEvent::SeekBackLong; break;
    // Printable keys: commands outside typing only.
    case SDLK_SPACE: ev = text_mode ? InputEvent::None : InputEvent::PlayPause; break;
    case SDLK_COMMA:
    case SDLK_LEFTBRACKET: ev = text_mode ? InputEvent::None : InputEvent::SeekBack; break;
    case SDLK_PERIOD:
    case SDLK_RIGHTBRACKET: ev = text_mode ? InputEvent::None : InputEvent::SeekFwd; break;
    default: break;
  }
  if (repeat && ev != InputEvent::Up && ev != InputEvent::Down &&
      ev != InputEvent::Left && ev != InputEvent::Right && ev != InputEvent::Erase) {
    return InputEvent::None;  // no burst of OK, Back, pause...
  }
  return ev;
}

// Gamepad (SDL names = Xbox layout: A at the bottom, B on the right, X on the left,
// Y at the top). On a PlayStation gamepad: A = cross, B = circle, X = square,
// Y = triangle, START = Options, BACK = Create/Share. playstation: the
// gamepad is a DualShock/DualSense, whose START button is "Options".
inline InputEvent map_button(uint8_t button, bool playstation) {
  switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return InputEvent::Up;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return InputEvent::Down;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return InputEvent::Left;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return InputEvent::Right;
    case SDL_CONTROLLER_BUTTON_A: return InputEvent::Ok;
    case SDL_CONTROLLER_BUTTON_B: return InputEvent::Back;
    case SDL_CONTROLLER_BUTTON_X: return InputEvent::Erase;
    case SDL_CONTROLLER_BUTTON_Y: return InputEvent::PlayPause;
    case SDL_CONTROLLER_BUTTON_START:
      return playstation ? InputEvent::Menu : InputEvent::PlayPause;
    case SDL_CONTROLLER_BUTTON_BACK: return InputEvent::Menu;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return InputEvent::SeekBack;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return InputEvent::SeekFwd;
    default: return InputEvent::None;
  }
}

inline bool is_direction(InputEvent ev) {
  return ev == InputEvent::Up || ev == InputEvent::Down || ev == InputEvent::Left ||
         ev == InputEvent::Right;
}

// Threshold with hysteresis of an analog axis (trigger or stick): a single
// event per crossing, no bounce around the threshold.
class AxisEdge {
 public:
  // value in [-32768, 32767]. Returns +1 (crossed upwards), -1
  // (crossed downwards) or 0 (no state change).
  int update(int value) {
    const int on = 16000;  // ~50 % : enclenchement
    const int off = 9000;  // ~27%: release
    int next = state_;
    if (state_ != 0 && !(state_ > 0 ? value >= off : value <= -off)) {
      next = 0;
    }
    if (next == 0) {
      next = value >= on ? 1 : (value <= -on ? -1 : 0);
    }
    const int fired = (next != state_ && next != 0) ? next : 0;
    state_ = next;
    return fired;
  }
  int state() const { return state_; }

 private:
  int state_ = 0;
};

// Repetition of a held direction (d-pad or stick): first event on
// press, then after delay_ms, then every interval_ms.
class RepeatTracker {
 public:
  RepeatTracker(uint64_t delay_ms = 400, uint64_t interval_ms = 110)
      : delay_ms_(delay_ms), interval_ms_(interval_ms) {}

  void press(InputEvent ev, uint64_t now_ms) {
    held_ = ev;
    next_ms_ = now_ms + delay_ms_;
  }
  void release(InputEvent ev) {
    if (held_ == ev) {
      held_ = InputEvent::None;
    }
  }
  // One repeated event if the deadline has passed (at most one per call: no
  // catch-up burst after a loop freeze).
  InputEvent poll(uint64_t now_ms) {
    if (held_ == InputEvent::None || now_ms < next_ms_) {
      return InputEvent::None;
    }
    next_ms_ = now_ms + interval_ms_;
    return held_;
  }
  InputEvent held() const { return held_; }

 private:
  uint64_t delay_ms_;
  uint64_t interval_ms_;
  InputEvent held_ = InputEvent::None;
  uint64_t next_ms_ = 0;
};

}  // namespace platform_sdl

#endif  // PELAGIA_PLATFORM_LINUX_INPUT_MAP_H
