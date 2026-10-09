#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 steredbits and Pelagia contributors
"""Minimal fake Jellyfin server to test Pelagia without a real server.

Simulates the subset of the API (behavior observed on Jellyfin 12.1.0)
that the client needs:

- POST /Users/AuthenticateByName: random token for each run.
- General API: only the `Authorization: MediaBrowser ..., Token="..."` header
  is accepted; `api_key=` and `X-Emby-Token` answer 401 (12.1 finding).
- GET /Users/{id}/Views, /Users/{id}/Items (filtered by includeItemTypes:
  one movie, one series), /Users/{id}/Items/{itemId} (RunTimeTicks,
  UserData.PlaybackPositionTicks, MediaSources), /Shows/{id}/Seasons and
  /Shows/{id}/Episodes (two episodes listed, not playable).
- GET /Videos/{id}/stream.ts: progressive MPEG-TS transcoded by ffmpeg from
  --media, `Accept-Ranges: none`, `Transfer-Encoding: chunked` (unknown
  length, like Kestrel/ASP.NET Core: the normal end is explicit, a drop
  is not), Range ignored, `startTimeTicks` honored. Stream authentication
  through `api_key` and/or the MediaBrowser header (--stream-auth).
- Transcoding jobs: one job per (deviceId, itemId, playSessionId). A
  request that falls back on an existing job reuses it and IGNORES the new
  startTimeTicks (the stream restarts from the beginning of the job's file,
  like the real server). A new playSessionId creates a new job, but the old
  one keeps running until it is deleted.
- DELETE /Videos/ActiveEncodings: 400 without playSessionId, otherwise stops the job.
- POST /Sessions/Playing, /Sessions/Playing/Progress, /Sessions/Playing/Stopped:
  recorded; Progress updates the position, Stopped applies the resume
  thresholds and stops the PlaySessionId's job (model to verify against a
  real server).

Audio and subtitle tracks ("tracks" catalog = demo + these movies): four movies carry
MediaStreams (see TRACK_PROFILES), including a real case ("1917": HEVC video,
English TrueHD, French E-AC3 by default, two French PGS one of which is default, none
forced), a subbed movie (3 audio tracks including a "copyable" one in AAC mono 44.1 kHz,
SRT/ASS/PGS subtitles one of which is forced), a dubbed movie and a movie with external
subtitles. Each audio track is a sine wave of its own frequency (the decoded content says
which track was served) and `subtitleStreamIndex` + `subtitleMethod=Encode` burns in a red
square (high V chroma) in the top-left corner of the video. The server returns, like the
real one, the profile preferences (`User.Configuration`) and DefaultAudio/SubtitleStreamIndex
per source; the choices reported by `Progress` (AudioStreamIndex / SubtitleStreamIndex) are
remembered and then returned as default tracks (RememberAudioSelections). Text subtitles
are served as SRT by `/Videos/{id}/{source}/Subtitles/{n}/Stream.srt` (ASS is converted,
`{\\an8}` and `<i>` tags included, like ffmpeg); image subtitles answer 404.

Simulation of network conditions:
- --startup-delay: latency before the first byte of each new job
  (~10 s for a real 4K transcode; ~1 s in automated tests).
- --readrate: transcoding rate as a multiple of real time (0 = maximal).
- --video-kbps / --late-audio: imitate Jellyfin's "direct streaming"
  (video at its original bitrate, transcoded audio): imposed video bitrate
  (padding bytes included) and first audio packet arriving after N s of video,
  hence well beyond a 1 MB probe (ffmpeg: "Could not find codec parameters
  for stream 1 ... 0 channels"). Changeable on the fly: POST /__test/config
  {"video_kbps": 12000, "late_audio": 3.0}; the audio is then also shifted by
  N s in PTS.
- POST /__test/fault {"drop": true, "stall_s": 3, "refuse_s": 5}: cuts the
  open streams (RST), freezes sending without closing, refuses every request;
  {"truncate": true} cleanly ends the open streams before the end.

Catalog (--catalog): "minimal" (default, network playback tests: one playable movie,
a series of two non-playable episodes, no posters) or "demo" (interface:
Movies and TV shows libraries, about fifteen movies and two playable series,
PNG posters generated on the fly, Unicode titles of a real library - U+2010, U+2019,
(R), a title prefixed with U+200E -, resume positions). Both catalogs also serve
/Users/Me, /Sessions/Logout (revokes the token), /Users/{id}/Items/Resume and
/Items/{id}/Images/Primary.

Test control: GET /__test/state (requests, jobs, reports, saved
position), POST /__test/config, /__test/user_data, /__test/reset.

Usage:
  python3 tools/fake_jellyfin_server.py --make-media pattern.mp4 --seconds 600
  python3 tools/fake_jellyfin_server.py --media pattern.mp4 --port 8096 \\
      --startup-delay 10 --readrate 1.5 --verbose
The server prints "READY port=N" on stdout when it is listening.
"""

import argparse
import json
import os
import re
import secrets
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import zlib
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

TICKS_PER_SECOND = 10_000_000
# Language of the demo content (titles, overviews, subtitle texts): English by default,
# French with --language fr (used for the French screenshots).
LANGUAGE = "en"
FR = {
    "Test pattern": "Mire de test",
    "Movies": "Films",
    "TV shows": "Séries",
    "Test series": "Série de test",
    "Season {n}": "Saison {n}",
    "Episode {n}": "Épisode {n}",
    "Missing poster": "Affiche introuvable",
    "Poster-less work": "Œuvre sans affiche",
    "An extremely long title to check truncation when displayed":
        "Un titre extrêmement long pour vérifier la troncature à l’affichage",
    "Symbols: \u201cquotes\u201d, 20 \u00b0C, 5 \u20ac, Brand\u2122...":
        "Symboles : « guillemets », 20 °C, 5 €, Marque™…",
    "Chronicles of the Awakening": "Chroniques de l’éveil",
    "A demonstration movie served by the fake Jellyfin server: the stream is the test "
    "pattern, whose luminance follows time. This summary is deliberately long to check "
    "line wrapping and truncation of the detail page, with accents, typographic "
    "apostrophes and \u201cquotes\u201d.":
        "Un film de démonstration servi par le faux serveur Jellyfin : le flux est la mire "
        "de test, dont la luminance suit le temps. Ce résumé est volontairement long pour "
        "vérifier le retour à la ligne et la troncature de la fiche détail, avec des "
        "accents, des apostrophes typographiques l’air de rien et des guillemets « français ».",
    "\u201c{name}\u201d: demonstration series of the fake server.":
        "« {name} » : série de démonstration du faux serveur.",
    "Episode {e}: trial no. {e}": "Épisode {e} : l’épreuve n°{e}",
    "{s_name}, episode {e} of \u201c{name}\u201d.": "{s_name}, épisode {e} de « {name} ».",
    "cue {k}": "réplique {k}",
    "In italics": "En italique",
    "Two lines{{\\i1}} <b>bold</b>\n[{lang}#{index}] continued":
        "Deux lignes{{\\i1}} <b>gras</b>\n[{lang}#{index}] suite",
    "Commentary": "Commentaire",
    "Full (ASS)": "Complet (ASS)",
    "Subbed movie": "Film VOSTFR",
    "Dubbed movie": "Film VF",
    "Movie with external subtitles": "Film aux sous-titres externes",
}


