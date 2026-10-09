// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// pelagia - Jellyfin client for the gamepad.
// Options: pelagia --help. Gamepad/keyboard controls are shown at the
// bottom of each screen.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "platform.h"
#include "player/av_log_bridge.h"
#include "ui/app.h"
#include "ui/jellyfin_backend.h"
#include "ui/platform_bridge.h"
#include "ui/poster_cache.h"
#include "ui/session_store.h"
#include "ui/tasks.h"
#include "ui/text_renderer.h"
#include "util/disk_cache.h"
#include "util/log.h"
#include "util/preconfig.h"

namespace {

constexpr int kExitOk = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;
constexpr uint64_t kDiskCacheBytes = 256ull * 1024 * 1024;

void print_usage(std::FILE* out) {
  std::fputs(
      "Usage : pelagia [options]\n"
      "\n"
      "Jellyfin client for the gamepad (1080p TV interface).\n"
      "\n"
      "Options :\n"
      "  --server <url>      server address (otherwise the saved\n"
      "                      session's); e.g. http://192.168.1.10:8096\n"
      "  --fullscreen        full screen\n"
      "  --window <W>x<H>    window size (default 1280x720)\n"
      "  --config-dir <dir>  saved session and optional pelagia.conf (server=...)\n"
      "                      (default ~/.config/pelagia)\n"
      "  --cache-dir <dir>   poster cache (default ~/.cache/pelagia)\n"
      "  --verbose           detailed logs (debug level)\n"
      "  -h, --help          shows this help\n"
      "\n"
      "Commandes :\n"
      "  Action               Clavier                       Manette\n"
      "  Navigate             Arrows                        D-pad, left stick\n"
      "  Select               Enter                         Cross (X)\n"
      "  Back                 Esc, Backspace                Circle (O)\n"
      "  Options              Tab                           Options\n"
      "  Erase (typing)       Backspace                     Square\n"
      "  Play / pause         Space                         Triangle (Start on a non-Sony gamepad)\n"
      "  Seek -10 s / +10 s   Left / right, , / .           L1 / R1 (and left / right)\n"
      "  Seek -60 s / +60 s   Page bas / Page haut          L2 / R2\n"
      "  Quitter              Ctrl+Q                        menu Options\n"
      "\n"
      "The session (server address and token, never the password) is\n"
      "saved with mode 0600 in the configuration folder; Options > Sign\n"
      "out erases it and revokes the token.\n",
      out);
}

struct Options {
  std::string server;
  std::string config_dir;
  std::string cache_dir;
  int width = 1280;
  int height = 720;
  bool fullscreen = false;
  bool verbose = false;
};

int parse_args(int argc, char** argv, Options* opt) {
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    const bool has_value = i + 1 < argc;
    if (std::strcmp(a, "--server") == 0 && has_value) {
      opt->server = argv[++i];
    } else if (std::strcmp(a, "--config-dir") == 0 && has_value) {
      opt->config_dir = argv[++i];
    } else if (std::strcmp(a, "--cache-dir") == 0 && has_value) {
      opt->cache_dir = argv[++i];
    } else if (std::strcmp(a, "--window") == 0 && has_value) {
      if (std::sscanf(argv[++i], "%dx%d", &opt->width, &opt->height) != 2 || opt->width < 320 ||
          opt->height < 180) {
        std::fprintf(stderr, "--window : format attendu LxH, ex. 1920x1080\n");
        return kExitUsage;
      }
    } else if (std::strcmp(a, "--fullscreen") == 0) {
      opt->fullscreen = true;
    } else if (std::strcmp(a, "--verbose") == 0) {
      opt->verbose = true;
    } else {
      std::fprintf(stderr, "Unknown or incomplete option: %s\n", a);
      return kExitUsage;
    }
  }
  return kExitOk;
}

// Main loop: input -> update -> draw. In menus,
// ~60 frames/s. During playback, the player pump runs every
// ~2 ms (like pelagia-play) and the screen is only redrawn on each new
// video frame, after a key press, or every 33 ms (banner animations).
// Address imposed at startup: --server, otherwise the
// pre-configuration file (pelagia.conf, server= only) if there is no
// saved session.
std::string start_server(const std::string& command_line,
                         const std::vector<std::string>& preconfig_paths,
                         const ui::SessionStore& sessions, util::FileStore* files) {
  ui::SavedSession saved;
  const bool has_saved = sessions.load(&saved);
  std::string preset;
  if (command_line.empty() && !has_saved) {
    std::string source;
    preset = util::find_preconfig_server(preconfig_paths, files, &source);
    if (!preset.empty()) {
      LOG_INFO("Server address pre-filled from %s", source.c_str());
    }
  }
  return util::choose_start_server(command_line, has_saved, preset);
}

