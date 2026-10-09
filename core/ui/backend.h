// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_UI_BACKEND_H
#define PELAGIA_CORE_UI_BACKEND_H

// Data seen by the UI. All methods are blocking and called
// from worker threads, never from the display loop.
// Implementations: JellyfinBackend (real server or fake test server) and
// in-memory backends in the navigation tests.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "api/jellyfin_client.h"
#include "api/jellyfin_models.h"
#include "api/track_selection.h"
#include "ui/playback_session.h"
#include "ui/poster_cache.h"
#include "ui/session_store.h"

namespace ui {

class Backend : public PosterFetcher {
 public:
  ~Backend() override {}

  // Opens a session; out receives the normalized address and the token to save.
  virtual api::ApiResult login(const std::string& server, const std::string& user,
                               const std::string& password, SavedSession* out) = 0;
  // Resumes a saved session (401: revoked or expired token).
  virtual api::ApiResult restore(const SavedSession& session) = 0;
  // Revokes the token on the server side if possible; the local session is closed
  // in all cases.
  virtual api::ApiResult logout() = 0;
  virtual std::string user_name() const = 0;
  // Playback preferences of the profile (languages, subtitles): server
  // defaults as long as no session is open.
  virtual api::UserPreferences preferences() const { return api::UserPreferences(); }

  // Managed libraries (movies, series, mixed), in server order.
  virtual api::ApiResult libraries(std::vector<api::Library>* out) = 0;
  // "Continue watching".
  virtual api::ApiResult resume(api::ItemList* out) = 0;
  // Latest additions of a library.
  virtual api::ApiResult latest(const api::Library& library, api::ItemList* out) = 0;
  // Full content of a library, sorted for display (cleaned titles).
  virtual api::ApiResult library_items(const api::Library& library, api::ItemList* out) = 0;
  virtual api::ApiResult seasons(const std::string& series_id, api::ItemList* out) = 0;
  virtual api::ApiResult episodes(const std::string& series_id, const std::string& season_id,
                                  api::ItemList* out) = 0;
  // An up-to-date item (resume position, media source).
  virtual api::ApiResult item(const std::string& id, api::MediaItem* out) = 0;

  // Text subtitles of a track (SRT); blocking, like the other calls.
  virtual api::ApiResult subtitle_text(const std::string& item_id,
                                       const std::string& media_source_id, int stream_index,
                                       std::string* srt) {
    (void)item_id;
    (void)media_source_id;
    (void)stream_index;
    (void)srt;
    api::ApiResult r;
    r.error = api::ApiError::NotAuthenticated;
    r.message = "sous-titres non pris en charge";
    return r;
  }

  // Prepares the playback of an item from start_ms: objects only,
  // no network call (the stream opening happens outside the display
  // loop, in PlaybackSession). nullptr without an open session.
  // tracks: audio / subtitle tracks chosen (empty: server's choice).
  virtual std::unique_ptr<PlaybackMedia> create_playback(const api::MediaItem& item,
                                                         int64_t start_ms,
                                                         const api::TrackSelection& tracks) = 0;
};

// Common rules (pure functions, tested) -------------------------------------

// Displayed libraries: movies, series and mixed (empty type); music,
// books, playlists, live TV... are ignored in v1.
std::vector<api::Library> supported_libraries(const std::vector<api::Library>& all);
// Item types of a library: "Movie", "Series" or both.
std::string item_types_for(const api::Library& library);
// Stable display sort by cleaned key (fixes the server sort disturbed
// by invisible characters).
void sort_for_display(std::vector<api::MediaItem>* items);

}  // namespace ui

#endif  // PELAGIA_CORE_UI_BACKEND_H
