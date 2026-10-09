# Development tools

## `fake_jellyfin_server.py` — fake Jellyfin server

Minimal server (standard Python + ffmpeg) that imitates the behavior of Jellyfin 12.1 as observed on a
real server: authentication through the MediaBrowser header only, progressive MPEG-TS stream without
`Accept-Ranges`, `startTimeTicks`, reuse of the transcoding job for identical `deviceId` +
`playSessionId`, `DELETE /Videos/ActiveEncodings` (400 without `playSessionId`), reporting through
`/Sessions/Playing[/Progress|/Stopped]`. It is used by the automated tests (ctest) and for manual trials.

```bash
# Test file: the luminance of the picture is 16 + 5 x T (the position can be read from the content)
python3 tools/fake_jellyfin_server.py --make-media /tmp/pattern.mp4 --seconds 600

# Realistic server: ~10 s before the first frames, transcoding at 1.5x real time
python3 tools/fake_jellyfin_server.py --media /tmp/pattern.mp4 --port 8096 \
    --startup-delay 10 --readrate 1.5 --verbose
# Credentials: test / test  (--user, --password)
```

User interface catalog: `--catalog demo` serves two libraries (Movies, Series), about fifteen movies
and two playable series (all of them stream the test pattern), PNG posters generated on the fly
(`/Items/{id}/Images/Primary`, one movie without a poster, one whose poster answers 404), Unicode
titles taken from a real library (U+2010 hyphen, U+2019 apostrophe, "F1®", a title prefixed with
U+200E, accented capitals, ligatures) and two started playbacks ("Continue watching").
The default catalog (`minimal`) is the one used by the network playback tests.
`--language en|fr` (default `en`) sets the language of the demo content (titles, library names,
subtitle cues, track titles).

```bash
python3 tools/fake_jellyfin_server.py --media /tmp/pattern.mp4 --port 8096 --catalog demo
./build-linux/pelagia --server http://127.0.0.1:8096   # credentials test / test
```

Audio tracks and subtitles: `--catalog tracks` = `demo` + four movies that carry `MediaStreams`:
"1917" (real case observed: HEVC video, English TrueHD, French E-AC3 by default, two French PGS of
which one is the default, none forced), a subbed movie (3 audio tracks, one of them a mono 44.1 kHz AAC
that the server "copies", SRT / ASS / PGS subtitles of which one is forced), a dubbed movie and a
movie with external subtitles. Each audio track is a sine wave with its own frequency (the decoded
content tells which track was served), `subtitleStreamIndex` + `subtitleMethod=Encode` burns a red
square into the top left corner, `Subtitles/{n}/Stream.srt` serves a 3.5 s cue every 5 s (the ASS is
"converted" with tags), PGS tracks answer 404. The profile (`User.Configuration`) is set with
`--audio-language`, `--subtitle-language`, `--subtitle-mode`, `--no-play-default-audio`,
`--no-remember`, or live (`/__test/config`: `subtitle_mode`, `omit_defaults`, `remember_selections`,
`subtitle_delay`...); the tracks reported by `Progress` are remembered and returned as the default
tracks.

Driving the server during a trial (another terminal):

```bash
curl -s localhost:8096/__test/state | python3 -m json.tool          # requests, jobs, reports
curl -s -X POST localhost:8096/__test/fault -d '{"drop": true}'      # cuts the open streams (RST)
curl -s -X POST localhost:8096/__test/fault -d '{"stall_s": 5}'      # freezes sending for 5 s
curl -s -X POST localhost:8096/__test/fault -d '{"drop": true, "refuse_s": 8}'  # "Wi-Fi down" for 8 s
curl -s -X POST localhost:8096/__test/config -d '{"stream_auth": "header"}'     # refuses api_key
```

Useful options: `--pts-mode absolute` (absolute PTS after `startTimeTicks` instead of restarting
from ~0), `--stream-auth query|header|both`, `--min-resume-duration` (300 s by default, like Jellyfin).