def L(text, **kw):
    """Text in the demo language (English source strings, French in FR)."""
    if LANGUAGE == "fr":
        text = FR.get(text, text)
    return text.format(**kw) if kw else text


ITEM_ID = "f00dfeed0000000000000000000000aa"
USER_ID = "0123456789abcdef0123456789abcdef"
LIBRARY_ID = "f00dfeed0000000000000000000000bb"
SERIES_ID = "f00dfeed0000000000000000000000cc"
SEASON_ID = "f00dfeed0000000000000000000000dd"
EPISODE_IDS = ["f00dfeed0000000000000000000000e1", "f00dfeed0000000000000000000000e2"]
SERVER_ID = "fakejellyfin"
TOKEN_RE = re.compile(r'Token="([^"]*)"')
API_KEY_RE = re.compile(r"(api_key=)[^&\s]*", re.IGNORECASE)



# ---------------------------------------------------------------------------
# Catalogue
# ---------------------------------------------------------------------------

SERIES_LIBRARY_ID = "f00dfeed0000000000000000000000b2"


def demo_id(n):
    return f"de300000000000000000000000{n:06x}"


# --- Pistes -------------------------------------------------------

TRACKS_1917_ID = demo_id(0x300)
TRACKS_VOSTFR_ID = demo_id(0x301)
TRACKS_VF_ID = demo_id(0x302)
TRACKS_EXTERNAL_ID = demo_id(0x303)


def _video(index, codec):
    return {"Index": index, "Type": "Video", "Codec": codec, "IsDefault": True}


def _audio(index, codec, lang, channels, layout, freq, default=False, title="",
           copy=None, original=False):
    """Audio track. freq: frequency of the served sine wave. copy: (rate, channels)
    if the server can copy the track as is (AAC), otherwise transcoded to
    48 kHz stereo as the stream URL requests."""
    st = {"Index": index, "Type": "Audio", "Codec": codec, "Language": lang,
          "Channels": channels, "ChannelLayout": layout, "IsDefault": default,
          "IsForced": False, "IsExternal": False, "IsOriginal": original,
          "Title": title, "_freq": freq, "_copy": copy}
    return st


def _sub(index, codec, lang, default=False, forced=False, external=False, hi=False,
         title=""):
    text = codec.lower() not in ("pgssub", "dvd_subtitle", "dvdsub")
    return {"Index": index, "Type": "Subtitle", "Codec": codec, "Language": lang,
            "IsDefault": default, "IsForced": forced, "IsExternal": external,
            "IsHearingImpaired": hi, "IsTextSubtitleStream": text,
            "SupportsExternalStream": text, "Title": title}


TRACK_PROFILES = {
    # Real case (Jellyfin 12.1.0): default PGS, none forced.
    TRACKS_1917_ID: {
        "name": "1917", "container": "mkv", "default_audio": 2, "default_subtitle": 4,
        "streams": [
            _video(0, "hevc"),
            _audio(1, "truehd", "eng", 8, "7.1", 440, title="TrueHD Atmos"),
            _audio(2, "eac3", "fre", 6, "5.1", 660, default=True),
            _sub(3, "PGSSUB", "fre"),
            _sub(4, "PGSSUB", "fre", default=True),
        ]},
    # Subbed: English audio by default, French subtitles (full, forced, ASS, PGS).
    TRACKS_VOSTFR_ID: {
        "name": "Subbed movie", "container": "mkv", "default_audio": 1, "default_subtitle": 6,
        "streams": [
            _video(0, "h264"),
            _audio(1, "eac3", "eng", 6, "5.1", 440, default=True, original=True),
            _audio(2, "eac3", "fre", 6, "5.1", 660),
            _audio(3, "aac", "jpn", 1, "mono", 880, copy=(44100, 1), title="Commentary"),
            _sub(4, "subrip", "fre"),
            _sub(5, "subrip", "eng"),
            _sub(6, "subrip", "fre", forced=True),
            _sub(7, "ass", "fre", title="Full (ASS)"),
            _sub(8, "PGSSUB", "fre", hi=True),
        ]},
    # Dubbed: French audio by default, French forced by default.
    TRACKS_VF_ID: {
        "name": "Dubbed movie", "container": "mkv", "default_audio": 1, "default_subtitle": 3,
        "streams": [
            _video(0, "h264"),
            _audio(1, "ac3", "fre", 6, "5.1", 440, default=True),
            _audio(2, "ac3", "eng", 6, "5.1", 660, original=True),
            _sub(3, "subrip", "fre", forced=True),
            _sub(4, "subrip", "fre"),
            _sub(5, "subrip", "eng"),
        ]},
    # External subtitles (.srt files next to the video): chosen first by the server.
    TRACKS_EXTERNAL_ID: {
        "name": "Movie with external subtitles", "container": "mp4", "default_audio": 1,
        "default_subtitle": 3,
        "streams": [
            _video(0, "h264"),
            _audio(1, "aac", "eng", 2, "stereo", 440, default=True),
            _audio(2, "aac", "fre", 2, "stereo", 660),
            _sub(3, "subrip", "fre", external=True),
            _sub(4, "subrip", "eng", external=True),
        ]},
}


def public_stream(stream):
    return {k: v for k, v in stream.items() if not k.startswith("_")}


def srt_time(seconds):
    ms = int(round(seconds * 1000))
    return f"{ms // 3600000:02d}:{ms // 60000 % 60:02d}:{ms // 1000 % 60:02d},{ms % 1000:03d}"


