# Architecture

Pelagia is a native Jellyfin client for jailbroken PS5 consoles, written in C++17. The goal is a simple,
reliable player; the PS5-specific layer is kept as thin as possible and everything else is portable code
that is developed and tested on Linux.

## Layout

```
core/            portable code (C++17), no platform dependency
  api/           Jellyfin REST client (libcurl + cJSON): authentication, libraries, items, stream URLs,
                 audio/subtitle tracks (model, default choice, labels)
  player/        playback pipeline: ffmpeg demux/decode, frame queues, A/V sync, network source
  ui/            screens and navigation, driven by abstract input events; track menus, text subtitles
  util/          logs, config, image cache, track languages, SRT subtitles, version, app identity,
                 interface localization (i18n.h, catalogs in util/i18n/strings_{en,fr}.def)
platform/
  platform.h     abstract interface: window/rendering, audio, input, network, files, time
  sdl/           rendering, audio, input with SDL2, shared by Linux and PS5
  posix/         files (POSIX), shared by Linux and PS5
  linux/         Linux specifics (XDG folders); development and tests
  ps5/           PS5 specifics (folders, logs, fullscreen)
tests/           unit and integration tests (ctest); mocked API, JSON parsing, queues, UI navigation
tools/           fake Jellyfin server, release and maintenance scripts
assets/ third_party/   embedded fonts (Noto Sans, OFL); stb_truetype, cJSON
pkg/ ci/ cmake/        packaging resources, build scripts
docs/screenshots/      screenshots of the interface (generated headlessly)
```

Rule: `core/` never includes a header from `platform/sdl`, `platform/linux` or `platform/ps5`, only
`platform/platform.h`. The platform layer may use `util/log`.

## Technical choices

- C++17 without exceptions or RTTI in the core (portability to the PS5 toolchain), built with CMake for two
  targets: `-DPLATFORM=linux` and `-DPLATFORM=ps5` (cross-compilation with the open-source
  [ps5-payload-dev SDK](https://github.com/ps5-payload-dev/sdk) and PacBrew libraries; no Sony SDK or
  leaked header is ever used).
- Dependencies: libcurl, cJSON (vendored), ffmpeg (libavformat/libavcodec/libswscale/libswresample), SDL2.
- Video: software decoding. The server is asked for H.264 1080p + AAC stereo through the stream parameters,
  so only one pair of codecs needs to be handled; direct streaming is used when the server can.
- Rendering: SDL software renderer with an IYUV texture, linear scaling, letterboxing. The UI is drawn with
  a small canvas interface (SDL on Linux/PS5, a CPU rasteriser for screenshots and tests).
- Input: gamepad events are mapped to abstract events (`Up`, `Down`, `Ok`, `Back`, `PlayPause`, seek…).
- The UI never blocks (network, disk, decoding, opening a stream): work goes through worker threads and
  results are applied on the main thread.

## On the console

The app is a [websrv](https://github.com/ps5-payload-dev/websrv) homebrew: `eboot.elf` +
`sce_sys/icon0.png` in `/data/homebrew/Pelagia/` or in `/mnt/usbN/homebrew/Pelagia/`. Session, poster cache
and logs are always under `/data/homebrew/Pelagia/` (`platform/ps5/ps5_paths.h`); `pelagia.conf` is only
read, from the USB stick first, then from `/data`. Logs go to stdout and to `logs/pelagia.log`.

## Jellyfin API notes

- **Authentication**: the general API only accepts the `Authorization: MediaBrowser …, Token="…"` header.
  The stream accepts that header too, and the client uses it alone by default (`--stream-auth header`) so
  the token appears in no URL. `query` and `both` remain available as fallbacks; the client never switches
  on its own.
- **Seeking** is not possible inside the progressive stream (`Accept-Ranges: none`): the stream is reopened
  with `startTimeTicks` and a fresh `playSessionId`, after `DELETE /Videos/ActiveEncodings` for the previous
  session (otherwise the server reuses the job and ignores the new position).
- **Direct streaming**: depending on the file the server copies the video and only transcodes the audio; the
  first audio packet of the MPEG-TS can arrive after several MB of video, so the probe goes up to 32 MB or
  10 s, and the player never stays stuck (clock locked to the video after 3 s without audio, give up after
  20 s with nothing readable).
- **Tracks**: the server computes `DefaultAudioStreamIndex` / `DefaultSubtitleStreamIndex` per media source
  for the user profile and the client follows them (local fallback: `api/track_selection`). The stream URL
  carries `audioStreamIndex` and, only for burned-in subtitles, `subtitleStreamIndex` +
  `subtitleMethod=Encode`. Text subtitles are downloaded separately as SRT and drawn by the client, so the
  stream is unchanged. The server only remembers tracks from `Progress` reports.
- **Logs never contain the token**: `util/log` masks registered secrets and the `api_key=` / `Token="…"`
  patterns. Do not write to stderr/stdout around it, and let ffmpeg logs go through `player/av_log_bridge`.

## Testing

- `ctest --test-dir build-linux --output-on-failure`: unit tests plus end-to-end tests against
  `tools/fake_jellyfin_server.py` (a small Jellyfin imitation with a media test pattern).
- `./ci/build-sanitize.sh`: Debug build with libstdc++ assertions, ASan and UBSan, all tests (also a CI job).
- `tools/update_screenshots.sh`: regenerates `docs/screenshots` headlessly after a visible UI change.
- `tools/scan-secrets.sh`: gitleaks scan (CI job).
- PS5 builds can only be checked by cross-compiling (`ci/build-ps5.sh`); behaviour on the console can only
  be checked on a console, so keep PS5-specific code minimal and log what it does.
