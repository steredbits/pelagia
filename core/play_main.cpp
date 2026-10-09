// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// pelagia-play - validation tool: playback of a local file.
// Options, keyboard/gamepad controls and exit codes: see print_usage()
// (pelagia-play --help).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "api/http_curl.h"
#include "api/jellyfin_client.h"
#include "api/jellyfin_reporter.h"
#include "api/jellyfin_stream.h"
#include "api/track_labels.h"
#include "api/track_selection.h"
#include "platform.h"
#include "player/media_source.h"
#include "player/av_log_bridge.h"
#include "player/network_source.h"
#include "player/playback_monitor.h"
#include "player/player.h"
#include "util/log.h"

namespace {

struct PlatformVideoSink : player::VideoSink {
  bool submit(const AVFrame* frame) override {
    return platform::video_submit_frame_yuv(
        frame->data[0], frame->data[1], frame->data[2], frame->linesize[0],
        frame->linesize[1], frame->linesize[2], frame->width, frame->height);
  }
};

struct PlatformAudioSink : player::AudioSink {
  size_t queue(const void* samples, size_t bytes) override {
    return platform::audio_queue(samples, bytes);
  }
  size_t queued_bytes() override { return platform::audio_queued_bytes(); }
  void pause(bool paused) override { platform::audio_pause(paused); }
  void flush() override { platform::audio_flush(); }
};

constexpr int kExitOk = 0;
constexpr int kExitFailure = 1;  // opening, initialization or playback
constexpr int kExitUsage = 2;    // arguments incorrects

void print_usage(std::FILE* out) {
  std::fputs(
    "Usage: pelagia-play [options] [--] <file>\n"
    "        pelagia-play [options] --item <id>\n"
    "\n"
    "Plays a local media file, or an item of a Jellyfin server (transcoded\n"
    "H.264 + AAC stream), with ffmpeg decoding and SDL2 rendering and audio.\n"
    "\n"
    "Options:\n"
    "  --item <id>   plays the Jellyfin item <id> instead of a file; server and\n"
    "                credentials via JELLYFIN_URL, JELLYFIN_USER, JELLYFIN_PASS\n"
    "  --resume      with --item: resumes at the position saved by the\n"
    "                server (otherwise, plays from the beginning)\n"
    "  --stream-auth <mode>\n"
    "                stream authentication (default: header): header = MediaBrowser\n"
    "                header only, the token is in no URL; query =\n"
    "                api_key in the URL; both = both\n"
    "  --audio <n>   with --item: audio track with Jellyfin index <n> (default: the\n"
    "                server's choice according to the user's profile)\n"
    "  --subtitle <n|none>\n"
    "                with --item: subtitles with index <n>, or none (default: the\n"
    "                server's choice according to the profile). Images (PGS) are burned in by\n"
    "                the server (transcoded video); text is not displayed by\n"
    "                this tool (see --burn-subtitles)\n"
    "  --burn-subtitles\n"
    "                with --item: makes the server burn in even text subtitles\n"
    "  --list-tracks with --item: shows the item's tracks and the default choice,\n"
    "                then exits\n"
    "  --headless    decodes the whole media without rendering or audio output, then\n"
    "                shows the statistics (frames decoded/dropped,\n"
    "                real-time factor, simulated A/V drift)\n"
    "  --verbose     detailed logs (debug level)\n"
    "  -h, --help    shows this help and exits\n"
    "  --            end of options (file whose name starts with '-')\n"
    "\n"
    "Controls during windowed playback:\n"
    "  Action                Keyboard                        Gamepad\n"
    "  Pause / resume        Space                           Start / triangle\n"
    "  Seek -10 s / +10 s    Left arrow / right arrow, , / . L1 / R1, d-pad left / right\n"
    "  Seek -60 s / +60 s    Page Down / Page Up             L2 / R2\n"
    "  Quit                  Esc, Backspace, Ctrl+Q          circle (or close the window)\n"
    "\n"
    "Without a display or sound card (CI, container):\n"
    "  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy pelagia-play <file>\n"
    "\n"
    "Exit codes: 0 success, 1 opening, playback or server connection\n"
    "            error, 2 incorrect arguments.\n",
    out);
}

struct Options {
  bool headless = false;
  bool verbose = false;
  const char* path = nullptr;
  const char* item_id = nullptr;
  bool resume = false;
  int audio_index = -1;      // --audio: -1 = server's choice
  bool subtitle_set = false;  // --subtitle given
  int subtitle_index = -1;    // -1: none
  bool burn_subtitles = false;
  bool list_tracks = false;
  // Header only: the token appears in no URL (server logs,
  // proxies). Accepted by Jellyfin 12.1 on stream.ts (tested on a real server).
  api::StreamAuthMode stream_auth = api::StreamAuthMode::Header;
};

// Returns kExitOk if the arguments are valid; otherwise kExitUsage, with the
// error message already written on stderr. Options are only recognized
// before "--".
int parse_args(int argc, char** argv, Options* opt) {
  bool options_ended = false;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (!options_ended && std::strcmp(arg, "--") == 0) {
      options_ended = true;
    } else if (!options_ended && std::strcmp(arg, "--headless") == 0) {
      opt->headless = true;
    } else if (!options_ended && std::strcmp(arg, "--verbose") == 0) {
      opt->verbose = true;
    } else if (!options_ended && std::strcmp(arg, "--resume") == 0) {
      opt->resume = true;
    } else if (!options_ended && std::strcmp(arg, "--item") == 0) {
      if (i + 1 >= argc || argv[i + 1][0] == '\0') {
        std::fprintf(stderr, "--item expects an item identifier\n");
        return kExitUsage;
      }
      opt->item_id = argv[++i];
    } else if (!options_ended && std::strcmp(arg, "--burn-subtitles") == 0) {
      opt->burn_subtitles = true;
    } else if (!options_ended && std::strcmp(arg, "--list-tracks") == 0) {
      opt->list_tracks = true;
    } else if (!options_ended && std::strcmp(arg, "--audio") == 0) {
      char* end = nullptr;
      const long v = i + 1 < argc ? std::strtol(argv[i + 1], &end, 10) : -1;
      if (i + 1 >= argc || end == argv[i + 1] || *end != '\0' || v < 0 || v > 1000) {
        std::fprintf(stderr, "--audio expects a track index (number)\n");
        return kExitUsage;
      }
      opt->audio_index = static_cast<int>(v);
      ++i;
    } else if (!options_ended && std::strcmp(arg, "--subtitle") == 0) {
      if (i + 1 < argc && std::strcmp(argv[i + 1], "none") == 0) {
        opt->subtitle_set = true;
        opt->subtitle_index = -1;
      } else {
        char* end = nullptr;
        const long v = i + 1 < argc ? std::strtol(argv[i + 1], &end, 10) : -1;
        if (i + 1 >= argc || end == argv[i + 1] || *end != '\0' || v < 0 || v > 1000) {
          std::fprintf(stderr, "--subtitle expects a track index or none\n");
          return kExitUsage;
        }
        opt->subtitle_set = true;
        opt->subtitle_index = static_cast<int>(v);
      }
      ++i;
    } else if (!options_ended && std::strcmp(arg, "--stream-auth") == 0) {
      if (i + 1 >= argc || !api::parse_stream_auth_mode(argv[i + 1], &opt->stream_auth)) {
        std::fprintf(stderr, "--stream-auth expects both, query or header\n");
        return kExitUsage;
      }
      ++i;
    } else if (!options_ended && arg[0] == '-' && arg[1] != '\0') {
      std::fprintf(stderr, "Unknown option: %s\n", arg);
      return kExitUsage;
    } else if (opt->path) {
      std::fprintf(stderr, "Only one file expected (got: '%s' and '%s')\n",
        opt->path, arg);
      return kExitUsage;
    } else {
      opt->path = arg;
    }
  }
  if (opt->path && opt->item_id) {
    std::fprintf(stderr, "A file and --item are mutually exclusive.\n");
    return kExitUsage;
  }
  if (!opt->path && !opt->item_id) {
    std::fprintf(stderr, "No file given (or --item <id>).\n");
    return kExitUsage;
  }
  if (opt->resume && !opt->item_id) {
    std::fprintf(stderr, "--resume is only used with --item.\n");
    return kExitUsage;
  }
  if (!opt->item_id && (opt->audio_index >= 0 || opt->subtitle_set || opt->burn_subtitles ||
                        opt->list_tracks)) {
    std::fprintf(stderr, "--audio, --subtitle, --burn-subtitles and --list-tracks are"
                         "only used with --item.\n");
    return kExitUsage;
  }
  return kExitOk;
}

