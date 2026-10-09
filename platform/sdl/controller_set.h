// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_PLATFORM_SDL_CONTROLLER_SET_H
#define PELAGIA_PLATFORM_SDL_CONTROLLER_SET_H

// Open gamepads, indexed by SDL instance id. Pure logic
// (no SDL call): tested by tests/test_controller_set.cpp.
//
// All gamepads are opened and their events accepted: on PS5, the
// SDL driver creates one joystick per user logged in on the console (not per
// physical gamepad), and nothing guarantees that the first one is the one holding
// the gamepad (finding on the console). Each gamepad keeps its
// layout (PlayStation or not), which decides the role of START.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace platform_sdl {

template <typename Handle>
class ControllerSet {
 public:
  struct Entry {
    int32_t instance_id = -1;
    Handle handle{};
    bool playstation = false;
  };

  bool contains(int32_t instance_id) const { return find(instance_id) != nullptr; }

  // Adds an open gamepad. False if the instance is already known (SDL also sends
  // an "added" event for the gamepads present at startup).
  bool add(int32_t instance_id, Handle handle, bool playstation) {
    if (contains(instance_id)) {
      return false;
    }
    Entry e;
    e.instance_id = instance_id;
    e.handle = handle;
    e.playstation = playstation;
    entries_.push_back(e);
    return true;
  }

  // Removes the gamepad; *handle receives the handle to close. False if unknown.
  bool remove(int32_t instance_id, Handle* handle) {
    for (size_t i = 0; i < entries_.size(); ++i) {
      if (entries_[i].instance_id == instance_id) {
        *handle = entries_[i].handle;
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        return true;
      }
    }
    return false;
  }

  // Layout of the gamepad that emitted an event (false if unknown).
  bool playstation(int32_t instance_id) const {
    const Entry* e = find(instance_id);
    return e != nullptr && e->playstation;
  }

  size_t size() const { return entries_.size(); }
  const std::vector<Entry>& entries() const { return entries_; }
  void clear() { entries_.clear(); }

 private:
  const Entry* find(int32_t instance_id) const {
    for (const Entry& e : entries_) {
      if (e.instance_id == instance_id) {
        return &e;
      }
    }
    return nullptr;
  }

  std::vector<Entry> entries_;
};

}  // namespace platform_sdl

#endif  // PELAGIA_PLATFORM_SDL_CONTROLLER_SET_H
