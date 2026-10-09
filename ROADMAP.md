# Roadmap

Pelagia is a young project; this is the plan, not a promise. Ideas and contributions are welcome
(see [CONTRIBUTING.md](CONTRIBUTING.md)).

## v0.1 — first public release

Jellyfin library browsing and playback with the controller, audio and subtitle selection, install from a USB
stick or over FTP. See [CHANGELOG.md](CHANGELOG.md).

## v0.2 — a proper title on the home screen

- Install so that Pelagia shows up as a **title on the PS5 home screen** (through ShadowMountPlus), without
  the websrv launcher.
- Fix the **black screen after quitting** when launched from the websrv launcher.

## v0.3 and after

- **HTTPS** with full certificate verification (CA bundle shipped with the app).
- Behave more like the official Jellyfin clients: let the server choose direct play, direct streaming or
  transcoding from the console's capabilities (`PlaybackInfo` + device profile).
- Clearer error messages (server unreachable or token expired during playback or browsing).
- Integration tests against real Jellyfin servers (Docker) in CI, several versions.

## Later / ideas

- More interface languages (English and French are available), and reading the PS5 system language.
- 4K, HEVC, HDR and hardware decoding, direct play without transcoding.
- Play the next episode automatically; library search with the on-screen keyboard.
- Fallback fonts for non-Latin scripts; styled ASS/SSA subtitles; multi-user support.