// Help takes precedence over any other argument error.
bool wants_help(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--") == 0) {
      return false;
    }
    if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      return true;
    }
  }
  return false;
}

// Max A/V drift: "n/a" when nothing could be compared (no audio track
// or no video), never a misleading 0 ms.
void format_drift(const player::PlayerStats& stats, char* buf, size_t size) {
  if (stats.drift_measured) {
    std::snprintf(buf, size, "%lld ms", static_cast<long long>(stats.max_drift_ms));
  } else {
    std::snprintf(buf, size, "n/a");
  }
}

void print_headless_stats(const player::PlayerStats& stats) {
  const double duration_s = stats.media_duration_ms / 1000.0;
  const double wall_s = stats.wall_ms / 1000.0;
  const double video_busy_s = stats.video_decode_busy_ms / 1000.0;
  const double audio_busy_s = stats.audio_decode_busy_ms / 1000.0;
  const double total_factor = wall_s > 0.0 ? duration_s / wall_s : 0.0;
  const double video_factor =
      video_busy_s > 0.0 ? duration_s / video_busy_s : 0.0;
  const double audio_factor =
      audio_busy_s > 0.0 ? duration_s / audio_busy_s : 0.0;

  std::printf("=== Statistiques headless ===\n");
  std::printf("Media duration       : %.1f s\n", duration_s);
  std::printf("Full decoding        : %.2f s (%.1fx real time)\n", wall_s,
              total_factor);
  std::printf(
      "Video                : %llu frames decoded, %llu dropped,"
      "%.2f s of decoding (%.1fx real time)\n",
      static_cast<unsigned long long>(stats.video_frames_decoded),
      static_cast<unsigned long long>(stats.video_frames_dropped),
      video_busy_s, video_factor);
  std::printf(
      "Audio                : %llu frames decoded, %.2f s of decoding"
      "(%.1fx real time)\n",
      static_cast<unsigned long long>(stats.audio_frames_decoded),
      audio_busy_s, audio_factor);
  if (stats.drift_measured) {
    std::printf(
    "Max A/V drift        : %lld ms - SIMULATED (PTS difference in headless mode,"
    "not a playback measurement)\n",
        static_cast<long long>(stats.max_drift_ms));
  } else {
    std::printf(
    "Max A/V drift        : n/a (an audio track and a video track"
    "are needed to compare)\n");
  }
}

