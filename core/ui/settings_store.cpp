// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "ui/settings_store.h"

#include <cjson/cJSON.h>

#include "util/log.h"

namespace ui {

SettingsStore::SettingsStore(util::FileStore* store, std::string config_dir)
    : store_(store), dir_(std::move(config_dir)) {}

std::string SettingsStore::path() const { return dir_ + "/settings.json"; }

std::string SettingsStore::to_json(const Settings& settings) {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "language", util::language_setting_code(settings.language));
  char* text = cJSON_PrintUnformatted(root);
  std::string out = text ? text : "";
  cJSON_free(text);
  cJSON_Delete(root);
  return out;
}

bool SettingsStore::from_json(const std::string& json, Settings* out) {
  cJSON* root = cJSON_Parse(json.c_str());
  if (!root) {
    return false;
  }
  const cJSON* language = cJSON_GetObjectItemCaseSensitive(root, "language");
  if (cJSON_IsString(language)) {
    util::parse_language_setting(language->valuestring, &out->language);
  }
  cJSON_Delete(root);
  return true;
}

Settings SettingsStore::load() const {
  Settings settings;
  std::string json;
  if (store_->read(path(), &json) && !from_json(json, &settings)) {
    LOG_WARN("Unreadable settings (%s): ignored", path().c_str());
    settings = Settings();
  }
  return settings;
}

bool SettingsStore::save(const Settings& settings) const {
  if (!store_->make_dirs(dir_) || !store_->write(path(), to_json(settings), false)) {
    LOG_WARN("Cannot save the settings (%s)", path().c_str());
    return false;
  }
  return true;
}

}  // namespace ui
