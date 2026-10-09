#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 steredbits and Pelagia contributors
"""Self-test of the fake Jellyfin server (ctest: test_fake_server).

Checks that the simulation is faithful to what the client has to face:
12.1 auth, progressive TS without Accept-Ranges, startTimeTicks (decoded content),
job reuse, DELETE ActiveEncodings, reporting, drops.
Exit code 77 (skipped by ctest) if python/ffmpeg/ffprobe are missing.
"""

import argparse
import http.client
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

SKIP = 77
TPS = 10_000_000
ITEM = "f00dfeed0000000000000000000000aa"
AUTH_BASE = 'MediaBrowser Client="test", Device="test", DeviceId="dev1", Version="0"'

failures = []


def check(cond, message):
    print(("ok    " if cond else "FAIL  ") + message, flush=True)
    if not cond:
        failures.append(message)


class Server:
    def __init__(self, script, media, *extra):
        self.proc = subprocess.Popen(
            [sys.executable, script, "--media", media, "--port", "0",
             "--min-resume-duration", "0", *extra],
            stdout=subprocess.PIPE, text=True)
        line = self.proc.stdout.readline()
        if not line.startswith("READY port="):
            raise RuntimeError("the fake server did not start: " + line)
        self.port = int(line.strip().split("=")[1])

    def stop(self):
        self.proc.terminate()
        self.proc.wait(timeout=10)


def request(port, method, path, headers=None, body=None, timeout=15):
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=timeout)
    data = json.dumps(body).encode() if body is not None else None
    hdrs = dict(headers or {})
    if data is not None:
        hdrs["Content-Type"] = "application/json"
    conn.request(method, path, body=data, headers=hdrs)
    resp = conn.getresponse()
    payload = resp.read()
    conn.close()
    return resp.status, resp, payload


def state(port):
    return json.loads(request(port, "GET", "/__test/state")[2])


def stream_path(token=None, start_s=0, psid="", device="dev1"):
    path = f"/Videos/{ITEM}/stream.ts?static=false&deviceId={device}"
    if token:
        path += f"&api_key={token}"
    if start_s:
        path += f"&startTimeTicks={int(start_s * TPS)}"
    if psid:
        path += f"&playSessionId={psid}"
    return path


def read_stream(port, path, nbytes, headers=None):
    """Reads nbytes of the stream; returns (status, headers, data, first-byte delay)."""
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=20)
    t0 = time.monotonic()
    conn.request("GET", path, headers=headers or {})
    resp = conn.getresponse()
    ttfb = time.monotonic() - t0
    data = b""
    if resp.status == 200:
        while len(data) < nbytes:
            chunk = resp.read(min(65536, nbytes - len(data)))
            if not chunk:
                break
            data += chunk
    conn.close()
    return resp.status, resp, data, ttfb


def decoded_luma_and_pts(ts_bytes, workdir):
    """Mean luminance of the 1st frame and PTS of the 1st video packet of the stream."""
    path = os.path.join(workdir, "probe.ts")
    with open(path, "wb") as f:
        f.write(ts_bytes)
    # Raw Y plane (yuv420p): the "gray" output would stretch the 16-235 range.
    raw = subprocess.run(
        ["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", path, "-frames:v", "1",
         "-f", "rawvideo", "-pix_fmt", "yuv420p", "-"],
        capture_output=True, check=True).stdout
    y_plane = raw[:320 * 180]
    luma = sum(y_plane) / max(1, len(y_plane))
    pts = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-read_intervals", "%+#1",
         "-show_entries", "packet=pts_time", "-of", "default=nw=1:nk=1", path],
        capture_output=True, text=True, check=True).stdout.split()
    return luma, float(pts[0]) if pts else -1.0


ID_1917 = "de300000000000000000000000000300"
ID_VOSTFR = "de300000000000000000000000000301"
ID_EXTERNAL = "de300000000000000000000000000303"


def get_json(port, path, header):
    status, _, body = request(port, "GET", path, header)
    return status, (json.loads(body) if body and body[:1] in (b"{", b"[") else None)


def stream_item(port, item, query, header, nbytes=300_000):
    path = f"/Videos/{item}/stream.ts?static=false&deviceId=dev1&{query}"
    return read_stream(port, path, nbytes, header)