// Everything needed to play a Jellyfin item; lives for the duration of playback.
struct NetworkSession {
  api::HttpCurlTransport transport;  // appels API
  api::HttpCurlTransport control;    // DELETE during a seek: short timeouts
  api::HttpCurlTransport reporting;  // playback reports: short timeouts
  std::unique_ptr<api::JellyfinClient> client;
  std::unique_ptr<api::JellyfinStreamLocator> locator;
  std::unique_ptr<player::NetworkSource> source;
  std::unique_ptr<api::PlaybackReporter> reporter;
  api::MediaItem item;
  int64_t start_ms = 0;
};

int run_headless(player::MediaSource* source, int64_t start_ms) {
  player::Player p;
  player::Player::Callbacks cb;  // no sink: pure decoding
  cb.start_position_ms = start_ms;
  if (!p.open(source, cb)) {
    return kExitFailure;
  }
  player::PlayerStats stats;
  if (!p.run_headless(&stats)) {
    LOG_ERROR("Headless decoding failed");
    return kExitFailure;
  }
  print_headless_stats(stats);
  return kExitOk;
}

player::PlaybackObservation observe(const player::Player& p, const NetworkSession& net) {
  player::PlaybackObservation obs;
  obs.now_ms = platform::ticks_ms();
  obs.position_ms = p.position_ms();
  obs.paused = p.state() == player::PlayerState::Paused;
  obs.seek_in_flight = p.seek_in_flight();
  obs.session_id = net.source->session_id();
  const api::TrackSelection tracks = net.locator->tracks();
  obs.has_tracks = !tracks.media_source_id.empty() || tracks.audio_index >= 0;
  obs.media_source_id = tracks.media_source_id;
  obs.audio_index = tracks.audio_index;
  obs.subtitle_index = tracks.subtitle_index;
  return obs;
}