def make_srt(stream, runtime_s):
    """A 3.5 s cue every 5 s, from 0 s; the text says the track."""
    lang, index = stream.get("Language", ""), stream["Index"]
    out = []
    for k in range(max(0, int(runtime_s // 5))):
        start = 5 * k
        text = f"[{lang}#{index}] " + L("cue {k}", k=k)
        if stream["Codec"] == "ass":
            # ASS -> SRT conversion of the server: position tags, italic, two lines.
            if k == 1:
                text = "{\\an8}<i>" + L("In italics") + "</i>"
            elif k == 2:
                text = L("Two lines{{\\i1}} <b>bold</b>\n[{lang}#{index}] continued", lang=lang, index=index)
        out.append(f"{k + 1}\n{srt_time(start)} --> {srt_time(start + 3.5)}\n{text}\n")
    return "\n".join(out)


class Catalog:
    """Libraries, items and series -> seasons -> episodes hierarchy."""

    def __init__(self, kind, runtime_ticks):
        self.kind = kind
        self.runtime = runtime_ticks
        self.libraries = []
        self.items = {}       # id -> base JSON dict (without UserData)
        self.playable = set()
        self.broken_images = set()
        self.initial_user_data = {}  # id -> starting UserData
        self.track_profiles = {}     # id -> TRACK_PROFILES[...] (demo)
        self._created = 0
        if kind in ("demo", "tracks"):
            self._build_demo()
        else:
            self._build_minimal()

    # --- construction ----------------------------------------------------

    def _add(self, item, parent=None, playable=False, image=True):
        self._created += 1
        item.setdefault("ImageTags", {})
        if image:
            item["ImageTags"]["Primary"] = hashlib.md5(item["Id"].encode()).hexdigest()[:12]
        item["_parent"] = parent
        item["_created"] = self._created
        self.items[item["Id"]] = item
        if playable:
            item["RunTimeTicks"] = self.runtime
            item["MediaSources"] = [{"Id": item["Id"], "Container": "mp4",
                                     "RunTimeTicks": self.runtime}]
            self.playable.add(item["Id"])
        return item

    def _build_minimal(self):
        self.libraries = [{"Id": LIBRARY_ID, "Name": L("Movies"), "CollectionType": "movies"}]
        self._add({"Id": ITEM_ID, "Name": L("Test pattern"), "Type": "Movie",
                   "ProductionYear": 2026}, LIBRARY_ID, playable=True, image=False)
        self._add({"Id": SERIES_ID, "Name": L("Test series"), "Type": "Series",
                   "ProductionYear": 2026}, LIBRARY_ID, image=False)
        self._add({"Id": SEASON_ID, "Name": L("Season {n}", n=1), "Type": "Season",
                   "IndexNumber": 1, "SeriesName": L("Test series"), "SeriesId": SERIES_ID},
                  SERIES_ID, image=False)
        # Listed to test the CLI; not playable (no media behind them).
        for n, eid in enumerate(EPISODE_IDS, start=1):
            self._add({"Id": eid, "Name": L("Episode {n}", n=n), "Type": "Episode",
                       "IndexNumber": n, "ParentIndexNumber": 1, "SeriesName": L("Test series"),
                       "SeriesId": SERIES_ID, "SeasonId": SEASON_ID,
                       "RunTimeTicks": 25 * 60 * TICKS_PER_SECOND}, SEASON_ID, image=False)

    def _build_demo(self):
        self.libraries = [
            {"Id": LIBRARY_ID, "Name": L("Movies"), "CollectionType": "movies"},
            {"Id": SERIES_LIBRARY_ID, "Name": L("TV shows"), "CollectionType": "tvshows"},
        ]
        overview = L("A demonstration movie served by the fake Jellyfin server: the stream is the test "
                     "pattern, whose luminance follows time. This summary is deliberately long to "
                     "check line wrapping and truncation of the detail page, with accents, "
                     "typographic apostrophes and \u201cquotes\u201d.")
        movies = [
            # (name, SortName, year, image)
            (L("Test pattern"), None, 2026, True),
            ("Crazy Kung‐Fu", None, 2004, True),
            ("L’éveil", "l’eveil", 2019, True),
            ("F1®", None, 2025, True),
            ("‎À bout de souffle", "‎à bout de souffle", 1960, True),
            ("Zodiac", None, 2007, True),
            ("Batman Begins", None, 2005, True),
            ("Interstellar", None, 2014, True),
            ("Été 85", None, 2020, True),
            (L("Poster-less work"), None, 2021, False),
            (L("Missing poster"), None, 2022, True),
            (L("An extremely long title to check truncation when displayed"), None,
             2023, True),
            (L("Symbols: \u201cquotes\u201d, 20 \u00b0C, 5 \u20ac, Brand\u2122..."), None, 2024, True),
            ("Amélie", None, 2001, True),
        ]
        for n, (name, sort_name, year, image) in enumerate(movies):
            mid = ITEM_ID if n == 0 else demo_id(0x100 + n)
            item = {"Id": mid, "Name": name, "Type": "Movie", "ProductionYear": year,
                    "Overview": overview, "SortName": sort_name or name.lower()}
            self._add(item, LIBRARY_ID, playable=True, image=image)
            if name == L("Missing poster"):
                self.broken_images.add(mid)
        for mid, profile in (TRACK_PROFILES.items() if self.kind == "tracks" else ()):
            name = L(profile["name"])
            self._add({"Id": mid, "Name": name, "Type": "Movie",
                       "ProductionYear": 2019, "Overview": overview,
                       "SortName": name.lower()}, LIBRARY_ID, playable=True)
            self.track_profiles[mid] = profile
        crazy = demo_id(0x101)
        self.initial_user_data[crazy] = {"PlaybackPositionTicks": int(self.runtime * 0.35),
                                         "Played": False, "LastPlayed": 2}
        self.initial_user_data[demo_id(0x105)] = {"PlaybackPositionTicks": 0, "Played": True}

        shows = [
            (SERIES_ID, L("Test series"), 2026,
             [(L("Season {n}", n=1), 2), (L("Season {n}", n=2), 3)]),
            (demo_id(0x200), L("Chronicles of the Awakening"), 2023,
             [(L("Season {n}", n=1), 4)]),
        ]
        for sid, name, year, seasons in shows:
            self._add({"Id": sid, "Name": name, "Type": "Series", "ProductionYear": year,
                       "SortName": name.lower(),
                       "Overview": L("\u201c{name}\u201d: demonstration series of the fake server.",
                                     name=name)},
                      SERIES_LIBRARY_ID)
            series_tag = self.items[sid]["ImageTags"]["Primary"]
            for s_index, (s_name, count) in enumerate(seasons, start=1):
                season_id = SEASON_ID if sid == SERIES_ID and s_index == 1 else \
                    demo_id(int(sid[-6:], 16) + 0x10 * s_index)
                self._add({"Id": season_id, "Name": s_name, "Type": "Season",
                           "IndexNumber": s_index, "SeriesName": name, "SeriesId": sid},
                          sid, image=False)
                for e in range(1, count + 1):
                    if sid == SERIES_ID and s_index == 1:
                        eid = EPISODE_IDS[e - 1]
                    else:
                        eid = demo_id(int(season_id[-6:], 16) + e)
                    self._add({"Id": eid, "Name": L("Episode {e}: trial no. {e}", e=e),
                               "Type": "Episode", "IndexNumber": e,
                               "ParentIndexNumber": s_index, "SeriesName": name,
                               "SeriesId": sid, "SeasonId": season_id,
                               "SeriesPrimaryImageTag": series_tag,
                               "Overview": L("{s_name}, episode {e} of \u201c{name}\u201d.",
                                             s_name=s_name, e=e, name=name)},
                              season_id, playable=True, image=False)
        s2 = [i for i in self.items.values()
              if i["Type"] == "Episode" and i["SeriesId"] == SERIES_ID
              and i["ParentIndexNumber"] == 2 and i["IndexNumber"] == 1][0]
        self.initial_user_data[s2["Id"]] = {"PlaybackPositionTicks": int(self.runtime * 0.6),
                                            "Played": False, "LastPlayed": 3}
        for e in EPISODE_IDS:
            self.initial_user_data[e] = {"PlaybackPositionTicks": 0, "Played": True}

    # --- requests ----------------------------------------------------------

    def public(self, item, user_data):
        out = {k: v for k, v in item.items() if not k.startswith("_")}
        out["UserData"] = {"PlaybackPositionTicks": user_data.get("PlaybackPositionTicks", 0),
                           "Played": bool(user_data.get("Played", False))}
        return out

    def library_of(self, item):
        parent = item["_parent"]
        while parent is not None and parent not in {lib["Id"] for lib in self.libraries}:
            parent = self.items[parent]["_parent"]
        return parent

    def query(self, parent_id, types, recursive):
        out = []
        for item in self.items.values():
            if types and item["Type"].lower() not in types:
                continue
            if parent_id:
                if recursive:
                    if self.library_of(item) != parent_id and item["_parent"] != parent_id:
                        continue
                elif item["_parent"] != parent_id:
                    continue
            elif not types and item["Type"] in ("Season", "Episode"):
                continue
            out.append(item)
        return out

    def children(self, parent_id, item_type):
        return sorted((i for i in self.items.values()
                       if i["_parent"] == parent_id and i["Type"] == item_type),
                      key=lambda i: i.get("IndexNumber", 0))

    def episodes(self, series_id, season_id):
        seasons = [season_id] if season_id else [s["Id"] for s in
                                                 self.children(series_id, "Season")]
        out = []
        for sid in seasons:
            out.extend(self.children(sid, "Episode"))
        return out


# ---------------------------------------------------------------------------
# Generated PNG posters (pure Python: zlib + struct)
# ---------------------------------------------------------------------------

def _png(width, height, rows):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))
    raw = b"".join(b"\x00" + row for row in rows)
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def make_poster(seed, width):
    """2:3 poster: vertical gradient, light disc, dark banner at the bottom."""
    width = max(16, min(int(width), 600))
    height = width * 3 // 2
    h = hashlib.md5(seed.encode()).digest()
    top = (40 + h[0] % 150, 40 + h[1] % 150, 40 + h[2] % 150)
    bottom = (h[3] % 60, h[4] % 60, h[5] % 60)
    disc = (min(255, top[0] + 90), min(255, top[1] + 90), min(255, top[2] + 90))
    cx, cy, r = width * (30 + h[6] % 40) // 100, height * (25 + h[7] % 30) // 100, width // 4
    band = height * 78 // 100
    rows = []
    for y in range(height):
        t = y / max(1, height - 1)
        base = bytes(int(top[k] + (bottom[k] - top[k]) * t) for k in range(3))
        if y >= band:
            base = bytes(c // 3 for c in base)
        row = bytearray(base * width)
        dy = y - cy
        if abs(dy) < r:
            span = int((r * r - dy * dy) ** 0.5)
            x0, x1 = max(0, cx - span), min(width, cx + span)
            row[x0 * 3:x1 * 3] = bytes(disc) * (x1 - x0)
        rows.append(bytes(row))
    return _png(width, height, rows)

def make_media(path, seconds):
    """Test file: luminance = 16 + 5 x T (position readable in the picture)."""
    video = (f"color=c=black:s=320x180:r=25:d={seconds},format=yuv420p,"
             "geq=lum='16+5*T':cb=128:cr=128")
    audio = f"sine=frequency=440:sample_rate=48000:duration={seconds}"
    tmp = f"{path}.{os.getpid()}.part.mp4"
    subprocess.run(
        ["ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
         "-f", "lavfi", "-i", video, "-f", "lavfi", "-i", audio,
         "-c:v", "libx264", "-preset", "ultrafast", "-g", "25",
         "-pix_fmt", "yuv420p", "-c:a", "aac", "-ac", "2", "-shortest", tmp],
        check=True)
    os.replace(tmp, path)


def probe_duration_ticks(media):
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-show_entries", "format=duration",
         "-of", "default=nw=1:nk=1", media],
        capture_output=True, text=True, check=True)
    return int(float(out.stdout.strip()) * TICKS_PER_SECOND)