def decode_audio_frequency(ts_bytes, workdir):
    """(estimated frequency Hz, sampling rate, channels) of the stream's audio."""
    path = os.path.join(workdir, "probe-audio.ts")
    with open(path, "wb") as f:
        f.write(ts_bytes)
    info = json.loads(subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries",
         "stream=sample_rate,channels", "-of", "json", path],
        capture_output=True, text=True, check=True).stdout)["streams"][0]
    rate, channels = int(info["sample_rate"]), int(info["channels"])
    raw = subprocess.run(
        ["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", path, "-t", "1", "-map", "0:a:0",
         "-ac", "1", "-f", "s16le", "-"], capture_output=True, check=True).stdout
    samples = [int.from_bytes(raw[i:i + 2], "little", signed=True) for i in range(0, len(raw) - 1, 2)]
    crossings = sum(1 for a, b in zip(samples, samples[1:]) if a < 0 <= b)
    return crossings * rate / max(1, len(samples)), rate, channels


def decoded_corner_chroma(ts_bytes, workdir):
    """Mean V value of the top-left corner (80x45 px) of the 1st frame: 128 on the pattern, ~240 if burned in."""
    path = os.path.join(workdir, "probe-burn.ts")
    with open(path, "wb") as f:
        f.write(ts_bytes)
    raw = subprocess.run(
        ["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", path, "-frames:v", "1",
         "-f", "rawvideo", "-pix_fmt", "yuv420p", "-"], capture_output=True, check=True).stdout
    v_plane = raw[320 * 180 + 160 * 90:]
    rows = [v_plane[y * 160:y * 160 + 40] for y in range(20)]  # 40x20 corner in chroma
    values = [b for row in rows for b in row]
    return sum(values) / len(values)