void submit_reports(NetworkSession* net, const std::vector<player::PlaybackReport>& reports) {
  for (const player::PlaybackReport& r : reports) {
    net->reporter->submit(r);
  }
}

// non-null net: playback of a Jellyfin item (buffer, resume, reports).
int run_windowed(player::MediaSource* source, int64_t start_ms, const char* label,
                 NetworkSession* net) {
  player::Player p;
  PlatformVideoSink video_sink;
  PlatformAudioSink audio_sink;

  player::Player::Callbacks cb;
  cb.video = &video_sink;
  cb.audio = &audio_sink;
  cb.start_position_ms = start_ms;
  if (net) {
    cb.buffering = player::BufferingPolicy::network();
    cb.deep_buffer = true;
    cb.recovery = player::RetryPolicy::network();
    cb.data_wait_timeout_ms = player::Player::Callbacks::kNetworkDataWaitMs;
  }
  if (!p.open(source, cb)) {
    return kExitFailure;
  }

  platform::Config config;
  config.window_width = 1280;
  config.window_height = 720;
  config.window_title = "Pelagia - playback";
  if (!platform::init(config)) {
    LOG_ERROR("Platform initialization failed");
    return kExitFailure;
  }
  if (p.has_audio()) {
    platform::AudioSpec spec;  // 48 kHz stereo S16 (player defaults)
    if (!platform::audio_open(spec)) {
      LOG_ERROR("Audio opening failed");
      platform::shutdown();
      return kExitFailure;
    }
  }

  p.start();
  LOG_INFO("Playing %s (%lld ms)", label,
           static_cast<long long>(p.duration_ms()));

  uint64_t last_position_log = platform::ticks_ms();
  bool running = true;
  int status = kExitOk;
  player::PlaybackMonitor monitor;
  std::vector<player::PlaybackReport> reports;
  if (net) {
    net->reporter->start();
  }
  while (running) {
    platform::Input in;
    while ((in = platform::poll_input()).event != platform::InputEvent::None) {
      switch (in.event) {
        case platform::InputEvent::Quit:
        case platform::InputEvent::Back:
          running = false;
          break;
        case platform::InputEvent::PlayPause:
          p.toggle_pause();
          break;
        case platform::InputEvent::SeekFwd:
        case platform::InputEvent::Right:
          p.seek_relative(10000);
          break;
        case platform::InputEvent::SeekBack:
        case platform::InputEvent::Left:
          p.seek_relative(-10000);
          break;
        case platform::InputEvent::SeekFwdLong:
          p.seek_relative(60000);
          break;
        case platform::InputEvent::SeekBackLong:
          p.seek_relative(-60000);
          break;
        default:
          break;
      }
    }

    const player::Player::TickResult r = p.tick();
    if (net) {
      reports.clear();
      monitor.update(observe(p, *net), &reports);
      submit_reports(net, reports);
    }
    if (r.new_video_frame) {
      platform::render_begin();
      platform::draw_video(platform::Rect{0, 0, platform::kLogicalWidth,
                                          platform::kLogicalHeight});
      platform::render_present();
    }
    if (r.ended) {
      LOG_INFO("End of playback");
      break;
    }
    if (r.failed) {
      LOG_ERROR("Playback interrupted: server unreachable");
      status = kExitFailure;
      break;
    }

    const uint64_t now = platform::ticks_ms();
    if (now - last_position_log >= 1000) {
      last_position_log = now;
      LOG_DEBUG("Position : %lld / %lld ms",
                static_cast<long long>(p.position_ms()),
                static_cast<long long>(p.duration_ms()));
    }
    platform::sleep_ms(2);
  }

  if (net) {
    reports.clear();
    monitor.stop(observe(p, *net), &reports);
    submit_reports(net, reports);
  }
  p.stop();
  if (net) {
    net->reporter->finish(3000);
  }
  const player::PlayerStats stats = p.stats();
  char drift[32];
  format_drift(stats, drift, sizeof(drift));
  LOG_INFO("Decoded video frames: %llu, dropped: %llu,"
           "max A/V drift: %s",
           static_cast<unsigned long long>(stats.video_frames_decoded),
           static_cast<unsigned long long>(stats.video_frames_dropped), drift);
  if (stats.reconnections > 0) {
    LOG_INFO("Recoveries after a network drop: %llu",
             static_cast<unsigned long long>(stats.reconnections));
  }
  if (p.has_audio()) {
    platform::audio_close();
  }
  platform::shutdown();
  return status;
}

