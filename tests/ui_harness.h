// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
#ifndef PELAGIA_TESTS_UI_HARNESS_H
#define PELAGIA_TESTS_UI_HARNESS_H

// UI test harness: in-memory backend (two libraries,
// a series of two seasons, playback of a local file), tasks run
// by hand, abstract inputs as with a gamepad. No rendering or network.

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "memory_file_store.h"
#include "player/media_source.h"
#include "ui/app.h"
#include "ui/soft_canvas.h"

namespace uitest {

using platform::InputEvent;

// Playback report counters (shared with the created media).
struct ReportLog {
  std::atomic<int> started{0};
  std::atomic<int> progress{0};
  std::atomic<int> stopped{0};
  std::atomic<int64_t> last_stop_position{-1};
  // Tracks: last tracks reported / requested by the interface.
  std::atomic<int> last_audio{-2};
  std::atomic<int> last_subtitle{-2};
  std::atomic<int> set_tracks_calls{0};
};

// Source that never opens (server that does not answer) but honors
// interruption: used to test cancellation during "Loading...".
class StuckSource final : public player::MediaSource {
 public:
  bool open() override {
    for (int i = 0; i < 3000; ++i) {
      if (interrupt_ && interrupt_(ctx_)) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }
  void close() override {}
  int read(AVPacket*) override { return -1; }
  bool seek(int64_t) override { return false; }
  bool timestamps_absolute() const override { return true; }
  AVFormatContext* format_context() override { return nullptr; }
  int64_t duration_ms() const override { return 0; }
  int64_t start_time_ms() const override { return 0; }
  void set_interrupt(player::InterruptFn fn, void* ctx) override {
    interrupt_ = fn;
    ctx_ = ctx;
  }

 private:
  player::InterruptFn interrupt_ = nullptr;
  void* ctx_ = nullptr;
};

// Playback of a local file, reports counted.
class LocalMedia final : public ui::PlaybackMedia {
 public:
  LocalMedia(const std::string& path, std::shared_ptr<ReportLog> log, bool stuck,
             const api::TrackSelection& tracks = api::TrackSelection())
      : path_(path), file_(path_.c_str()), log_(std::move(log)), stuck_(stuck), tracks_(tracks) {}
  bool selected_tracks(api::TrackSelection* out) const override {
    *out = tracks_;
    return !tracks_.media_source_id.empty() || tracks_.audio_index >= 0;
  }
  void set_tracks(const api::TrackSelection& tracks) override {
    tracks_ = tracks;
    ++log_->set_tracks_calls;
  }
  player::MediaSource* source() override {
    return stuck_ ? static_cast<player::MediaSource*>(&stuck_source_) : &file_;
  }
  void report(const player::PlaybackReport& r) override {
    if (r.has_tracks) {
      log_->last_audio = r.audio_index;
      log_->last_subtitle = r.subtitle_index;
    }
    if (r.kind == player::ReportKind::Started) ++log_->started;
    if (r.kind == player::ReportKind::Progress) ++log_->progress;
    if (r.kind == player::ReportKind::Stopped) {
      ++log_->stopped;
      log_->last_stop_position = r.position_ms;
    }
  }