def test_tracks(args, media, workdir):
    """"tracks" catalog: tracks, profile, stream per track, burn-in, subtitles."""
    srv = Server(args.server, media, "--catalog", "tracks", "--startup-delay", "0.2")
    port = srv.port
    try:
        status, _, body = request(port, "POST", "/Users/AuthenticateByName",
                                  {"Authorization": AUTH_BASE}, {"Username": "test", "Pw": "test"})
        auth = json.loads(body)
        token, user_id = auth["AccessToken"], auth["User"]["Id"]
        header = {"Authorization": AUTH_BASE + f', Token="{token}"'}

        # User profile: as on a real server (Default, PlayDefaultAudioTrack).
        cfg = auth["User"]["Configuration"]
        check(cfg["SubtitleMode"] == "Default" and cfg["PlayDefaultAudioTrack"] is True
              and "AudioLanguagePreference" not in cfg and cfg["SubtitleLanguagePreference"] == "",
              "profile: configuration of a real server (auth)")
        me = json.loads(request(port, "GET", "/Users/Me", header)[2])
        check(me["Configuration"]["RememberAudioSelections"] is True, "profile: /Users/Me")

        # "1917": English TrueHD, French E-AC3 by default, two PGS, defaults 2 and 4.
        status, item = get_json(port, f"/Users/{user_id}/Items/{ID_1917}", header)
        src = item["MediaSources"][0]
        streams = src["MediaStreams"]
        check(status == 200 and [x["Index"] for x in streams] == [0, 1, 2, 3, 4], "1917: 5 tracks")
        check(src["DefaultAudioStreamIndex"] == 2 and src["DefaultSubtitleStreamIndex"] == 4,
              "1917: default tracks 2 and 4")
        check(streams[3]["Codec"] == "PGSSUB" and streams[3]["IsTextSubtitleStream"] is False
              and not streams[3]["IsForced"] and streams[4]["IsDefault"], "1917: PGS not forced")
        check(all(not k.startswith("_") for x in streams for k in x), "tracks: no private field")

        # One audio track per frequency: the served content says which one was served.
        for idx, expected in ((1, 440), (2, 660)):
            status, _, data, _ = stream_item(
                port, ID_VOSTFR, f"mediaSourceId={ID_VOSTFR}&audioStreamIndex={idx}"
                                 f"&playSessionId=a{idx}", header)
            freq, rate, channels = decode_audio_frequency(data, tmp_dir(workdir))
            check(status == 200 and abs(freq - expected) < 25 and rate == 48000 and channels == 2,
                  f"stream: audioStreamIndex={idx} -> {expected} Hz stereo 48 kHz (measured {freq:.0f})")
        # Copyable track (AAC mono 44.1 kHz): original parameters kept.
        status, _, data, _ = stream_item(port, ID_VOSTFR, "audioStreamIndex=3&playSessionId=a3",
                                         header)
        freq, rate, channels = decode_audio_frequency(data, tmp_dir(workdir))
        check(status == 200 and abs(freq - 880) < 25 and rate == 44100 and channels == 1,
              f"stream: copyable track -> 880 Hz mono 44.1 kHz (measured {freq:.0f}, {rate}, {channels})")
        # Without audioStreamIndex: default track of the source (no. 1, 440 Hz).
        status, _, data, _ = stream_item(port, ID_VOSTFR, "playSessionId=a4", header)
        freq, _, _ = decode_audio_frequency(data, tmp_dir(workdir))
        check(status == 200 and abs(freq - 440) < 25, "stream: no parameter -> default track")
        status, _, _, _ = stream_item(port, ID_VOSTFR, "audioStreamIndex=4&playSessionId=a5", header)
        check(status == 400, "stream: a subtitle index is not an audio track -> 400")
        status, _, _, _ = stream_item(port, ID_VOSTFR, "mediaSourceId=autre&playSessionId=a6", header)
        check(status == 404, "stream: unknown mediaSourceId -> 404")

        # Burn-in: subtitleMethod=Encode adds the red square; without an index, nothing.
        _, _, data, _ = stream_item(port, ID_VOSTFR, "playSessionId=b1", header)
        plain = decoded_corner_chroma(data, tmp_dir(workdir))
        _, _, data, _ = stream_item(
            port, ID_VOSTFR, "subtitleStreamIndex=4&subtitleMethod=Encode&playSessionId=b2", header)
        burned = decoded_corner_chroma(data, tmp_dir(workdir))
        check(abs(plain - 128) < 8 and burned > 200, f"stream: Encode burns in (V {plain:.0f} -> {burned:.0f})")
        _, _, data, _ = stream_item(port, ID_VOSTFR, "subtitleStreamIndex=4&subtitleMethod=External"
                                    "&playSessionId=b3", header)
        check(decoded_corner_chroma(data, tmp_dir(workdir)) < 140, "stream: External does not burn in")
        reqs = state(port)["stream_requests"]
        check(any(r["burn"] and r["subtitle_index"] == "4" and r["subtitle_method"] == "Encode"
                  for r in reqs), "state: burn-in request recorded")

        # Text subtitles: SRT served, ASS converted, PGS not found, authentication required.
        path = f"/Videos/{ID_VOSTFR}/{ID_VOSTFR}/Subtitles/%d/Stream.srt"
        status, resp, body = request(port, "GET", path % 4, header)
        text = body.decode("utf-8")
        check(status == 200 and "00:00:00,000 --> 00:00:03,500" in text and "[fre#4] cue 0" in text,
              "subtitles: SRT of track 4")
        status, _, body = request(port, "GET", path % 7, header)
        check(status == 200 and "{\\an8}<i>In italics</i>" in body.decode("utf-8"),
              "subtitles: ASS converted to SRT with tags")
        check(request(port, "GET", path % 8, header)[0] == 404, "subtitles: PGS -> 404")
        check(request(port, "GET", path % 1, header)[0] == 404, "subtitles: audio track -> 404")
        check(request(port, "GET", path % 4)[0] == 401, "subtitles: without a token -> 401")
        check(request(port, "GET", path % 4 + "?x=1", header)[0] == 200, "subtitles: request with a query")
        check(len(state(port)["subtitle_requests"]) >= 4, "state: subtitle requests recorded")

        # External subtitles: server default = external.
        _, item = get_json(port, f"/Users/{user_id}/Items/{ID_EXTERNAL}", header)
        streams = item["MediaSources"][0]["MediaStreams"]
        check(item["MediaSources"][0]["DefaultSubtitleStreamIndex"] == 3 and streams[3]["IsExternal"],
              "external: external subtitles by default")

        # Memorization: a Progress with the tracks becomes the item's default.
        report = {"ItemId": ID_VOSTFR, "PlaySessionId": "r1", "PositionTicks": 100_000_000,
                  "AudioStreamIndex": 2, "SubtitleStreamIndex": -1}
        check(request(port, "POST", "/Sessions/Playing/Progress", header, report)[0] == 204,
              "report: Progress with tracks accepted")
        _, item = get_json(port, f"/Users/{user_id}/Items/{ID_VOSTFR}", header)
        src = item["MediaSources"][0]
        check(src["DefaultAudioStreamIndex"] == 2 and src["DefaultSubtitleStreamIndex"] == -1,
              "memorization: reported tracks returned as defaults")
        report["SubtitleStreamIndex"] = 4
        request(port, "POST", "/Sessions/Playing/Progress", header, report)
        _, item = get_json(port, f"/Users/{user_id}/Items/{ID_VOSTFR}", header)
        check(item["MediaSources"][0]["DefaultSubtitleStreamIndex"] == 4,
              "memorization: subtitles updated")
        # Driven settings: without memorization, defaults omitted, new profile.
        request(port, "POST", "/__test/config", body={"remember_selections": False})
        _, item = get_json(port, f"/Users/{user_id}/Items/{ID_VOSTFR}", header)
        check(item["MediaSources"][0]["DefaultAudioStreamIndex"] == 1, "memorization disabled")
        request(port, "POST", "/__test/config", body={"omit_defaults": True, "subtitle_mode": "Smart",
                                                      "subtitle_language": "fre"})
        _, item = get_json(port, f"/Users/{user_id}/Items/{ID_VOSTFR}", header)
        check("DefaultAudioStreamIndex" not in item["MediaSources"][0], "omit_defaults: no default announced")
        me = json.loads(request(port, "GET", "/Users/Me", header)[2])
        check(me["Configuration"]["SubtitleMode"] == "Smart"
              and me["Configuration"]["SubtitleLanguagePreference"] == "fre", "profile changeable on the fly")
    finally:
        srv.stop()


