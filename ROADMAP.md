# Roadmap

Pelagia is a young project; this is the plan, not a promise. Ideas and contributions are welcome
(see [CONTRIBUTING.md](CONTRIBUTING.md)).

## Released

### v0.1.0 — first public release (2026-10-09)

Jellyfin library browsing and playback with the controller, audio and subtitle selection, English and French
interface, install from a USB stick or over FTP. Tested on PS5 firmware 13.60. See
[CHANGELOG.md](CHANGELOG.md).

## Planned

### v0.1.x — maintenance

- Bug fixes and dependency updates.
- Follow the PS5 system language automatically.

### v0.2.0 — a proper title on the home screen

- Pelagia shows up with its icon on the **PS5 home screen**, without the websrv launcher.
- Fix the **black screen after quitting** when launched from the websrv launcher.

### v0.3.0 — closer to the official Jellyfin clients

- The server chooses direct play, direct streaming or transcoding from the console's capabilities.
- Clearer error messages (server unreachable, session expired during playback or browsing).
- Tested against several Jellyfin server versions.

### After that

- **Styled subtitles**: ASS/SSA subtitles with their original fonts, colors and positions.
- **HTTPS** with full certificate verification.
- **Version 2**: hardware decoding and GPU rendering, 4K and HDR.

## Ideas

- More interface languages (English and French are available).
- Play the next episode automatically; library search with the on-screen keyboard.
- Fallback fonts for non-Latin scripts; multi-user support.