 private:
  std::string path_;
  player::FileSource file_;
  StuckSource stuck_source_;
  std::shared_ptr<ReportLog> log_;
  bool stuck_;
  api::TrackSelection tracks_;
};


inline api::MediaItem make_item(const std::string& id, const std::string& name, const std::string& type) {
  api::MediaItem m;
  m.id = id;
  m.name = name;
  m.type = type;
  if (type == "Movie" || type == "Episode") m.runtime_ticks = 6000LL * 10000000;
  return m;
}

inline api::ApiResult http_error(long status) {
  api::ApiResult r;
  r.error = api::ApiError::Http;
  r.http_status = status;
  return r;
}

// In-memory backend: two libraries, a series of two seasons.
class FakeBackend final : public ui::Backend {
 public:
  FakeBackend() {
    libs.resize(2);
    libs[0].id = "lib-films", libs[0].name = "Films", libs[0].collection_type = "movies";
    libs[1].id = "lib-series", libs[1].name = "Séries", libs[1].collection_type = "tvshows";
    const char* names[] = {"Zodiac", "\xE2\x80\x8E" "À bout de souffle", "Batman", "Crazy",
                           "Été 85", "F1", "Gravity", "Heat", "Inception", "Jaws", "Klaus",
                           "Lucy"};
    for (int i = 0; i < 12; ++i) {
      movies.push_back(make_item("m" + std::to_string(i), names[i], "Movie"));
    }
    movies[3].playback_position_ticks = 1200LL * 10000000;  // "Crazy" started
    resume_list.push_back(movies[3]);
    series = make_item("s1", "Série", "Series");
    for (int s = 1; s <= 2; ++s) {
      api::MediaItem season = make_item("season" + std::to_string(s), "Saison " +
                                        std::to_string(s), "Season");
      season.index_number = s;
      season_items.push_back(season);
    }
  }

  api::ApiResult login(const std::string& server, const std::string& user,
                       const std::string& password, ui::SavedSession* out) override {
    ++login_calls;
    if (user != "test" || password != "test") return http_error(401);
    out->server = server;
    out->token = "tok-new";
    connected = true;
    return api::ApiResult{};
  }
  api::ApiResult restore(const ui::SavedSession& s) override {
    if (s.token == "revoked") return http_error(401);
    if (s.token == "offline") {
      api::ApiResult r;
      r.error = api::ApiError::Transport;
      r.message = "Connection refused";
      return r;
    }
    connected = true;
    return api::ApiResult{};
  }
  api::ApiResult logout() override {
    ++logout_calls;
    connected = false;
    return api::ApiResult{};
  }
  std::string user_name() const override { return "test"; }
  api::UserPreferences preferences() const override { return prefs; }
  api::ApiResult libraries(std::vector<api::Library>* out) override {
    *out = libs;
    return api::ApiResult{};
  }
  api::ApiResult resume(api::ItemList* out) override {
    out->items = resume_list;
    return api::ApiResult{};
  }
  api::ApiResult latest(const api::Library& lib, api::ItemList* out) override {
    if (lib.id == "lib-films") out->items.assign(movies.begin(), movies.begin() + 4);
    else out->items.push_back(series);
    return api::ApiResult{};
  }
  api::ApiResult library_items(const api::Library& lib, api::ItemList* out) override {
    if (lib.id == "lib-films") {
      out->items = movies;
      ui::sort_for_display(&out->items);
    } else {
      out->items.push_back(series);
    }
    return api::ApiResult{};
  }
  api::ApiResult seasons(const std::string&, api::ItemList* out) override {
    out->items = season_items;
    return api::ApiResult{};
  }
  api::ApiResult episodes(const std::string&, const std::string& season_id,
                          api::ItemList* out) override {
    const int n = season_id == "season1" ? 2 : 3;
    for (int e = 1; e <= n; ++e) {
      api::MediaItem ep = make_item(season_id + "-e" + std::to_string(e),
                                    "Épisode " + std::to_string(e), "Episode");
      ep.index_number = e;
      ep.parent_index_number = season_id == "season1" ? 1 : 2;
      out->items.push_back(ep);
    }
    return api::ApiResult{};
  }
  api::ApiResult item(const std::string& id, api::MediaItem* out) override {
    ++item_calls;
    for (const api::MediaItem& m : movies) {
      if (m.id == id) {
        *out = m;
        out->overview = "Résumé à jour";
        return api::ApiResult{};
      }
    }
    *out = make_item(id, "Épisode", "Episode");
    return api::ApiResult{};
  }
  api::ApiResult subtitle_text(const std::string& item_id, const std::string& source_id,
                               int index, std::string* srt) override {
    ++subtitle_calls;
    last_subtitle_request = item_id + "/" + source_id + "/" + std::to_string(index);
    if (index == failing_subtitle) {
      api::ApiResult r;
      r.error = api::ApiError::Http;
      r.http_status = 500;
      r.message = "extraction impossible";
      return r;
    }
    *srt = subtitle_srt;
    return api::ApiResult{};
  }
  bool fetch(const ui::PosterKey&, std::string*) override { return false; }
  std::unique_ptr<ui::PlaybackMedia> create_playback(const api::MediaItem& item,
                                                     int64_t start_ms,
                                                     const api::TrackSelection& tracks) override {
    (void)item;
    last_play_start = start_ms;
    last_play_tracks = tracks;
    ++play_calls;
    if (!connected) return nullptr;
    return std::unique_ptr<ui::PlaybackMedia>(new LocalMedia(media_path, reports, stuck_open, tracks));
  }