void run_loop(ui::App& app) {
  uint64_t last_draw = 0;
  while (!app.quit_requested()) {
    const uint64_t frame_start = platform::ticks_ms();
    platform::Input in;
    while ((in = platform::poll_input()).event != platform::InputEvent::None) {
      app.handle(in);
    }
    platform::set_text_input(app.wants_text_input());
    app.update(platform::ticks_ms());
    const bool fast = app.fast_ticks();
    if (!fast || app.take_dirty() || frame_start - last_draw >= 33) {
      platform::render_begin();
      app.draw();
      platform::render_present();
      last_draw = frame_start;
    }
    const uint64_t spent = platform::ticks_ms() - frame_start;
    const uint64_t period = fast ? 2 : 16;
    if (spent < period) {
      platform::sleep_ms(static_cast<uint32_t>(period - spent));
    }
  }
}

// Logs the end of the destruction of whatever was declared just after
// it (destructors run in reverse order). On PS5, the last
// "Shutdown: ..." line of the log tells how far the shutdown went.
struct ShutdownMark {
  explicit ShutdownMark(const char* what) : what_(what) {}
  ~ShutdownMark() { LOG_INFO("Shutdown: %s", what_); }
  const char* what_;
};

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      print_usage(stdout);
      return kExitOk;
    }
  }
  Options opt;
  if (parse_args(argc, argv, &opt) != kExitOk) {
    std::fprintf(stderr, "Try: pelagia --help\n");
    return kExitUsage;
  }
  if (opt.verbose) {
    util::log_set_level(util::LogLevel::Debug);
  }
  player::install_ffmpeg_log_bridge(opt.verbose);
  LOG_INFO("Pelagia starting");
  // Launch diagnostics (PS5: where the application was launched from, USB stick or /data).
  LOG_INFO("Executable: %s", argc > 0 ? argv[0] : "(unknown)");

  platform::Config config;
  config.window_width = opt.width;
  config.window_height = opt.height;
  config.fullscreen = opt.fullscreen;
  if (!platform::init(config)) {
    LOG_ERROR("Platform initialization failed");
    return kExitFailure;
  }
  // Audio output opened once and for all (48 kHz stereo S16, player
  // format); without it, movies play without sound.
  const bool audio_ok = platform::audio_open(platform::AudioSpec());
  if (!audio_ok) {
    LOG_WARN("Audio output unavailable: playing without sound");
  }
  int status = kExitOk;
  {
    ShutdownMark done_canvas("canvas, texts and files released");
    ui::PlatformAudioSink audio_sink;
    ui::PlatformCanvas canvas;
    ui::TextRenderer text(&canvas);
    ui::PlatformFileStore files;
    const std::string config_dir = opt.config_dir.empty() ? platform::config_dir() : opt.config_dir;
    const std::string cache_dir = opt.cache_dir.empty() ? platform::cache_dir() : opt.cache_dir;
    if (!text.init()) {
      status = kExitFailure;
    } else {
      // Destruction order (reverse): screens, posters, worker threads
      // (finished before the objects they use), queue, cache, backend.
      ShutdownMark done_backend("backend and disk cache closed");
      ui::JellyfinBackend backend;
      util::DiskCache disk(&files, cache_dir + "/posters", kDiskCacheBytes);
      disk.init();
      ShutdownMark done_main_queue("result queue emptied");
      ui::MainQueue main_queue;
      ShutdownMark done_api_pool("\"api\" threads joined");
      ui::ThreadPool api_pool(2, "api");
      ShutdownMark done_image_pool("\"posters\" threads joined");
      ui::ThreadPool image_pool(2, "posters");
      ShutdownMark done_posters("poster cache released (textures destroyed)");
      ui::PosterCache posters(&canvas, &image_pool, &main_queue, &backend, &disk,
                              ui::PosterCache::Options());
      ui::SessionStore sessions(&files, config_dir);
      ui::SettingsStore settings(&files, config_dir);
      ShutdownMark done_app("interface closed (screens, playback stopped and threads joined)");
      ui::App::Deps deps;
      deps.backend = &backend;
      deps.api = &api_pool;
      deps.main = &main_queue;
      deps.posters = &posters;
      deps.text = &text;
      deps.canvas = &canvas;
      deps.sessions = &sessions;
      deps.settings = &settings;
      deps.system_locales = platform::preferred_locales();
      deps.audio = audio_ok ? &audio_sink : nullptr;
      ui::App app(deps);
      // Pre-configuration next to the session (--config-dir included).
      const std::vector<std::string> preconfig =
          opt.config_dir.empty() ? platform::preconfig_files()
                                 : std::vector<std::string>{config_dir + "/pelagia.conf"};
      app.start(start_server(opt.server, preconfig, sessions, &files));
      run_loop(app);
      LOG_INFO("Shutdown: quit requested, closing the interface");
    }
  }
  if (audio_ok) {
    platform::audio_close();
    LOG_INFO("Shutdown: audio output closed");
  }
  platform::shutdown();
  LOG_INFO("Shutdown: platform stopped");
  LOG_INFO("Pelagia finished");
  platform::finish_process(status);
  return status;
}