def tmp_dir(workdir):
    path = os.path.join(workdir, "probe")
    os.makedirs(path, exist_ok=True)
    return path


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--server", required=True)
    p.add_argument("--workdir", required=True)
    args = p.parse_args()
    for tool in ("ffmpeg", "ffprobe"):
        if not shutil.which(tool):
            print(f"{tool} missing: test skipped")
            return SKIP

    os.makedirs(args.workdir, exist_ok=True)
    media = os.path.join(args.workdir, "mire_40s.mp4")
    if not os.path.exists(media):
        subprocess.run([sys.executable, args.server, "--make-media", media, "--seconds", "40"],
                       check=True)
    test_tracks(args, media, args.workdir)
    tmp = tempfile.mkdtemp(prefix="test-fake-server-")
    srv = Server(args.server, media, "--startup-delay", "0.3")
    port = srv.port
    try:
        # --- Authentification (comportement 12.1) ------------------------
        status, _, _ = request(port, "POST", "/Users/AuthenticateByName",
                               {"Authorization": AUTH_BASE}, {"Username": "test", "Pw": "faux"})
        check(status == 401, "auth: wrong password -> 401")
        status, _, body = request(port, "POST", "/Users/AuthenticateByName",
                                  {"Authorization": AUTH_BASE}, {"Username": "test", "Pw": "test"})
        check(status == 200, "auth: valid credentials -> 200")
        auth = json.loads(body)
        token, user_id = auth["AccessToken"], auth["User"]["Id"]
        header = {"Authorization": AUTH_BASE + f', Token="{token}"'}

        status, _, _ = request(port, "GET", f"/Users/{user_id}/Views?api_key={token}")
        check(status == 401, "API: api_key in the query refused (401)")
        status, _, _ = request(port, "GET", f"/Users/{user_id}/Views", {"X-Emby-Token": token})
        check(status == 401, "API: X-Emby-Token refused (401)")
        status, _, _ = request(port, "GET", f"/Users/{user_id}/Views", header)
        check(status == 200, "API: MediaBrowser header accepted")
        status, _, body = request(port, "GET", f"/Users/{user_id}/Items/{ITEM}", header)
        item = json.loads(body)
        check(status == 200 and abs(item["RunTimeTicks"] - 40 * TPS) < TPS,
              "item: RunTimeTicks ~ 40 s")
        check("PlaybackPositionTicks" in item["UserData"], "item: UserData present")
        base = f"/Users/{user_id}/Items?recursive=true&includeItemTypes="
        movies = json.loads(request(port, "GET", base + "Movie", header)[2])["Items"]
        series = json.loads(request(port, "GET", base + "Series", header)[2])["Items"]
        check([i["Type"] for i in movies] == ["Movie"], "items: includeItemTypes=Movie -> one movie")
        check([i["Type"] for i in series] == ["Series"], "items: includeItemTypes=Series -> one series")
        eps = json.loads(request(port, "GET", f"/Shows/{series[0]['Id']}/Episodes?userId={user_id}",
                                 header)[2])["Items"]
        check(len(eps) == 2 and eps[0]["IndexNumber"] == 1 and eps[0]["ParentIndexNumber"] == 1,
              "episodes: two episodes numbered S01Exx")
        status, _, _ = request(port, "GET", "/Shows/unknown/Episodes", header)
        check(status == 404, "episodes: unknown series -> 404")

        # --- Stream : TS progressif, latence, startTimeTicks -----------------
        status, resp, data, ttfb = read_stream(port, stream_path(token, 10, "p1"), 400_000)
        check(status == 200, "stream: 200 with api_key")
        check(resp.getheader("Accept-Ranges") == "none", "stream: Accept-Ranges: none")
        check(resp.getheader("Content-Length") is None, "stream: no Content-Length")
        check(resp.getheader("Transfer-Encoding") == "chunked", "stream: Transfer-Encoding chunked")
        check(len(data) >= 376 and data[0] == 0x47 and data[188] == 0x47,
              "stream: MPEG-TS sync bytes")
        check(ttfb >= 0.3, f"stream: startup latency respected ({ttfb:.2f} s)")
        luma, pts = decoded_luma_and_pts(data, tmp)
        check(abs(luma - (16 + 5 * 10)) < 8,
              f"stream: content decoded at 10 s (luminance {luma:.0f}, expected 66)")
        check(0.0 <= pts < 3.0, f"stream: PTS restarting from ~0 in zero mode ({pts:.2f})")

        # Reuse: same deviceId + playSessionId -> startTimeTicks ignored.
        status, _, data, _ = read_stream(port, stream_path(token, 25, "p1"), 400_000)
        last = state(port)["stream_requests"][-1]
        check(last["reused"] and last["actual_start_ticks"] == 10 * TPS,
              "job: reused with an identical playSessionId, startTimeTicks ignored")
        luma, _ = decoded_luma_and_pts(data, tmp)
        check(abs(luma - 66) < 8, f"reused job: content of the old position ({luma:.0f})")

        # New playSessionId without DELETE: new job, the old one continues.
        read_stream(port, stream_path(token, 25, "p2"), 1000)
        jobs = state(port)["active_jobs"]
        check(len(jobs) == 2, f"job: the old job survives without DELETE ({len(jobs)} active)")

        # DELETE ActiveEncodings.
        status, _, _ = request(port, "DELETE", "/Videos/ActiveEncodings?deviceId=dev1", header)
        check(status == 400, "DELETE without playSessionId -> 400")
        status, _, _ = request(port, "DELETE",
                               "/Videos/ActiveEncodings?deviceId=dev1&playSessionId=p1", header)
        check(status == 204, "DELETE with playSessionId -> 204")
        jobs = state(port)["active_jobs"]
        check([j["play_session_id"] for j in jobs] == ["p2"], "DELETE: only job p1 is stopped")

        # Range ignored, header-only auth, refusal without auth.
        status, _, data, _ = read_stream(port, stream_path(None, 0, "p3"), 188,
                                         {**header, "Range": "bytes=100000-"})
        check(status == 200 and data[:1] == b"\x47", "stream: Range ignored, header only accepted")
        status, _, _, _ = read_stream(port, stream_path(None, 0, "p4"), 188)
        check(status == 401, "stream: without authentication -> 401")
        request(port, "POST", "/__test/config", body={"stream_auth": "header"})
        status, _, _, _ = read_stream(port, stream_path(token, 0, "p5"), 188)
        check(status == 401, "stream (--stream-auth header): api_key alone refused")
        request(port, "POST", "/__test/config", body={"stream_auth": "both", "pts_mode": "absolute"})

        # PTS absolus.
        status, _, data, _ = read_stream(port, stream_path(token, 20, "p6"), 400_000)
        _, pts = decoded_luma_and_pts(data, tmp)
        check(19.0 < pts < 23.0, f"stream (absolute pts): 1st PTS ~ 20 s ({pts:.2f})")
        request(port, "POST", "/__test/config", body={"pts_mode": "zero"})

        # --- Reporting and resume thresholds ---------------------------------
        def report(kind, seconds, psid="p2"):
            path = {"start": "/Sessions/Playing", "progress": "/Sessions/Playing/Progress",
                    "stopped": "/Sessions/Playing/Stopped"}[kind]
            return request(port, "POST", path, header,
                           {"ItemId": ITEM, "PlaySessionId": psid,
                            "PositionTicks": int(seconds * TPS), "IsPaused": False})[0]

        check(report("start", 0) == 204, "Sessions/Playing -> 204")
        check(report("progress", 20) == 204, "Sessions/Playing/Progress -> 204")
        st = state(port)
        check(st["user_data"]["PlaybackPositionTicks"] == 20 * TPS, "progress: position recorded")
        check(any(j["ping_count"] > 0 for j in st["active_jobs"]),
              "progress: ping of the PlaySessionId job")
        check(report("stopped", 1) == 204 and
              state(port)["user_data"]["PlaybackPositionTicks"] == 0,
              "stopped < 5%: position cleared")
        report("stopped", 38)
        ud = state(port)["user_data"]
        check(ud["PlaybackPositionTicks"] == 0 and ud["Played"], "stopped > 90%: marked played")
        request(port, "POST", "/__test/user_data", body={"Played": False})
        report("stopped", 20)
        check(state(port)["user_data"]["PlaybackPositionTicks"] == 20 * TPS,
              "stopped between 5 and 90%: resume position recorded")
        check(not any(j["play_session_id"] == "p2" for j in state(port)["active_jobs"]),
              "stopped: PlaySessionId job stopped")
        status, _, _ = request(port, "POST", "/Sessions/Playing", body={"ItemId": ITEM})
        check(status == 401, "reporting without authentication -> 401")

        # --- Coupures --------------------------------------------------------
        request(port, "POST", "/__test/fault", body={"refuse_s": 1.0})
        try:
            status, _, _ = request(port, "GET", f"/Users/{user_id}/Views", header, timeout=5)
            refused = False
        except (ConnectionError, http.client.HTTPException, OSError):
            refused = True
        check(refused, "fault refuse: connection cut")
        time.sleep(1.1)
        status, _, _ = request(port, "GET", f"/Users/{user_id}/Views", header)
        check(status == 200, "fault refuse: service restored after the delay")

        # Real-time transcoding: the stream is still going on during the drops.
        request(port, "POST", "/__test/config", body={"readrate": 1.0, "startup_delay": 0.1})
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=20)
        conn.request("GET", stream_path(token, 0, "p7"))
        resp = conn.getresponse()
        resp.read(10_000)
        request(port, "POST", "/__test/fault", body={"drop": True})
        dropped = False
        try:
            for _ in range(200):
                if not resp.read(65536):
                    dropped = True
                    break
        except (ConnectionError, http.client.HTTPException, OSError):
            dropped = True
        conn.close()
        check(dropped, "fault drop: open stream interrupted")

        def keeps_flowing(resp, window_s):
            """True if data arrives with no 0.5 s gap during window_s."""
            end = time.monotonic() + window_s
            try:
                while time.monotonic() < end:
                    if not resp.read1(65536):
                        return False
            except (TimeoutError, OSError):
                return False
            return True

        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=0.5)
        conn.request("GET", stream_path(token, 0, "p8"))
        resp = conn.getresponse()
        check(keeps_flowing(resp, 1.0), "real-time stream: data arrives continuously")
        request(port, "POST", "/__test/fault", body={"stall_s": 2.0})
        check(not keeps_flowing(resp, 1.5), "fault stall: no more data, connection open")
        conn.close()  # after a timeout, the Python reader is no longer reliable
        time.sleep(2.0)
        status, _, data, _ = read_stream(port, stream_path(token, 0, "p9"), 1000)
        check(status == 200 and len(data) == 1000, "fault stall: the service resumes after the freeze")
        request(port, "POST", "/__test/config", body={"readrate": 0.0, "startup_delay": 0.3})
    finally:
        srv.stop()
        shutil.rmtree(tmp, ignore_errors=True)

    print(f"\n{len(failures)} failure(s)" if failures else "\nOK: fake server conforms")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