  std::vector<api::Library> libs;
  std::vector<api::MediaItem> movies;
  std::vector<api::MediaItem> resume_list;
  api::MediaItem series;
  std::vector<api::MediaItem> season_items;
  bool connected = false;
  int login_calls = 0;
  int logout_calls = 0;
  int item_calls = 0;
  // Lecture.
  std::string media_path = "/nonexistent.mp4";
  bool stuck_open = false;
  int play_calls = 0;
  int64_t last_play_start = -1;
  // Subtitles: SRT served for any track, track whose download fails.
  std::string subtitle_srt = "1\n00:00:01,000 --> 00:00:03,000\nBonjour\n\n"
                             "2\n00:00:03,500 --> 00:00:05,000\nDeux\nlignes\n";
  int failing_subtitle = -1;
  std::atomic<int> subtitle_calls{0};
  std::string last_subtitle_request;
  api::TrackSelection last_play_tracks;
  api::UserPreferences prefs;
  std::shared_ptr<ReportLog> reports = std::make_shared<ReportLog>();
};

struct Harness {
  FakeBackend backend;
  MemoryFileStore files;
  ui::ManualRunner api;
  ui::ManualRunner images;
  ui::MainQueue main;
  ui::SoftCanvas canvas{16, 16};
  ui::TextRenderer text{&canvas};
  ui::PosterCache posters{&canvas, &images, &main, &backend, nullptr, ui::PosterCache::Options()};
  ui::SessionStore sessions{&files, "/conf"};
  ui::SettingsStore settings{&files, "/conf"};
  ui::App* app = nullptr;
  uint64_t now = 1000;

  // with_settings: the App reads the saved settings (language) and applies them,
  // with the given system locales; otherwise it leaves the current language alone.
  explicit Harness(bool with_settings = false,
                   std::vector<std::string> system_locales = std::vector<std::string>()) {
    ui::App::Deps d;
    if (with_settings) {
      d.settings = &settings;
      d.system_locales = std::move(system_locales);
    }
    d.backend = &backend;
    d.api = &api;
    d.main = &main;
    d.posters = &posters;
    d.text = &text;
    d.canvas = &canvas;
    d.sessions = &sessions;
    app = new ui::App(d);
  }
  ~Harness() { delete app; }

  // Runs the tasks and applies the results until idle.
  void settle() {
    for (int i = 0; i < 20; ++i) {
      api.run_all();
      app->update(now += 16);
      if (api.pending() == 0 && main.size() == 0) break;
    }
  }
  void press(InputEvent ev, int times = 1) {
    for (int i = 0; i < times; ++i) {
      platform::Input in;
      in.event = ev;
      app->handle(in);
    }
  }
  void type(const std::string& s) {
    for (unsigned char c : s) {
      platform::Input in;
      in.event = InputEvent::Text;
      in.codepoint = c;
      app->handle(in);
    }
  }
  std::string screen() const { return app->top() ? app->top()->name() : ""; }
  template <typename T>
  T* top() const { return static_cast<T*>(app->top()); }
};

inline void save_session(Harness& h, const std::string& token) {
  ui::SavedSession s;
  s.server = "http://srv:8096";
  s.token = token;
  h.sessions.save(s);
}

}  // namespace uitest

#endif  // PELAGIA_TESTS_UI_HARNESS_H
