# Changelog

All notable changes to Pelagia are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/). The release workflow publishes the
section of the tagged version as the release notes; set the date when tagging.

## [0.1.0] - 2026-10-09

First public release: an unofficial Jellyfin client for jailbroken PS5 consoles,
driven with the controller.

### Added
- Jellyfin client: sign in with user name and password (on-screen keyboard), session
  kept between launches (token only, the password is never stored), home screen with
  "Continue watching", libraries and latest additions, poster grids, movie and episode
  pages, series > seasons > episodes.
- Playback of movies and episodes in 1080p: the server transcodes to H.264 + AAC or
  streams the video directly; software decoding with ffmpeg, A/V sync, pause, seek
  (-/+10 s, -/+60 s), resume where you stopped, buffering and automatic reconnection,
  playback reports so the server keeps your position and watched state.
- Audio and subtitle selection from the movie page and during playback, preselected
  from your Jellyfin profile (preferred languages, subtitle mode, remembered choices).
  Text subtitles are drawn by Pelagia (no server transcoding); image subtitles are
  burned in by the server.
- Interface in English and French: string catalogs, language chosen from the system
  (when available) or in Options > Language, saved between launches; track language names and
  the on-screen keyboard layout (QWERTY / AZERTY) follow it.
- Installation from a USB stick (unzip the release at the root of an exFAT stick:
  `homebrew/Pelagia/eboot.elf`) or over FTP in `/data/homebrew/Pelagia/`. Session, cache
  and logs are always stored in `/data/homebrew/Pelagia/`, never on the stick.
- Optional `pelagia.conf` (`server=`) to pre-fill the server address, read from the USB
  stick first, then from `/data/homebrew/Pelagia/`.
- Version number shown in the Options menu, "About" screen with the unofficial-project
  notice and licence. The device appears as "Pelagia (PS5)" on the Jellyfin dashboard.
- GPL-3.0-or-later licence (`LICENSE`) and `THIRD_PARTY_NOTICES`.
- The `eboot.elf` in the release zip is stripped (smaller). The unstripped build is published next to
  the zip as `pelagia-debug-symbols.elf` (listed in `SHA256SUMS`), to analyze crashes.

### Known limitations
- HTTP only (no HTTPS yet, planned).
- The interface is available in English and French (Options > Language). On the PS5 the system
  language cannot be read yet, so it starts in English.
- 1080p output, software decoding (no 4K, HDR or hardware decoding yet).
- After "Quit" from the websrv launcher the screen can stay black; close the launcher by hand.
- ASS/SSA subtitle styling is not reproduced (plain text is shown); the embedded font covers
  Latin scripts only.
- Tested only with PS5 firmware 13.60 (Relapse jailbreak: etaHEN, kstuff lite, ftpsrv,
  websrv, ShadowMountPlus) and Jellyfin server 12.1.0.
