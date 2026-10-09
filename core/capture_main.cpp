// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// pelagia-capture - renders the main screens into PNG files, with no display or
// GPU, by driving the application with real inputs (like a gamepad)
// against a Jellyfin server (in practice the fake server, --catalog demo).
// Used to check the interface visually and review it (docs/screenshots).
//
// Each step checks the screen reached: a broken path fails (exit code 1).

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "player/av_log_bridge.h"
#include "ui/app.h"
#include "ui/image_codec.h"
#include "ui/jellyfin_backend.h"
#include "ui/platform_bridge.h"
#include "ui/screens/player_screen.h"
#include "ui/screens/screens.h"
#include "ui/soft_canvas.h"
#include "ui/text_renderer.h"
#include "util/disk_cache.h"
#include "util/log.h"
#include "util/i18n.h"

namespace {

using platform::InputEvent;

uint64_t now_ms() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

struct Options {
  std::string server;
  std::string user = "test";
  std::string password = "test";
  std::string out_dir = ".";
  std::string work_dir;
  std::string language = "en";    // interface language of the captures
  std::string scenario = "demo";  // "demo" (demonstration path) or "tracks"
};

class Driver {
 public:
  Driver(ui::App* app, ui::SoftCanvas* canvas, const std::string& out)
      : app_(app), canvas_(canvas), out_(out) {}

