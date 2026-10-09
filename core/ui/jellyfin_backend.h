// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_CORE_UI_JELLYFIN_BACKEND_H
#define PELAGIA_CORE_UI_JELLYFIN_BACKEND_H

// Backend on top of the Jellyfin client (core/api). Thread-safe: the UI calls
// (one thread) and the poster downloads (several threads) cross each
// other. The current client is shared by pointer; sign-in and
// sign-out publish a new one without modifying the one that other threads
// utilisent encore.

#include <memory>
#include <mutex>

#include "api/http_curl.h"
#include "api/jellyfin_stream.h"
#include "ui/backend.h"

namespace ui {

class JellyfinBackend final : public Backend {
 public:
  explicit JellyfinBackend(std::string device_id = "pelagia",
                           api::StreamAuthMode stream_auth = api::StreamAuthMode::Header);

  api::ApiResult login(const std::string& server, const std::string& user,
                       const std::string& password, SavedSession* out) override;
  api::ApiResult restore(const SavedSession& session) override;
  api::ApiResult logout() override;
  std::string user_name() const override;
  api::UserPreferences preferences() const override;

  api::ApiResult libraries(std::vector<api::Library>* out) override;
  api::ApiResult resume(api::ItemList* out) override;
  api::ApiResult latest(const api::Library& library, api::ItemList* out) override;
  api::ApiResult library_items(const api::Library& library, api::ItemList* out) override;
  api::ApiResult seasons(const std::string& series_id, api::ItemList* out) override;
  api::ApiResult episodes(const std::string& series_id, const std::string& season_id,
                          api::ItemList* out) override;
  api::ApiResult item(const std::string& id, api::MediaItem* out) override;
  api::ApiResult subtitle_text(const std::string& item_id, const std::string& media_source_id,
                               int stream_index, std::string* srt) override;
  bool fetch(const PosterKey& key, std::string* bytes) override;
  std::unique_ptr<PlaybackMedia> create_playback(const api::MediaItem& item, int64_t start_ms,
                                                 const api::TrackSelection& tracks) override;

 private:
  std::shared_ptr<api::JellyfinClient> client() const;
  api::ApiResult not_connected() const;

  std::string device_id_;
  api::StreamAuthMode stream_auth_;
  // One curl handle per request: usable from several threads.
  api::HttpCurlTransport transport_;
  mutable std::mutex mutex_;
  std::shared_ptr<api::JellyfinClient> client_;
};

}  // namespace ui

#endif  // PELAGIA_CORE_UI_JELLYFIN_BACKEND_H
