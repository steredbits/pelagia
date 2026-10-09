# Interface screenshots

Rendered by `pelagia-capture` (software rasterizer, 1920x1080, no display) by driving the
application with real gamepad inputs against the fake Jellyfin server
(`tools/fake_jellyfin_server.py --catalog demo`, then `--catalog tracks` for the tracks), once
per interface language: `en/` (used by `README.md`) and `fr/` (used by `README.fr.md`), same file names.
Regeneration: `tools/update_screenshots.sh`.
The same walkthrough runs in ctest (`test_ui_capture`), in both languages: a screen that is not reached
fails the test.

| File | Screen |
|---|---|
| `01-sign-in.png` | Sign-in: pre-filled address (`--server`), user name, masked password |
| `02-virtual-keyboard.png` | Virtual keyboard driven by the gamepad (QWERTY in English, AZERTY in French; typing the user name) |
| `03-home.png` | Home: "Continue watching" (episode with its series poster), libraries, latest additions |
| `04-movie-grid.png` | Movie grid, sorted ignoring U+200E and accents; titles with ‐ ’ ® « » ° € ™; movie without a poster and poster answering 404 (placeholder frames), watched movie |
| `04b-movie-grid-scrolled.png` | Grid after scrolling one row |
| `05-movie-page.png` | Detail page: "Resume at …" / "Play from the beginning", progress, overview |
| `06-series-episodes.png` | Series: seasons, episodes of season 2, playback in progress |
| `07-episode-page.png` | Episode detail page |
| `08-options.png` | Options menu: refresh, language, sign out, about, quit |
| `08b-about.png` | About: subtitle "unofficial Jellyfin client for PS5", license, trademark notice |
| `08c-language.png` | Language menu: automatic (system language), English, Français |
| `09-player-loading.png` | Player: opening the stream (dedicated thread, cancellable) |
| `10-player.png` | Playback with the banner: title, time, progress bar |
| `11-player-seeking.png` | Seek in progress ("Seeking to 0:52…") |
| `12-player-paused.png` | Pause |
| `13-page-after-playback.png` | Back on the detail page: resume position re-read after the stop report |
| `14-home-after-playback.png` | "Continue watching" reordered, the focus follows the movie |

Audio tracks and subtitles (`tracks` catalog, `--scenario tracks` of `pelagia-capture`):

| File | Screen |
|---|---|
| `15-movie-tracks.png` | Detail page of a subbed movie: tracks preselected from the profile (audio "English (original)", forced French subtitles), "Audio" and "Subtitles" rows |
| `16-audio-menu.png` | List of audio tracks, current track checked |
| `17-subtitle-menu.png` | List of subtitles: "None", text (rendered by the client), forced, ASS, image "burned in" by the server |
| `18-tracks-burned-in.png` | Real case "1917": the profile gives a PGS, burned in by the server |
| `19-player-tracks.png` | Player: the banner shows the current audio and subtitles |
| `24-player-subtitles.png` | Text subtitles rendered by the client over the video (SRT cue of the forced track, downloaded separately) |
| `20-player-options.png` | Player: Options menu (Audio, Subtitles) |
| `21-player-audio-list.png` | Player: list of audio tracks, current track checked |
| `22-player-track-change.png` | "Changing track at …": stream reopened at the current position |
| `23-page-after-track-change.png` | Back on the detail page: the server remembered the reported tracks (French audio) |

The video is the fake server's test pattern: its luminance follows time (16 + 5 × T, which overflows
after 44 s), hence a bright picture at 0:42 and a dark one at 0:52. Software rendering reproduces the
layout faithfully; at the pixel level, smoothing may differ slightly from the GPU's (SDL).