  // Runs the application until idle (loading and posters
  // done), at most timeout_ms.
  bool settle(int timeout_ms = 20000) {
    const uint64_t end = now_ms() + timeout_ms;
    int idle_frames = 0;
    while (now_ms() < end) {
      // Like the real loop: update then draw (posters are only
      // requested when they are drawn).
      app_->update(now_ms());
      canvas_->clear();
      app_->draw();
      idle_frames = app_->idle() ? idle_frames + 1 : 0;
      if (idle_frames >= 3) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::fprintf(stderr, "capture: the application does not settle\n");
    return false;
  }

  // Lets playback run up to the given position (ms), at most timeout_ms.
  bool run_until_position(int64_t position_ms, int timeout_ms) {
    const uint64_t end = now_ms() + timeout_ms;
    while (now_ms() < end) {
      app_->update(now_ms());
      canvas_->clear();
      app_->draw();
      if (app_->top() && std::strcmp(app_->top()->name(), "player") == 0) {
        const ui::PlayerScreen* ps = static_cast<const ui::PlayerScreen*>(app_->top());
        if (ps->session() && ps->session()->player().position_ms() >= position_ms &&
            !ps->session()->player().seek_in_flight()) {
          return true;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::fprintf(stderr, "capture : position %lld ms non atteinte\n",
                 static_cast<long long>(position_ms));
    ok_ = false;
    return false;
  }

  // Lets the application run (playback in progress) for ms.
  void run_for(int ms) {
    const uint64_t end = now_ms() + ms;
    while (now_ms() < end) {
      app_->update(now_ms());
      canvas_->clear();
      app_->draw();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }

  void press(InputEvent ev, int times = 1) {
    for (int i = 0; i < times; ++i) {
      platform::Input in;
      in.event = ev;
      app_->handle(in);
      app_->update(now_ms());
    }
  }

  void type(const std::string& text) {
    for (unsigned char c : text) {
      platform::Input in;
      in.event = InputEvent::Text;
      in.codepoint = c;  // ASCII only in the scenario
      app_->handle(in);
    }
  }

  bool expect(const char* screen) {
    if (!app_->top() || std::strcmp(app_->top()->name(), screen) != 0) {
      std::fprintf(stderr, "capture: screen \"%s\" expected, \"%s\" obtained\n", screen,
                   app_->top() ? app_->top()->name() : "(none)");
      ok_ = false;
      return false;
    }
    return true;
  }

  // Renders the current screen and writes it as a PNG.
  bool shot(const std::string& name) {
    canvas_->clear();
    app_->update(now_ms());
    app_->draw();
    ui::Image img;
    img.w = canvas_->width();
    img.h = canvas_->height();
    img.rgba = canvas_->pixels();
    std::string png;
    const std::string path = out_ + "/" + name + ".png";
    FILE* f = nullptr;
    if (ui::encode_png(img, &png) && (f = std::fopen(path.c_str(), "wb")) != nullptr) {
      const bool written = std::fwrite(png.data(), 1, png.size(), f) == png.size();
      std::fclose(f);
      if (written) {
        std::printf("%s (%zu Ko)\n", path.c_str(), png.size() / 1024);
        return true;
      }
    }
    std::fprintf(stderr, "capture: cannot write: %s\n", path.c_str());
    ok_ = false;
    return false;
  }

  bool ok() const { return ok_; }
  ui::App& app() { return *app_; }

 private:
  ui::App* app_;
  ui::SoftCanvas* canvas_;
  std::string out_;
  bool ok_ = true;
};

// Sign-in: address provided, focus on the user name. Home screen at the end.
bool log_in(Driver& d, const Options& opt, bool with_shots) {
  if (!d.settle() || !d.expect("login")) return false;
  d.press(InputEvent::Ok);  // on-screen keyboard on "User name"
  d.type(opt.user.substr(0, 2));
  d.press(InputEvent::Right, 2);
  if (with_shots) d.shot("02-virtual-keyboard");
  d.type(opt.user.substr(2));
  d.press(InputEvent::PlayPause);  // valider : champ suivant
  d.type(opt.password);
  d.press(InputEvent::Down);  // "Sign in" button
  if (with_shots) d.shot("01-sign-in");
  d.press(InputEvent::Ok);
  return d.settle() && d.expect("home");
}

// Demonstration path ("demo" catalog of the fake server).
bool run_scenario(Driver& d, const Options& opt) {
  if (!log_in(d, opt, true)) return false;
  d.shot("03-home");

  d.press(InputEvent::Menu);
  d.shot("08-options");
  d.press(InputEvent::Down);  // "Language"
  d.press(InputEvent::Ok);
  d.shot("08c-language");
  d.press(InputEvent::Back);
  d.press(InputEvent::Menu);
  d.press(InputEvent::Down, 3);  // "About"
  d.press(InputEvent::Ok);
  if (!d.expect("about")) return false;
  d.shot("08b-about");
  d.press(InputEvent::Back);

  // Movies library: "Libraries" row, first tile.
  d.press(InputEvent::Down);
  d.press(InputEvent::Ok);
  if (!d.settle() || !d.expect("grid")) return false;
  d.press(InputEvent::Right, 4);  // "Crazy Kung-Fu" (playback started)
  d.settle();
  d.shot("04-movie-grid");
  d.press(InputEvent::Down);
  d.settle();
  d.shot("04b-movie-grid-scrolled");
  d.press(InputEvent::Up);

  d.press(InputEvent::Ok);
  if (!d.settle() || !d.expect("detail")) return false;
  d.shot("05-movie-page");
  d.press(InputEvent::Back);
  d.press(InputEvent::Back);
  if (!d.settle() || !d.expect("home")) return false;

  // TV shows library -> "Test series" -> season 2 -> episodes.
  d.press(InputEvent::Right);
  d.press(InputEvent::Ok);
  if (!d.settle() || !d.expect("grid")) return false;
  d.press(InputEvent::Right);
  d.press(InputEvent::Ok);
  if (!d.settle() || !d.expect("series")) return false;
  d.press(InputEvent::Down);
  d.settle();
  d.press(InputEvent::Right);
  d.shot("06-series-episodes");
  d.press(InputEvent::Ok);
  if (!d.settle() || !d.expect("detail")) return false;
  d.shot("07-episode-page");

  // Player: "Crazy Kung-Fu" resumed from the "Continue watching" row.
  d.press(InputEvent::Back);
  d.press(InputEvent::Back);
  d.press(InputEvent::Back);
  if (!d.settle() || !d.expect("home")) return false;
  d.press(InputEvent::Up);
  d.press(InputEvent::Right);
  d.press(InputEvent::Ok);
  if (!d.settle() || !d.expect("detail")) return false;
  d.press(InputEvent::Ok);  // "Resume from ..."
  if (!d.expect("player")) return false;
  d.shot("09-player-loading");  // opening in progress on its thread
  if (!d.settle(30000) || !d.expect("player")) return false;
  d.press(InputEvent::Up);  // shows the banner
  d.run_for(1500);
  d.shot("10-player");
  d.press(InputEvent::Right);  // +10 s: stream reopened on the server side
  d.shot("11-player-seeking");
  if (!d.settle(30000)) return false;
  d.press(InputEvent::PlayPause);
  d.run_for(300);
  d.shot("12-player-paused");
  d.press(InputEvent::Back);  // stop: report sent, page reloaded
  if (!d.settle(30000) || !d.expect("detail")) return false;
  d.shot("13-page-after-playback");
  d.press(InputEvent::Back);
  if (!d.settle() || !d.expect("home")) return false;
  d.shot("14-home-after-playback");
  return d.ok();
}

// Audio and subtitle tracks ("tracks" catalog): the page of the
// multi-language movie with the profile's tracks, the lists, then the player.
const char kVostfrId[] = "de300000000000000000000000000301";
const char k1917Id[] = "de300000000000000000000000000300";

bool open_detail(Driver& d, ui::JellyfinBackend& backend, const char* id) {
  api::MediaItem item;
  if (!backend.item(id, &item).ok()) {
    std::fprintf(stderr, "capture: item %s not found\n", id);
    return false;
  }
  d.app().push(ui::make_detail_screen(item));
  return d.settle() && d.expect("detail");
}

bool run_tracks_scenario(Driver& d, ui::JellyfinBackend& backend, const Options& opt) {
  if (!log_in(d, opt, false)) return false;

  // Subtitled movie: English audio, forced French subtitles (text, rendered by the client).
  if (!open_detail(d, backend, kVostfrId)) return false;
  d.shot("15-movie-tracks");
  d.press(InputEvent::Down);
  d.press(InputEvent::Ok);
  d.shot("16-audio-menu");
  d.press(InputEvent::Back);
  d.press(InputEvent::Down);
  d.press(InputEvent::Ok);
  d.shot("17-subtitle-menu");
  d.press(InputEvent::Back);
  d.press(InputEvent::Back);  // back to the home screen

  // "1917": the profile's choice is a PGS, burned in by the server.
  if (!d.expect("home") || !open_detail(d, backend, k1917Id)) return false;
  d.shot("18-tracks-burned-in");
  d.press(InputEvent::Back);
  if (!d.settle() || !d.expect("home")) return false;

  // Player: current tracks in the banner, Options menu, audio list, then
  // track change (the stream is reopened at the current position).
  if (!open_detail(d, backend, kVostfrId)) return false;
  d.press(InputEvent::Ok);  // "Play"
  if (!d.expect("player") || !d.settle(30000) || !d.expect("player")) return false;
  d.press(InputEvent::Up);  // shows the banner
  d.run_for(1500);
  d.shot("19-player-tracks");
  // Client subtitles: 10 s jump, the line from 0:10 to 0:13.5 is displayed over the
  // video (the test media may last only 40 s: no big jump; playback
  // without a sound card advances slowly, hence the immediate pause after the jump).
  d.press(InputEvent::SeekFwd);
  if (!d.settle(30000) || !d.run_until_position(10000, 40000)) return false;
  d.press(InputEvent::PlayPause);  // pause in the line: stable image
  d.run_for(300);
  d.shot("24-player-subtitles");  // the line is moved above the banner
  d.press(InputEvent::PlayPause);
  d.press(InputEvent::Menu);
  d.shot("20-player-options");
  d.press(InputEvent::Ok);
  d.shot("21-player-audio-list");
  d.press(InputEvent::Down);
  d.press(InputEvent::Ok);  // French: stream reopened
  d.shot("22-player-track-change");
  if (!d.settle(30000)) return false;
  d.press(InputEvent::Back);  // stop: report sent, page reloaded
  if (!d.settle(30000) || !d.expect("detail")) return false;
  d.shot("23-page-after-track-change");
  return d.ok();
}

int parse(int argc, char** argv, Options* opt) {
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (i + 1 >= argc) {
      std::fprintf(stderr, "Incomplete option: %s\n", a.c_str());
      return 2;
    }
    const std::string v = argv[++i];
    if (a == "--server") opt->server = v;
    else if (a == "--user") opt->user = v;
    else if (a == "--password") opt->password = v;
    else if (a == "--out") opt->out_dir = v;
    else if (a == "--work-dir") opt->work_dir = v;
    else if (a == "--scenario") opt->scenario = v;
    else if (a == "--language") opt->language = v;
    else {
      std::fprintf(stderr, "Unknown option: %s\n", a.c_str());
      return 2;
    }
  }
  if (opt->scenario != "demo" && opt->scenario != "tracks") {
    std::fprintf(stderr, "--scenario expects demo or tracks\n");
    return 2;
  }
  if (opt->language != "en" && opt->language != "fr") {
    std::fprintf(stderr, "--language expects en or fr\n");
    return 2;
  }
  if (opt->server.empty() || opt->work_dir.empty()) {
    std::fprintf(stderr,
                 "Usage: pelagia-capture --server <url> --work-dir <dir> [--out <dir>]\n"
                 "                       [--user test] [--password test]\n"
                 "                       [--scenario demo|tracks] [--language en|fr]\n"
                 "work-dir: isolated configuration and cache (emptied by the caller).\n");
    return 2;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options opt;
  if (const int rc = parse(argc, argv, &opt)) {
    return rc;
  }
  player::install_ffmpeg_log_bridge(false);
  util::set_current_language(opt.language == "fr" ? util::Lang::Fr : util::Lang::En);
  ui::SoftCanvas canvas(platform::kLogicalWidth, platform::kLogicalHeight);
  ui::TextRenderer text(&canvas);
  if (!text.init()) {
    return 1;
  }
  ui::PlatformFileStore files;
  bool ok = false;
  {
    ui::JellyfinBackend backend("pelagia-capture");
    util::DiskCache disk(&files, opt.work_dir + "/cache/posters", 64ull * 1024 * 1024);
    disk.init();
    ui::MainQueue main_queue;
    ui::ThreadPool api_pool(2, "api");
    ui::ThreadPool image_pool(2, "posters");
    ui::PosterCache posters(&canvas, &image_pool, &main_queue, &backend, &disk,
                            ui::PosterCache::Options());
    ui::SessionStore sessions(&files, opt.work_dir + "/config");
    sessions.clear();  // always start from the sign-in screen
    ui::App::Deps deps;
    deps.backend = &backend;
    deps.api = &api_pool;
    deps.main = &main_queue;
    deps.posters = &posters;
    deps.text = &text;
    deps.canvas = &canvas;
    deps.sessions = &sessions;
    deps.system_locales = {opt.language};  // "Automatic" resolves to the language of the capture
    ui::App app(deps);
    app.start(opt.server);
    Driver driver(&app, &canvas, opt.out_dir);
    ok = opt.scenario == "tracks" ? run_tracks_scenario(driver, backend, opt)
                                  : run_scenario(driver, opt);
  }
  return ok ? 0 : 1;
}
