// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// User settings file (interface language).

#include <string>

#include "memory_file_store.h"
#include "test_framework.h"
#include "ui/settings_store.h"

int main() {
  using util::LanguageSetting;

  // Serialization.
  ui::Settings settings;
  CHECK(settings.language == LanguageSetting::Auto);
  CHECK_EQ(ui::SettingsStore::to_json(settings), "{\"language\":\"auto\"}");
  settings.language = LanguageSetting::French;
  CHECK_EQ(ui::SettingsStore::to_json(settings), "{\"language\":\"fr\"}");

  ui::Settings parsed;
  CHECK(ui::SettingsStore::from_json("{\"language\":\"en\"}", &parsed));
  CHECK(parsed.language == LanguageSetting::English);
  // Unknown value or missing key: defaults kept; invalid JSON: refused.
  ui::Settings other;
  CHECK(ui::SettingsStore::from_json("{\"language\":\"klingon\"}", &other));
  CHECK(other.language == LanguageSetting::Auto);
  CHECK(ui::SettingsStore::from_json("{}", &other));
  CHECK(!ui::SettingsStore::from_json("not json", &other));

  // File: absent -> defaults; saved -> read back; corrupt -> defaults.
  MemoryFileStore files;
  ui::SettingsStore store(&files, "/cfg");
  CHECK_EQ(store.path(), "/cfg/settings.json");
  CHECK(store.load().language == LanguageSetting::Auto);
  settings.language = LanguageSetting::English;
  CHECK(store.save(settings));
  CHECK(store.load().language == LanguageSetting::English);
  CHECK(files.write("/cfg/settings.json", "garbage", false));
  CHECK(store.load().language == LanguageSetting::Auto);
  return testfw::test_failures();
}