const char* env_or_null(const char* name) {
  const char* value = std::getenv(name);
  return (value && value[0] != '\0') ? value : nullptr;
}

void log_api_error(const char* prefix, const api::ApiResult& result) {
  if (result.http_status > 0) {
    LOG_ERROR("%s : %s (HTTP %ld)", prefix, result.message.c_str(), result.http_status);
  } else {
    LOG_ERROR("%s : %s", prefix, result.message.c_str());
  }
}

void print_tracks(const api::MediaSourceInfo& source, const api::TrackSelection& chosen) {
  std::printf("Source %s (%s)\n", source.id.c_str(), source.container.c_str());
  for (const api::MediaStreamInfo& st : source.streams) {
    if (st.kind == api::StreamKind::Video) {
      std::printf("  [%d] video      %s\n", st.index, st.codec.c_str());
    } else if (st.kind == api::StreamKind::Audio) {
      std::printf("  [%d] audio      %s%s%s\n", st.index, api::audio_label(st).c_str(),
                  st.is_default ? "  (file default)" : "",
                  st.index == chosen.audio_index ? "  <- chosen" : "");
    } else {
      const bool client = api::subtitle_delivery_for(st) == api::SubtitleDelivery::Client;
      std::printf("  [%d] subtitle   %s  [%s]%s%s\n", st.index, api::subtitle_label(st).c_str(),
                  client ? "text, rendered by the client" : "burned in by the server",
                  st.is_default ? "  (file default)" : "",
                  st.index == chosen.subtitle_index ? "  <- chosen" : "");
    }
  }
}

// Playback tracks: the server's choice for the profile (or local fallback), then
// --audio / --subtitle / --burn-subtitles. Writes the summary to the logs.
int choose_tracks(const Options& opt, const NetworkSession& net, api::TrackSelection* out) {
  const api::MediaSourceInfo* source = api::find_source(net.item, "");
  if (!source) {
    LOG_WARN("The server does not return the item's tracks: choice left to the server");
    return kExitOk;
  }
  const api::UserPreferences& prefs = net.client->session().preferences;
  api::TrackSelection sel = api::default_selection(*source, prefs);
  if (opt.audio_index >= 0) {
    const api::MediaStreamInfo* a = api::find_stream(*source, opt.audio_index);
    if (!a || a->kind != api::StreamKind::Audio) {
      std::fprintf(stderr, "--audio %d: not an audio track of this item (--list-tracks)\n",
                   opt.audio_index);
      return kExitUsage;
    }
    sel = api::with_audio(*source, prefs, sel, opt.audio_index);
  }
  if (opt.subtitle_set) {
    const api::MediaStreamInfo* st = api::find_stream(*source, opt.subtitle_index);
    if (opt.subtitle_index >= 0 && (!st || st->kind != api::StreamKind::Subtitle)) {
      std::fprintf(stderr, "--subtitle %d: not a subtitle track of this item"
                           "(--list-tracks)\n", opt.subtitle_index);
      return kExitUsage;
    }
    sel = api::with_subtitle(*source, sel, opt.subtitle_index);
  }
  if (opt.burn_subtitles && sel.subtitle_index >= 0) {
    sel.subtitle_delivery = api::SubtitleDelivery::Encode;
  }
  if (opt.list_tracks) {
    print_tracks(*source, sel);
  }
  const api::MediaStreamInfo* a = api::find_stream(*source, sel.audio_index);
  const api::MediaStreamInfo* st = api::find_stream(*source, sel.subtitle_index);
  LOG_INFO("Tracks: audio %s; subtitles %s", a ? api::audio_label(*a).c_str() : "(server)",
           !st ? "none"
               : (api::subtitle_label(*st) +
                  (sel.subtitle_delivery == api::SubtitleDelivery::Encode
                       ? " (burned in by the server: transcoded video)"
                       : " (text: not displayed by this tool)")).c_str());
  *out = sel;
  return kExitOk;
}

