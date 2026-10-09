// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UI_SETTINGS_STORE_H
#define PELAGIA_CORE_UI_SETTINGS_STORE_H

// User settings kept between two launches (Options menu): for now the interface
// language. JSON file next to the session, kept when signing out.

#include <string>

#include "util/file_store.h"
#include "util/i18n.h"

namespace ui {

struct Settings {
  util::LanguageSetting language = util::LanguageSetting::Auto;
};

class SettingsStore {
 public:
  SettingsStore(util::FileStore* store, std::string config_dir);

  // Defaults if the file is absent or unreadable.
  Settings load() const;
  bool save(const Settings& settings) const;
  std::string path() const;

  // Serialization (pure functions, tested). Unknown or invalid values keep the defaults.
  static std::string to_json(const Settings& settings);
  static bool from_json(const std::string& json, Settings* out);

 private:
  util::FileStore* store_;
  std::string dir_;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_SETTINGS_STORE_H