class Config:
    """Parameters changeable on the fly through POST /__test/config."""

    def __init__(self, args):
        self.startup_delay = args.startup_delay
        self.readrate = args.readrate
        self.pts_mode = args.pts_mode
        self.video_kbps = args.video_kbps
        self.late_audio = args.late_audio
        self.stream_auth = args.stream_auth
        # User profile (User.Configuration) and track settings.
        self.audio_language = args.audio_language
        self.subtitle_language = args.subtitle_language
        self.subtitle_mode = args.subtitle_mode
        self.play_default_audio = not args.no_play_default_audio
        self.remember_selections = not args.no_remember
        self.omit_defaults = False     # does not announce DefaultAudio/SubtitleStreamIndex
        self.subtitle_delay = 0.0      # latency (s) of the subtitle downloads
        self.min_resume_pct = 5.0
        self.max_resume_pct = 90.0
        self.min_resume_duration_s = args.min_resume_duration

    def as_dict(self):
        return dict(self.__dict__)


class Job:
    """An ffmpeg transcode to a file, read by the streams that attach to it."""

    def __init__(self, state, key, start_ticks, audio=None, burn=False):
        self.device_id, self.item_id, self.play_session_id = key
        self.start_ticks = start_ticks
        self.audio = audio  # audio track of the profile (dict) or None: audio of --media
        self.burn = burn    # burned-in subtitles: red square at the top left
        self.path = os.path.join(state.workdir, f"job-{secrets.token_hex(6)}.ts")
        self.proc = None
        self.killed = threading.Event()
        self.launched = threading.Event()
        self.last_ping_paused = None
        self.ping_count = 0
        self._state = state
        self._delay = state.config.startup_delay
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        # Startup latency interruptible by a DELETE.
        if self.killed.wait(self._delay):
            self.launched.set()
            return
        cmd = self._ffmpeg_cmd()
        with self._state.lock:
            if not self.killed.is_set():
                self.proc = subprocess.Popen(
                    cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL)
        self.launched.set()

    def _ffmpeg_cmd(self):
        cfg = self._state.config
        start_s = self.start_ticks / TICKS_PER_SECOND
        cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin"]
        if cfg.readrate and cfg.readrate > 0:
            cmd += ["-readrate", str(cfg.readrate)]
        cmd += ["-ss", f"{start_s:.3f}", "-i", self._state.media]
        if self.audio is not None:
            # Profile track: sine wave specific to the track. Copyable (AAC): original
            # parameters; otherwise 48 kHz stereo, like the transcoding requested by the URL.
            rate, channels = self.audio.get("_copy") or (48000, 2)
            cmd += ["-f", "lavfi", "-i",
                    f"sine=frequency={self.audio['_freq']}:sample_rate={rate}",
                    "-map", "0:v:0?", "-map", "1:a:0", "-shortest"]
        elif cfg.late_audio and cfg.late_audio > 0:
            # Shifted audio: the first audio packet of the TS arrives after
            # late_audio seconds of video (case of direct streaming, where the
            # video keeps its original bitrate and the audio is transcoded).
            cmd += ["-itsoffset", f"{cfg.late_audio:.3f}", "-ss", f"{start_s:.3f}",
                    "-i", self._state.media, "-map", "0:v:0?", "-map", "1:a:0?"]
        else:
            cmd += ["-map", "0:v:0?", "-map", "0:a:0?"]
        if self.burn:
            cmd += ["-vf", "drawbox=x=0:y=0:w=80:h=45:color=red:t=fill"]
        cmd += ["-c:v", "libx264", "-preset", "ultrafast", "-tune", "zerolatency",
                "-g", "25", "-pix_fmt", "yuv420p"]
        if cfg.video_kbps and cfg.video_kbps > 0:
            # Imposed video bitrate (H.264 padding bytes included): the pattern
            # alone would only weigh a few tens of kB/s.
            k = int(cfg.video_kbps)
            cmd += ["-b:v", f"{k}k", "-minrate", f"{k}k", "-maxrate", f"{k}k",
                    "-bufsize", f"{k}k", "-x264-params", "nal-hrd=cbr:filler=1"]
        if self.audio is not None:
            rate, channels = self.audio.get("_copy") or (48000, 2)
            cmd += ["-c:a", "aac", "-ac", str(channels), "-ar", str(rate)]
        else:
            cmd += ["-c:a", "aac", "-ac", "2", "-ar", "48000"]
        if cfg.pts_mode == "absolute":
            cmd += ["-output_ts_offset", f"{start_s:.3f}"]
        cmd += ["-f", "mpegts", "-flush_packets", "1", "-y", self.path]
        return cmd

    def has_data(self):
        return os.path.exists(self.path) and os.path.getsize(self.path) > 0

    def finished(self):
        return self.launched.is_set() and (self.proc is None or self.proc.poll() is not None)

    def kill(self):
        self.killed.set()
        proc = self.proc
        if proc is not None and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()

    def as_dict(self):
        return {"device_id": self.device_id, "play_session_id": self.play_session_id,
                "start_ticks": self.start_ticks, "killed": self.killed.is_set(),
                "finished": self.finished(), "ping_count": self.ping_count,
                "burn": self.burn,
                "audio_index": self.audio["Index"] if self.audio else None,
                "last_ping_paused": self.last_ping_paused}