// Authenticates, reads the item metadata and prepares the network source.
int prepare_network(const Options& opt, NetworkSession* net) {
  const char* url = env_or_null("JELLYFIN_URL");
  const char* user = env_or_null("JELLYFIN_USER");
  const char* pass = env_or_null("JELLYFIN_PASS");
  if (!url || !user || !pass) {
    std::fprintf(stderr,
                 "--item requires the JELLYFIN_URL, JELLYFIN_USER and JELLYFIN_PASS variables\n");
    return kExitUsage;
  }
  net->control.set_timeouts(3, 5);
  net->reporting.set_timeouts(3, 5);
  net->client.reset(new api::JellyfinClient(net->transport, url));
  api::ApiResult result = net->client->authenticate(user, pass);
  if (!result.ok()) {
    log_api_error("Authentication failed", result);
    return kExitFailure;
  }
  result = net->client->fetch_item(opt.item_id, &net->item);
  if (!result.ok()) {
    log_api_error("Item not found", result);
    return kExitFailure;
  }
  if (net->item.runtime_ticks <= 0) {
    LOG_ERROR("Item \"%s\" (%s) is not a playable media", net->item.name.c_str(),
              net->item.type.c_str());
    if (net->item.type == "Series" || net->item.type == "Season") {
      LOG_ERROR("For a series, use the identifier of an episode: pelagia-cli --episodes %s",
                net->item.id.c_str());
    }
    return kExitFailure;
  }

  api::TrackSelection tracks;
  const int tracks_status = choose_tracks(opt, *net, &tracks);
  if (tracks_status != kExitOk) {
    return tracks_status;
  }
  net->locator.reset(new api::JellyfinStreamLocator(*net->client, net->control,
                                                    net->item.id, opt.stream_auth, tracks));
  net->reporter.reset(new api::PlaybackReporter(*net->client, net->reporting, net->item.id,
                                                net->item.media_source_id));
  player::NetworkSourceOptions options;
  options.duration_ms = net->item.runtime_ticks / 10000;
  if (opt.resume) {
    net->start_ms = net->item.playback_position_ticks / 10000;
    if (net->start_ms > 0) {
      LOG_INFO("Resume at %lld:%02lld:%02lld (position saved by the server)",
               static_cast<long long>(net->start_ms / 3600000),
               static_cast<long long>(net->start_ms / 60000 % 60),
               static_cast<long long>(net->start_ms / 1000 % 60));
    } else {
      LOG_INFO("No resume position saved: playing from the beginning");
    }
  }
  options.start_ms = net->start_ms;
  net->source.reset(new player::NetworkSource(net->locator.get(), options));
  LOG_INFO("Item \"%s\" (%lld s), stream authentication: %s", net->item.name.c_str(),
           static_cast<long long>(options.duration_ms / 1000),
           api::stream_auth_mode_name(opt.stream_auth));
  return kExitOk;
}

}  // namespace

int main(int argc, char** argv) {
  if (wants_help(argc, argv)) {
    print_usage(stdout);
    return kExitOk;
  }
  Options opt;
  if (parse_args(argc, argv, &opt) != kExitOk) {
    std::fprintf(stderr, "Try: pelagia-play --help\n");
    return kExitUsage;
  }
  if (opt.verbose) {
    util::log_set_level(util::LogLevel::Debug);
  }
  player::install_ffmpeg_log_bridge(opt.verbose);

  if (opt.item_id) {
    NetworkSession net;
    const int status = prepare_network(opt, &net);
    if (status != kExitOk) {
      return status;
    }
    if (opt.list_tracks) {
      return kExitOk;
    }
    return opt.headless ? run_headless(net.source.get(), net.start_ms)
                        : run_windowed(net.source.get(), net.start_ms, net.item.name.c_str(),
                                       &net);
  }
  player::FileSource source(opt.path);
  return opt.headless ? run_headless(&source, 0)
                      : run_windowed(&source, 0, opt.path, nullptr);
}
