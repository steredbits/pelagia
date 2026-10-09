// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#include "ui/session_store.h"

#include <cjson/cJSON.h>

#include "util/log.h"

namespace ui {

SessionStore::SessionStore(util::FileStore* store, std::string config_dir)
    : store_(store), dir_(std::move(config_dir)) {}

std::string SessionStore::path() const { return dir_ + "/session.json"; }

std::string SessionStore::to_json(const SavedSession& session) {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "server", session.server.c_str());
  cJSON_AddStringToObject(root, "token", session.token.c_str());
  char* text = cJSON_PrintUnformatted(root);
  std::string out = text ? text : "";
  cJSON_free(text);
  cJSON_Delete(root);
  return out;
}

bool SessionStore::from_json(const std::string& json, SavedSession* out) {
  cJSON* root = cJSON_Parse(json.c_str());
  if (!root) {
    return false;
  }
  const cJSON* server = cJSON_GetObjectItemCaseSensitive(root, "server");
  const cJSON* token = cJSON_GetObjectItemCaseSensitive(root, "token");
  const bool ok = cJSON_IsString(server) && cJSON_IsString(token) &&
                  server->valuestring[0] != '\0' && token->valuestring[0] != '\0';
  if (ok) {
    out->server = server->valuestring;
    out->token = token->valuestring;
  }
  cJSON_Delete(root);
  return ok;
}

bool SessionStore::load(SavedSession* out) const {
  std::string json;
  if (!store_->read(path(), &json)) {
    return false;
  }
  if (!from_json(json, out)) {
    LOG_WARN("Unreadable saved session (%s): ignored", path().c_str());
    return false;
  }
  util::log_add_secret(out->token);
  return true;
}

bool SessionStore::save(const SavedSession& session) const {
  if (!store_->make_dirs(dir_) || !store_->write(path(), to_json(session), true)) {
    LOG_WARN("Cannot save the session (%s)", path().c_str());
    return false;
  }
  return true;
}

bool SessionStore::clear() const { return store_->remove(path()); }

}  // namespace ui
