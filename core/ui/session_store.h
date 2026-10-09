// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UI_SESSION_STORE_H
#define PELAGIA_CORE_UI_SESSION_STORE_H

// Session saved between two launches: server address and token
// only, never the password. JSON file readable by the owner
// only (0600), removed by "Sign out".

#include <string>

#include "util/file_store.h"

namespace ui {

struct SavedSession {
  std::string server;
  std::string token;
};

class SessionStore {
 public:
  SessionStore(util::FileStore* store, std::string config_dir);

  // False if absent, unreadable or incomplete.
  bool load(SavedSession* out) const;
  bool save(const SavedSession& session) const;
  bool clear() const;
  std::string path() const;

  // Serialization (pure functions, tested).
  static std::string to_json(const SavedSession& session);
  static bool from_json(const std::string& json, SavedSession* out);

 private:
  util::FileStore* store_;
  std::string dir_;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_SESSION_STORE_H