class State:
    def __init__(self, args):
        self.lock = threading.Lock()
        self.config = Config(args)
        self.media = os.path.abspath(args.media)
        self.workdir = tempfile.mkdtemp(prefix="fake-jellyfin-")
        self.user = args.user
        self.password = args.password
        self.token = args.token or secrets.token_hex(16)
        self.verbose = args.verbose
        self.runtime_ticks = probe_duration_ticks(self.media)
        self.catalog = Catalog(args.catalog, self.runtime_ticks)
        # UserData per item; the one of the pattern (ITEM_ID) stays self.user_data
        # (driven by /__test/user_data, as for resume).
        self.user_data = {"PlaybackPositionTicks": 0, "Played": False}
        self.user_data_by_item = {k: dict(v) for k, v in
                                  self.catalog.initial_user_data.items()}
        self.user_data_by_item[ITEM_ID] = self.user_data
        self.play_counter = 10
        self.posters = {}
        self.jobs = {}
        self.stream_requests = []
        self.subtitle_requests = []
        self.remembered = {}  # item id -> {"audio": n, "subtitle": n} (RememberXxxSelections)
        self.deletes = []
        self.reports = []
        self.drop_generation = 0
        self.truncate_generation = 0
        self.stall_until = 0.0
        self.refuse_until = 0.0
        self.open_streams = 0

    def user_configuration(self):
        cfg = self.config
        out = {"PlayDefaultAudioTrack": cfg.play_default_audio,
               "SubtitleLanguagePreference": cfg.subtitle_language,
               "DisplayMissingEpisodes": False,
               "SubtitleMode": cfg.subtitle_mode,
               "RememberAudioSelections": cfg.remember_selections,
               "RememberSubtitleSelections": cfg.remember_selections}
        if cfg.audio_language:
            out["AudioLanguagePreference"] = cfg.audio_language
        return out

    def user_dto(self):
        return {"Id": USER_ID, "Name": self.user, "ServerId": SERVER_ID,
                "Configuration": self.user_configuration()}

    def media_sources(self, item_id):
        """MediaSources of an item with tracks, with the default tracks for the profile:
        those remembered by the Progress reports, otherwise those of the static profile."""
        profile = self.catalog.track_profiles[item_id]
        item = self.catalog.items[item_id]
        source = {"Id": item_id, "Container": profile["container"],
                  "RunTimeTicks": item.get("RunTimeTicks", self.runtime_ticks),
                  "MediaStreams": [public_stream(x) for x in profile["streams"]]}
        if not self.config.omit_defaults:
            audio, subtitle = profile["default_audio"], profile["default_subtitle"]
            memo = self.remembered.get(item_id) if self.config.remember_selections else None
            valid = {x["Index"]: x["Type"] for x in profile["streams"]}
            if memo and valid.get(memo.get("audio")) == "Audio":
                audio = memo["audio"]
            if memo and "subtitle" in memo and (memo["subtitle"] == -1 or
                                                valid.get(memo["subtitle"]) == "Subtitle"):
                subtitle = memo["subtitle"]
            source["DefaultAudioStreamIndex"] = audio
            if subtitle is not None:
                source["DefaultSubtitleStreamIndex"] = subtitle
        return [source]

    def mask(self, text):
        return API_KEY_RE.sub(r"\1********", text.replace(self.token, "********"))

    def refusing(self):
        return time.monotonic() < self.refuse_until

    def stalled(self):
        return time.monotonic() < self.stall_until

    def user_data_for(self, item_id):
        return self.user_data_by_item.setdefault(
            item_id, {"PlaybackPositionTicks": 0, "Played": False})

    def apply_stop_position(self, position_ticks, item_id=ITEM_ID):
        """Resume thresholds (Jellyfin model, to verify against a real server)."""
        cfg = self.config
        data = self.user_data_for(item_id)
        runtime = self.runtime_ticks
        if runtime <= 0 or runtime < cfg.min_resume_duration_s * TICKS_PER_SECOND:
            data["PlaybackPositionTicks"] = 0
            return
        pct = position_ticks * 100.0 / runtime
        if pct < cfg.min_resume_pct:
            data["PlaybackPositionTicks"] = 0
        elif pct > cfg.max_resume_pct:
            data["PlaybackPositionTicks"] = 0
            data["Played"] = True
        else:
            data["PlaybackPositionTicks"] = int(position_ticks)

    def kill_jobs(self, predicate):
        with self.lock:
            keys = [k for k, j in self.jobs.items() if predicate(k, j)]
            jobs = [self.jobs.pop(k) for k in keys]
        for job in jobs:
            job.kill()
        return len(jobs)

    def snapshot(self):
        with self.lock:
            return {
                "config": self.config.as_dict(),
                "runtime_ticks": self.runtime_ticks,
                "user_data": dict(self.user_data),
                "active_jobs": [j.as_dict() for j in self.jobs.values()
                                if not j.killed.is_set()],
                "stream_requests": list(self.stream_requests),
                "subtitle_requests": list(self.subtitle_requests),
                "remembered": {k: dict(v) for k, v in self.remembered.items()},
                "deletes": list(self.deletes),
                "reports": list(self.reports),
                "open_streams": self.open_streams,
            }

    def shutdown(self):
        self.kill_jobs(lambda k, j: True)
        shutil.rmtree(self.workdir, ignore_errors=True)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "FakeJellyfin/12.1"

    @property
    def st(self):
        return self.server.state

    def log_message(self, fmt, *args):
        if self.st.verbose:
            sys.stderr.write("[fake-jellyfin] " + self.st.mask(fmt % args) + "\n")

    # --- Utilitaires -----------------------------------------------------

    def _query(self):
        url = urllib.parse.urlsplit(self.path)
        q = urllib.parse.parse_qs(url.query, keep_blank_values=True)
        # ASP.NET binding is case-insensitive: keys normalized to lowercase.
        return url.path, {k.lower(): v[-1] for k, v in q.items()}

    def _read_body(self):
        # Always consume the body, even if the response is an error:
        # otherwise the keep-alive connection would read it again as a new request.
        length = int(self.headers.get("Content-Length") or 0)
        self._raw_body = self.rfile.read(length) if length else b""

    def _body_json(self):
        try:
            return json.loads(self._raw_body or b"{}")
        except ValueError:
            return None

    def _reply(self, status, obj=None):
        body = b"" if obj is None else json.dumps(obj).encode()
        self.send_response(status)
        if obj is not None:
            self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if body:
            self.wfile.write(body)

    def _reset_connection(self):
        """Abruptly closes (RST): simulates a network drop."""
        self.close_connection = True
        try:
            self.connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                       struct.pack("ii", 1, 0))
            self.rfile.close()
            self.connection.close()
        except OSError:
            pass

    def _header_token(self):
        auth = self.headers.get("Authorization") or ""
        if not auth.startswith("MediaBrowser"):
            return None
        m = TOKEN_RE.search(auth)
        return m.group(1) if m else None

    def _api_authorized(self):
        return self._header_token() == self.st.token

    # --- Routage ---------------------------------------------------------

    def do_GET(self):
        self._route("GET")

    def do_POST(self):
        self._route("POST")

    def do_DELETE(self):
        self._route("DELETE")

    def _route(self, method):
        path, q = self._query()
        lower = path.lower()
        self._read_body()
        if lower.startswith("/__test/"):
            return self._test_api(method, lower)
        if self.st.refusing():
            return self._reset_connection()

        if method == "POST" and lower == "/users/authenticatebyname":
            return self._authenticate()

        m = re.fullmatch(r"/videos/([^/]+)/stream(\.ts)?", lower)
        if method == "GET" and m:
            return self._stream(path.split("/")[2], q)

        m = re.fullmatch(r"/items/([^/]+)/images/primary", lower)
        if method == "GET" and m:
            return self._image(m.group(1), q)

        if not self._api_authorized():
            return self._reply(401)

        cat = self.st.catalog
        m = re.fullmatch(r"/videos/([^/]+)/([^/]+)/subtitles/(\d+)(?:/\d+)?/stream\.(\w+)", lower)
        if method == "GET" and m:
            return self._subtitles(m.group(1), m.group(2), int(m.group(3)), m.group(4))
        if method == "GET" and lower == "/users/me":
            return self._reply(200, self.st.user_dto())
        if method == "POST" and lower == "/sessions/logout":
            with self.st.lock:
                self.st.token = secrets.token_hex(16)  # the old token is revoked
            return self._reply(204)
        if method == "GET" and re.fullmatch(r"/users/[^/]+/views", lower):
            return self._reply(200, {"Items": cat.libraries,
                                     "TotalRecordCount": len(cat.libraries)})
        if method == "GET" and (re.fullmatch(r"/users/[^/]+/items/resume", lower)
                                or lower == "/useritems/resume"):
            return self._items_reply(self._resume_items(), q)
        if method == "GET" and re.fullmatch(r"/users/[^/]+/items", lower):
            types = {t.strip().lower() for t in q.get("includeitemtypes", "").split(",")
                     if t.strip()}
            items = cat.query(q.get("parentid", ""), types,
                              q.get("recursive", "").lower() == "true")
            return self._items_reply(self._sorted(items, q), q)
        m = re.fullmatch(r"/shows/([^/]+)/(seasons|episodes)", lower)
        if method == "GET" and m:
            series = cat.items.get(m.group(1))
            if series is None or series["Type"] != "Series":
                return self._reply(404)
            if m.group(2) == "seasons":
                items = cat.children(series["Id"], "Season")
            else:
                items = cat.episodes(series["Id"], q.get("seasonid", ""))
            return self._items_reply(items, q)
        m = re.fullmatch(r"/users/[^/]+/items/([^/]+)", lower)
        if method == "GET" and m:
            item = cat.items.get(m.group(1))
            if item is None:
                return self._reply(404)
            with self.st.lock:
                data = dict(self.st.user_data_for(item["Id"]))
                public = cat.public(item, data)
                if item["Id"] in cat.track_profiles:
                    public["MediaSources"] = self.st.media_sources(item["Id"])
            return self._reply(200, public)
        if method == "DELETE" and lower == "/videos/activeencodings":
            return self._delete_encoding(q)
        reports = {"/sessions/playing": "start",
                   "/sessions/playing/progress": "progress",
                   "/sessions/playing/stopped": "stopped"}
        if method == "POST" and lower in reports:
            return self._report(reports[lower])
        return self._reply(404)

    # --- API Jellyfin ----------------------------------------------------

    def _authenticate(self):
        body = self._body_json()
        if not (self.headers.get("Authorization") or "").startswith("MediaBrowser"):
            return self._reply(400)
        if (body is None or body.get("Username") != self.st.user
                or body.get("Pw") != self.st.password):
            return self._reply(401)
        return self._reply(200, {"User": self.st.user_dto(),
                                 "AccessToken": self.st.token, "ServerId": SERVER_ID})

    def _items_reply(self, items, q):
        try:
            limit = int(q.get("limit") or 0)
        except ValueError:
            limit = 0
        total = len(items)
        if limit > 0:
            items = items[:limit]
        st = self.st
        with st.lock:
            out = [st.catalog.public(i, dict(st.user_data_for(i["Id"]))) for i in items]
        return self._reply(200, {"Items": out, "TotalRecordCount": total})

    def _sorted(self, items, q):
        sort_by = q.get("sortby", "").lower()
        if sort_by == "datecreated":
            key = lambda i: i["_created"]
        elif sort_by == "sortname":
            # Raw server sort: a SortName that starts with U+200E goes to the
            # end of the list (seen on a real server), the client sorts again.
            key = lambda i: i.get("SortName", i["Name"].lower())
        else:
            return items
        return sorted(items, key=key, reverse=q.get("sortorder", "").lower() == "descending")

    def _resume_items(self):
        st = self.st
        with st.lock:
            started = [(st.user_data_by_item[i].get("LastPlayed", 0), i)
                       for i in st.catalog.playable
                       if st.user_data_by_item.get(i, {}).get("PlaybackPositionTicks", 0) > 0
                       and not st.user_data_by_item[i].get("Played")]
        started.sort(reverse=True)
        return [st.catalog.items[i] for _, i in started]

    def _image(self, item_id, q):
        st = self.st
        item = st.catalog.items.get(item_id)
        tag = (item or {}).get("ImageTags", {}).get("Primary")
        if not tag or item_id in st.catalog.broken_images:
            return self._reply(404)
        try:
            width = int(q.get("maxwidth") or 300)
        except ValueError:
            width = 300
        key = (item_id, width)
        with st.lock:
            data = st.posters.get(key)
        if data is None:
            data = make_poster(item_id + tag, width)
            with st.lock:
                st.posters[key] = data
        self.send_response(200)
        self.send_header("Content-Type", "image/png")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _stream_auth(self, q):
        via_query = q.get("api_key") == self.st.token
        via_header = self._header_token() == self.st.token
        mode = self.st.config.stream_auth
        ok = {"both": via_query or via_header, "query": via_query,
              "header": via_header}.get(mode, False)
        kinds = [name for name, used in (("query", via_query), ("header", via_header)) if used]
        return ok, "+".join(kinds)

    def _stream(self, item_id, q):
        st = self.st
        ok, auth_kind = self._stream_auth(q)
        if not ok:
            return self._reply(401)
        item_id = item_id.lower()
        if item_id not in st.catalog.playable:
            return self._reply(404)
        try:
            start_ticks = int(q.get("starttimeticks") or 0)
        except ValueError:
            return self._reply(400)
        # Tracks: only the items with a profile know them.
        profile = st.catalog.track_profiles.get(item_id)
        audio_param, subtitle_param = q.get("audiostreamindex"), q.get("subtitlestreamindex")
        method = q.get("subtitlemethod")
        audio = None
        burn = False
        source_id = q.get("mediasourceid")
        if profile is not None:
            if source_id and source_id.lower() != item_id:
                return self._reply(404)
            by_index = {x["Index"]: x for x in profile["streams"]}
            try:
                a_index = int(audio_param) if audio_param else profile["default_audio"]
                s_index = int(subtitle_param) if subtitle_param else None
            except ValueError:
                return self._reply(400)
            audio = by_index.get(a_index)
            if audio is None or audio["Type"] != "Audio":
                return self._reply(400)
            if s_index is not None:
                sub = by_index.get(s_index)
                if sub is None or sub["Type"] != "Subtitle":
                    return self._reply(400)
                # Without a method, the server burns in (Encode by default).
                burn = (method or "Encode").lower() == "encode"
        key = (q.get("deviceid", ""), item_id, q.get("playsessionid", ""))
        with st.lock:
            job = st.jobs.get(key)
            reused = job is not None and not job.killed.is_set()
            if not reused:
                job = Job(st, key, start_ticks, audio, burn)
                st.jobs[key] = job
            st.stream_requests.append({
                "device_id": key[0], "play_session_id": key[2],
                "media_source_id": source_id, "audio_index": audio_param,
                "subtitle_index": subtitle_param, "subtitle_method": method, "burn": burn,
                "start_ticks": start_ticks, "actual_start_ticks": job.start_ticks,
                "reused": reused, "auth": auth_kind,
                "range": self.headers.get("Range"), "time": time.monotonic()})

        # The server only answers once the first data is transcoded.
        while not job.killed.is_set() and not (job.has_data() or job.finished()):
            time.sleep(0.02)
        if job.killed.is_set():
            return self._reset_connection()
        if not job.has_data():
            return self._reply(500)

        self.send_response(200)
        self.send_header("Content-Type", "video/mp2t")
        self.send_header("Accept-Ranges", "none")
        self.send_header("Transfer-Encoding", "chunked")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True
        self._pump(job)

    def _pump(self, job):
        st = self.st
        generation = st.drop_generation
        truncate = st.truncate_generation
        with st.lock:
            st.open_streams += 1
        try:
            with open(job.path, "rb") as f:
                while True:
                    if st.drop_generation != generation:
                        return self._reset_connection()
                    if st.truncate_generation != truncate:
                        self.wfile.write(b"0\r\n\r\n")  # clean but premature end
                        return
                    if st.stalled():
                        time.sleep(0.02)
                        continue
                    chunk = f.read(65536)
                    if chunk:
                        self._write_chunk(chunk)
                        continue
                    if job.killed.is_set():
                        return self._reset_connection()
                    if job.finished():
                        rest = f.read()
                        if rest:
                            self._write_chunk(rest)
                        self.wfile.write(b"0\r\n\r\n")  # normal end of the stream
                        return
                    time.sleep(0.01)
        except OSError:
            return  # client parti
        finally:
            with st.lock:
                st.open_streams -= 1

    def _subtitles(self, item_id, source_id, index, fmt):
        st = self.st
        item_id = item_id.lower()
        profile = st.catalog.track_profiles.get(item_id)
        with st.lock:
            st.subtitle_requests.append({"item_id": item_id, "source_id": source_id,
                                         "index": index, "format": fmt})
        if profile is None or source_id.lower() != item_id:
            return self._reply(404)
        stream = next((x for x in profile["streams"] if x["Index"] == index), None)
        if stream is None or stream["Type"] != "Subtitle" or not stream["IsTextSubtitleStream"]:
            return self._reply(404)  # image (PGS...): nothing to extract as text
        if fmt.lower() != "srt":
            return self._reply(400)
        delay = st.config.subtitle_delay
        if delay > 0:
            time.sleep(delay)
        body = make_srt(stream, st.runtime_ticks / TICKS_PER_SECOND).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/x-subrip")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _write_chunk(self, data):
        self.wfile.write(b"%x\r\n" % len(data) + data + b"\r\n")

    def _delete_encoding(self, q):
        st = self.st
        device_id = q.get("deviceid", "")
        psid = q.get("playsessionid", "")
        if not device_id or not psid:
            with st.lock:
                st.deletes.append({"device_id": device_id, "play_session_id": psid,
                                   "status": 400, "killed": 0})
            return self._reply(400)
        killed = st.kill_jobs(lambda k, j: k[0] == device_id and k[2] == psid)
        with st.lock:
            st.deletes.append({"device_id": device_id, "play_session_id": psid,
                               "status": 204, "killed": killed})
        return self._reply(204)

    def _report(self, kind):
        st = self.st
        body = self._body_json()
        if body is None:
            return self._reply(400)
        position = body.get("PositionTicks")
        psid = body.get("PlaySessionId") or ""
        with st.lock:
            st.reports.append({"kind": kind, "body": body, "time": time.monotonic()})
            for (dev, _item, job_psid), job in st.jobs.items():
                if psid and job_psid == psid:
                    job.ping_count += 1
                    job.last_ping_paused = bool(body.get("IsPaused"))
            item_id = (body.get("ItemId") or ITEM_ID).lower()
            if kind == "progress" and st.config.remember_selections and \
                    ("AudioStreamIndex" in body or "SubtitleStreamIndex" in body):
                memo = st.remembered.setdefault(item_id, {})
                if "AudioStreamIndex" in body:
                    memo["audio"] = int(body["AudioStreamIndex"])
                if "SubtitleStreamIndex" in body:
                    memo["subtitle"] = int(body["SubtitleStreamIndex"])
            data = st.user_data_for(item_id)
            st.play_counter += 1
            data["LastPlayed"] = st.play_counter
            if kind == "progress" and position is not None:
                data["PlaybackPositionTicks"] = int(position)
            if kind == "stopped" and position is not None:
                st.apply_stop_position(int(position), item_id)
        if kind == "stopped" and psid:
            st.kill_jobs(lambda k, j: k[2] == psid)
        return self._reply(204)

    # --- Test control ------------------------------------------------------

    def _test_api(self, method, lower):
        st = self.st
        if method == "GET" and lower == "/__test/state":
            return self._reply(200, st.snapshot())
        body = self._body_json() if method == "POST" else {}
        if body is None:
            return self._reply(400)
        if lower == "/__test/fault":
            now = time.monotonic()
            with st.lock:
                if body.get("drop"):
                    st.drop_generation += 1
                if body.get("truncate"):
                    st.truncate_generation += 1
                if body.get("stall_s"):
                    st.stall_until = now + float(body["stall_s"])
                if body.get("refuse_s"):
                    st.refuse_until = now + float(body["refuse_s"])
            return self._reply(204)
        if lower == "/__test/config":
            with st.lock:
                for key, value in body.items():
                    if hasattr(st.config, key):
                        setattr(st.config, key, value)
            return self._reply(200, st.config.as_dict())
        if lower == "/__test/user_data":
            with st.lock:
                target = str(body.pop("ItemId", ITEM_ID)).lower()
                st.user_data_for(target).update(body)
            return self._reply(204)
        if lower == "/__test/reset":
            st.kill_jobs(lambda k, j: True)
            with st.lock:
                st.stream_requests.clear()
                st.subtitle_requests.clear()
                st.remembered.clear()
                st.deletes.clear()
                st.reports.clear()
                st.stall_until = st.refuse_until = 0.0
            return self._reply(204)
        return self._reply(404)


class FakeServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address, state):
        super().__init__(address, Handler)
        self.state = state


def main():
    p = argparse.ArgumentParser(description="Fake Jellyfin server to test Pelagia.")
    p.add_argument("--media", help="source file to \"transcode\"")
    p.add_argument("--make-media", metavar="OUTPUT",
                   help="generates a test file (pattern whose luminance follows time)")
    p.add_argument("--seconds", type=float, default=40.0, help="duration for --make-media")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=0, help="0 = free port")
    p.add_argument("--user", default="test")
    p.add_argument("--password", default="test")
    p.add_argument("--token", help="fixed token (default: random)")
    p.add_argument("--startup-delay", type=float, default=0.0,
                   help="latency before the first byte of a job (s); ~10 is realistic")
    p.add_argument("--readrate", type=float, default=0.0,
                   help="transcoding rate as a multiple of real time (0 = maximal)")
    p.add_argument("--pts-mode", choices=["zero", "absolute"], default="zero",
                   help="PTS after startTimeTicks: restarting from ~0 or absolute")
    p.add_argument("--video-kbps", type=int, default=0,
                   help="imposed video bitrate in kbit/s (0 = free); e.g. 20000 to simulate "
                        "high-bitrate direct streaming")
    p.add_argument("--late-audio", type=float, default=0.0,
                   help="delay of the first audio packet (s): the video alone fills the start "
                        "of the stream (ffmpeg probe too short to find the audio)")
    p.add_argument("--stream-auth", choices=["both", "query", "header"], default="both")
    p.add_argument("--min-resume-duration", type=float, default=300.0,
                   help="minimum duration (s) of an item to record a resume position")
    p.add_argument("--catalog", choices=["minimal", "demo", "tracks"], default="minimal",
                   help="catalog served: minimal (network playback tests), demo (interface) or "
                        "tracks (demo + four movies with audio and subtitle tracks)")
    p.add_argument("--language", choices=["en", "fr"], default="en",
                   help="language of the demo content (titles, overviews, subtitle texts)")
    p.add_argument("--audio-language", default="",
                   help="profile AudioLanguagePreference (empty: absent, like on a real server)")
    p.add_argument("--subtitle-language", default="", help="profile SubtitleLanguagePreference")
    p.add_argument("--subtitle-mode", default="Default",
                   choices=["Default", "Always", "OnlyForced", "None", "Smart"],
                   help="profile SubtitleMode")
    p.add_argument("--no-play-default-audio", action="store_true",
                   help="PlayDefaultAudioTrack = false")
    p.add_argument("--no-remember", action="store_true",
                   help="RememberAudioSelections / RememberSubtitleSelections = false")
    p.add_argument("--verbose", action="store_true", help="request log (token masked)")
    args = p.parse_args()

    if args.make_media:
        make_media(args.make_media, args.seconds)
        return 0
    if not args.media:
        p.error("--media is required (or --make-media to generate a file)")

    global LANGUAGE
    LANGUAGE = args.language
    state = State(args)
    server = FakeServer((args.host, args.port), state)

    def stop(*_):
        threading.Thread(target=server.shutdown, daemon=True).start()

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    print(f"READY port={server.server_address[1]}", flush=True)
    try:
        server.serve_forever(poll_interval=0.1)
    finally:
        state.shutdown()
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
