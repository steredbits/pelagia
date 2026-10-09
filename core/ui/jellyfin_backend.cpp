// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "ui/jellyfin_backend.h"

#include "ui/format.h"
#include "ui/jellyfin_playback.h"
#include "util/log.h"

namespace ui {

namespace {

constexpr int kResumeLimit = 12;
constexpr int kLatestLimit = 16;

api::ItemsQuery library_query(const api::Library& library) {
  api::ItemsQuery q;
  q.parent_id = library.id;
  q.recursive = true;
  q.include_item_types = item_types_for(library);
  // Individual movies rather than collections (BoxSets are not playable,
  // seen on a real server).
  q.collapse_box_set_items = false;
  return q;
}

}  // namespace

JellyfinBackend::JellyfinBackend(std::string device_id, api::StreamAuthMode stream_auth)
    : device_id_(std::move(device_id)), stream_auth_(stream_auth) {
  transport_.set_timeouts(5, 15);
}

std::shared_ptr<api::JellyfinClient> JellyfinBackend::client() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return client_;
}

api::ApiResult JellyfinBackend::not_connected() const {
  api::ApiResult r;
  r.error = api::ApiError::NotAuthenticated;
  r.message = "no open session";
  return r;
}

api::ApiResult JellyfinBackend::login(const std::string& server, const std::string& user,
                                      const std::string& password, SavedSession* out) {
  // New authenticated client outside the lock, published only on success.
  std::shared_ptr<api::JellyfinClient> fresh = std::make_shared<api::JellyfinClient>(
      transport_, complete_server_address(server), device_id_);
  const api::ApiResult result = fresh->authenticate(user, password);
  if (!result.ok()) {
    return result;
  }
  out->server = fresh->server_url();
  out->token = fresh->session().access_token;
  std::lock_guard<std::mutex> lock(mutex_);
  client_ = fresh;
  return result;
}

api::ApiResult JellyfinBackend::restore(const SavedSession& session) {
  std::shared_ptr<api::JellyfinClient> fresh = std::make_shared<api::JellyfinClient>(
      transport_, complete_server_address(session.server), device_id_);
  const api::ApiResult result = fresh->restore_session(session.token);
  if (result.ok()) {
    std::lock_guard<std::mutex> lock(mutex_);
    client_ = fresh;
  }
  return result;
}

api::ApiResult JellyfinBackend::logout() {
  std::shared_ptr<api::JellyfinClient> old;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    old.swap(client_);
  }
  if (!old) {
    return api::ApiResult{};
  }
  // Copy: other threads may still read the session of the old
  // client (poster downloads in progress).
  api::JellyfinClient copy = *old;
  const api::ApiResult result = copy.logout();
  if (!result.ok()) {
    LOG_WARN("Token revocation failed (%s): local session closed anyway",
             result.message.c_str());
  }
  return result;
}

std::string JellyfinBackend::user_name() const {
  std::shared_ptr<api::JellyfinClient> c = client();
  return c ? c->session().user_name : "";
}

api::ApiResult JellyfinBackend::libraries(std::vector<api::Library>* out) {
  std::shared_ptr<api::JellyfinClient> c = client();
  if (!c) {
    return not_connected();
  }
  std::vector<api::Library> all;
  const api::ApiResult result = c->fetch_libraries(&all);
  if (result.ok()) {
    *out = supported_libraries(all);
  }
  return result;
}

api::ApiResult JellyfinBackend::resume(api::ItemList* out) {
  std::shared_ptr<api::JellyfinClient> c = client();
  return c ? c->fetch_resume(kResumeLimit, out) : not_connected();
}

api::ApiResult JellyfinBackend::latest(const api::Library& library, api::ItemList* out) {
  std::shared_ptr<api::JellyfinClient> c = client();
  if (!c) {
    return not_connected();
  }
  api::ItemsQuery q = library_query(library);
  q.sort_by = "DateCreated";
  q.sort_descending = true;
  q.limit = kLatestLimit;
  return c->fetch_items(q, out);
}

api::ApiResult JellyfinBackend::library_items(const api::Library& library,
                                              api::ItemList* out) {
  std::shared_ptr<api::JellyfinClient> c = client();
  if (!c) {
    return not_connected();
  }
  const api::ApiResult result = c->fetch_items(library_query(library), out);
  if (result.ok()) {
    sort_for_display(&out->items);
  }
  return result;
}

api::ApiResult JellyfinBackend::seasons(const std::string& series_id, api::ItemList* out) {
  std::shared_ptr<api::JellyfinClient> c = client();
  return c ? c->fetch_seasons(series_id, out) : not_connected();
}

api::ApiResult JellyfinBackend::episodes(const std::string& series_id,
                                         const std::string& season_id, api::ItemList* out) {
  std::shared_ptr<api::JellyfinClient> c = client();
  return c ? c->fetch_episodes(series_id, season_id, out) : not_connected();
}

api::ApiResult JellyfinBackend::item(const std::string& id, api::MediaItem* out) {
  std::shared_ptr<api::JellyfinClient> c = client();
  return c ? c->fetch_item(id, out) : not_connected();
}

api::ApiResult JellyfinBackend::subtitle_text(const std::string& item_id,
                                              const std::string& media_source_id, int stream_index,
                                              std::string* srt) {
  std::shared_ptr<api::JellyfinClient> c = client();
  return c ? c->fetch_subtitle(item_id, media_source_id, stream_index, srt) : not_connected();
}

bool JellyfinBackend::fetch(const PosterKey& key, std::string* bytes) {
  std::shared_ptr<api::JellyfinClient> c = client();
  if (!c) {
    return false;
  }
  const api::ApiResult result = c->fetch_image(key.item_id, key.tag, key.width, bytes);
  if (!result.ok()) {
    LOG_DEBUG("Poster %s: %s (HTTP %ld)", key.item_id.c_str(), result.message.c_str(),
              result.http_status);
  }
  return result.ok();
}

api::UserPreferences JellyfinBackend::preferences() const {
  std::shared_ptr<api::JellyfinClient> c = client();
  return c ? c->session().preferences : api::UserPreferences();
}

std::unique_ptr<PlaybackMedia> JellyfinBackend::create_playback(
    const api::MediaItem& item, int64_t start_ms, const api::TrackSelection& tracks) {
  std::shared_ptr<api::JellyfinClient> c = client();
  if (!c) {
    return nullptr;
  }
  return std::unique_ptr<PlaybackMedia>(
      new JellyfinPlaybackMedia(c, item, start_ms, stream_auth_, tracks));
}

}  // namespace ui
