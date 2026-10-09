// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
// UI Jellyfin backend against the fake server ("demo" catalog):
// sign-in, libraries, resume, sort, posters, sign-out with
// token revocation, session resume.

#include <cstdio>
#include <string>

#include "fake_server.h"
#include "test_framework.h"
#include "ui/image_codec.h"
#include "ui/jellyfin_backend.h"
#include "util/text.h"

namespace {

const char kCrazy[] = "de300000000000000000000000000101";
const char kNoPoster[] = "de300000000000000000000000000109";
const char kBrokenPoster[] = "de30000000000000000000000000010a";

bool has_name(const api::ItemList& list, const std::string& name) {
  for (const api::MediaItem& m : list.items) {
    if (m.name == name) return true;
  }
  return false;
}

}  // namespace

int main() {
  std::string media;
  if (!testnet::ensure_test_media(&media)) {
    std::fprintf(stderr, "python3/ffmpeg indisponibles : test ignoré\n");
    return testnet::kSkip;
  }
  testnet::FakeServer server;
  if (!server.start(media, {"--catalog", "demo"})) {
    std::fprintf(stderr, "faux serveur non démarré\n");
    return 1;
  }

  ui::JellyfinBackend backend("ui-test");
  ui::SavedSession saved;
  // Wrong password: 401, no session.
  api::ApiResult r = backend.login(server.base_url(), "test", "faux", &saved);
  CHECK(!r.ok() && r.http_status == 401);
  std::vector<api::Library> libs;
  CHECK(backend.libraries(&libs).error == api::ApiError::NotAuthenticated);

  // Address typed without a scheme, with a trailing "/".
  const std::string typed = server.base_url().substr(7) + "/";
  CHECK(backend.login(typed, "test", "test", &saved).ok());
  CHECK(saved.server == server.base_url());
  CHECK(!saved.token.empty());
  CHECK(backend.user_name() == "test");

  CHECK(backend.libraries(&libs).ok());
  CHECK_EQ(libs.size(), 2u);
  CHECK(libs[0].collection_type == "movies" && libs[1].collection_type == "tvshows");

  api::ItemList movies;
  CHECK(backend.library_items(libs[0], &movies).ok());
  CHECK_EQ(movies.items.size(), 14u);
  // Client sort: the title prefixed with U+200E comes first, "Ete 85" and
  // "Oeuvre" at their alphabetical place (the server puts them at the end of the list).
  CHECK(util::clean_display_text(movies.items[0].name) == "À bout de souffle");
  CHECK(movies.items.back().name == "Zodiac");
  CHECK(has_name(movies, "Crazy Kung\xE2\x80\x90" "Fu"));
  CHECK(has_name(movies, "L\xE2\x80\x99" "éveil"));
  CHECK(has_name(movies, "F1\xC2\xAE"));

  api::ItemList latest;
  CHECK(backend.latest(libs[0], &latest).ok());
  CHECK(!latest.items.empty() && latest.items.size() <= 16u);

  api::ItemList resume;
  CHECK(backend.resume(&resume).ok());
  CHECK_EQ(resume.items.size(), 2u);
  CHECK(resume.items[0].type == "Episode");  // most recent first
  CHECK(!resume.items[0].series_primary_image_tag.empty());
  CHECK(resume.items[1].id == kCrazy);
  CHECK(resume.items[1].playback_position_ticks > 0);

  api::ItemList shows, seasons, episodes;
  CHECK(backend.library_items(libs[1], &shows).ok());
  CHECK_EQ(shows.items.size(), 2u);
  const std::string series_id = "f00dfeed0000000000000000000000cc";
  CHECK(backend.seasons(series_id, &seasons).ok());
  CHECK_EQ(seasons.items.size(), 2u);
  CHECK(backend.episodes(series_id, seasons.items[1].id, &episodes).ok());
  CHECK_EQ(episodes.items.size(), 3u);
  CHECK(episodes.items[0].parent_index_number == 2 && episodes.items[0].index_number == 1);
  CHECK(episodes.items[0].runtime_ticks > 0);  // playable in demo mode

  api::MediaItem detail;
  CHECK(backend.item(kCrazy, &detail).ok());
  CHECK(!detail.media_source_id.empty());
  CHECK(!detail.overview.empty());

  // Posters: PNG decodable at the requested width; missing or broken -> failure.
  ui::PosterKey key;
  key.item_id = kCrazy;
  key.tag = detail.primary_image_tag;
  key.width = 240;
  std::string bytes;
  CHECK(backend.fetch(key, &bytes));
  ui::Image img;
  CHECK(ui::decode_image(bytes, 240, 0, &img));
  CHECK(img.w == 240 && img.h == 360);
  key.item_id = kBrokenPoster;
  CHECK(!backend.fetch(key, &bytes));
  key.item_id = kNoPoster;
  CHECK(!backend.fetch(key, &bytes));

  // Sign-out: token revoked on the server side, the saved session no longer
  // works (401); a new sign-in is needed.
  CHECK(backend.logout().ok());
  CHECK(backend.libraries(&libs).error == api::ApiError::NotAuthenticated);
  ui::JellyfinBackend other("ui-test");
  r = other.restore(saved);
  CHECK(!r.ok() && r.http_status == 401);
  ui::SavedSession again;
  CHECK(other.login(server.base_url(), "test", "test", &again).ok());
  ui::JellyfinBackend third("ui-test");
  CHECK(third.restore(again).ok());
  CHECK(third.user_name() == "test");

  // Server stopped: transport error (not a 401: the session is kept).
  server.stop();
  ui::JellyfinBackend offline("ui-test");
  r = offline.restore(again);
  CHECK(!r.ok() && r.error == api::ApiError::Transport);
  return testfw::test_failures();
}
