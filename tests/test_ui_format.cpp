// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Tests of the display rules: durations, episodes, item poster, server
// address, saved session (never a password, private file),
// managed libraries and display sort.

#include <string>
#include <vector>

#include "memory_file_store.h"
#include "test_framework.h"
#include "ui/backend.h"
#include "ui/format.h"
#include "ui/session_store.h"

static api::MediaItem item(const std::string& name, const std::string& sort_name = "") {
  api::MediaItem m;
  m.id = name;
  m.name = name;
  m.sort_name = sort_name;
  return m;
}

static void test_clock_and_runtime() {
  CHECK(ui::format_clock(0) == "0:00");
  CHECK(ui::format_clock(-5) == "0:00");
  CHECK(ui::format_clock(123000) == "2:03");
  CHECK(ui::format_clock(3723000) == "1:02:03");
  CHECK(ui::format_clock(36000000) == "10:00:00");
  CHECK(ui::format_runtime(20000) == "< 1 min");
  CHECK(ui::format_runtime(42 * 60000) == "42 min");
  CHECK(ui::format_runtime(102 * 60000) == "1 h 42");
  CHECK(ui::format_runtime(120 * 60000) == "2 h");
  CHECK(ui::format_runtime(65 * 60000) == "1 h 05");
}

static void test_items() {
  api::MediaItem ep = item("\xE2\x80\x8E" "Pilote");
  ep.type = "Episode";
  ep.index_number = 3;
  ep.parent_index_number = 1;
  ep.runtime_ticks = 25LL * 60 * 10000000;
  ep.playback_position_ticks = ep.runtime_ticks / 4;
  CHECK(ui::episode_code(ep) == "S01E03");
  CHECK(ui::display_title(ep) == "Pilote");
  CHECK(ui::is_playable(ep));
  CHECK_EQ(ui::resume_position_ms(ep), 375000);
  CHECK(ui::progress_fraction(ep) > 0.24 && ui::progress_fraction(ep) < 0.26);
  ep.played = true;  // marked played: no resume
  CHECK_EQ(ui::resume_position_ms(ep), 0);
  CHECK(ui::progress_fraction(ep) == 0.0);
  api::MediaItem series = item("Série");
  series.type = "Series";
  CHECK(!ui::is_playable(series));
  CHECK(ui::episode_code(series).empty());

  // Poster: its own, otherwise the series', otherwise none.
  ep.primary_image_tag.clear();
  ep.series_id = "s1";
  ep.series_primary_image_tag = "stag";
  ui::PosterKey k = ui::poster_key_for(ep, 240);
  CHECK(k.item_id == "s1" && k.tag == "stag" && k.width == 240);
  ep.primary_image_tag = "own";
  k = ui::poster_key_for(ep, 240);
  CHECK(k.item_id == ep.id && k.tag == "own");
  CHECK(ui::poster_key_for(item("x"), 240).tag.empty());
}

static void test_server_address() {
  CHECK(ui::complete_server_address("192.168.1.10:8096") == "http://192.168.1.10:8096");
  CHECK(ui::complete_server_address(" https://jf.local/ ") == "https://jf.local");
  CHECK(ui::complete_server_address("http://x:8096//") == "http://x:8096");
  CHECK(ui::complete_server_address("").empty());
}

static void test_session_store() {
  MemoryFileStore fs;
  ui::SessionStore store(&fs, "/conf/pelagia");
  ui::SavedSession s;
  CHECK(!store.load(&s));
  ui::SavedSession saved;
  saved.server = "http://srv:8096";
  saved.token = "tok-secret";
  CHECK(store.save(saved));
  const MemoryFileStore::File& f = fs.files["/conf/pelagia/session.json"];
  CHECK(f.private_file);  // 0600
  CHECK(f.data.find("tok-secret") != std::string::npos);
  CHECK(f.data.find("password") == std::string::npos);
  CHECK(f.data.find("Pw") == std::string::npos);
  CHECK(store.load(&s));
  CHECK(s.server == saved.server && s.token == saved.token);
  CHECK(store.clear());
  CHECK(!store.load(&s));
  // Corrupted or incomplete file: ignored.
  fs.files["/conf/pelagia/session.json"].data = "{\"server\":\"http://x\"}";
  CHECK(!store.load(&s));
  fs.files["/conf/pelagia/session.json"].data = "pas du json";
  CHECK(!store.load(&s));
  ui::SavedSession parsed;
  CHECK(ui::SessionStore::from_json(ui::SessionStore::to_json(saved), &parsed));
  CHECK(parsed.token == "tok-secret");
}

static void test_libraries_and_sort() {
  std::vector<api::Library> all(5);
  all[0].name = "Films", all[0].collection_type = "movies";
  all[1].name = "Musique", all[1].collection_type = "music";
  all[2].name = "Séries", all[2].collection_type = "tvshows";
  all[3].name = "Mixte", all[3].collection_type = "";
  all[4].name = "Listes", all[4].collection_type = "playlists";
  const std::vector<api::Library> kept = ui::supported_libraries(all);
  CHECK_EQ(kept.size(), 3u);
  CHECK(kept[0].name == "Films" && kept[1].name == "Séries" && kept[2].name == "Mixte");
  CHECK(ui::item_types_for(kept[0]) == "Movie");
  CHECK(ui::item_types_for(kept[1]) == "Series");
  CHECK(ui::item_types_for(kept[2]) == "Movie,Series");

  // Server order disturbed by U+200E and accents: sorted again.
  std::vector<api::MediaItem> items = {
      item("Zodiac"), item("Été 85", "été 85"),
      item("\xE2\x80\x8E" "À bout de souffle", "\xE2\x80\x8E" "à bout de souffle"),
      item("Batman Begins", "batman begins"), item("Œuvre", "œuvre")};
  ui::sort_for_display(&items);
  CHECK(items[0].name == "\xE2\x80\x8E" "À bout de souffle");
  CHECK(items[1].name == "Batman Begins");
  CHECK(items[2].name == "Été 85");
  CHECK(items[3].name == "Œuvre");
  CHECK(items[4].name == "Zodiac");
}

int main() {
  test_clock_and_runtime();
  test_items();
  test_server_address();
  test_session_store();
  test_libraries_and_sort();
  return testfw::test_failures();
}
