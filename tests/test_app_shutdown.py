#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 steredbits and Pelagia contributors
"""Exit of the complete application (pelagia, dummy SDL drivers).

Session resumed on the fake server, home screen reached, then a request to
close (SIGTERM: SDL turns it into a quit event, like the Options >
Quit menu). Checks: exit code 0 (under ASan/UBSan: no error
or leak), all the "Shutdown: ..." steps in order and "Pelagia finished"
last. On PS5 the last logged step shows where a crash
at shutdown happened.

Usage : test_app_shutdown.py --app <pelagia> --server <fake_jellyfin_server.py>
                             --workdir <folder>
"""
import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import time

SKIP = 77
TOKEN = "abcdefghijklmnop0123456789"
STEPS = [
    "Shutdown: quit requested, closing the interface",
    "Shutdown: interface closed",
    "Shutdown: poster cache released",
    "Shutdown: \"posters\" threads joined",
    "Shutdown: \"api\" threads joined",
    "Shutdown: result queue emptied",
    "Shutdown: backend and disk cache closed",
    "Shutdown: canvas, texts and files released",
    "Shutdown: audio output closed",
    "Shutdown: SDL platform",
    "Shutdown: gamepads closed",
    "Shutdown: textures destroyed",
    "Shutdown: renderer destroyed",
    "Shutdown: window destroyed",
    "Shutdown: SDL_Quit",
    "Shutdown: SDL_Quit done",
    "Shutdown: platform stopped",
    "Pelagia finished",
]


def wait_for(path, text, timeout_s, proc=None):
    end = time.monotonic() + timeout_s
    while time.monotonic() < end:
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                if text in f.read():
                    return True
        except FileNotFoundError:
            pass
        if proc is not None and proc.poll() is not None:
            return False
        time.sleep(0.1)
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", required=True)
    ap.add_argument("--server", required=True)
    ap.add_argument("--workdir", required=True)
    args = ap.parse_args()
    if not shutil.which("ffmpeg"):
        print("ffmpeg unavailable: test skipped", file=sys.stderr)
        return SKIP

    work = os.path.abspath(args.workdir)
    shutil.rmtree(work, ignore_errors=True)
    for sub in ("config", "cache"):
        os.makedirs(os.path.join(work, sub))
    media = os.path.join(work, "mire.mp4")
    subprocess.run([sys.executable, args.server, "--make-media", media, "--seconds", "5"],
                   check=True)

    server = subprocess.Popen(
        [sys.executable, args.server, "--media", media, "--port", "0", "--catalog", "demo",
         "--token", TOKEN],
        stdout=subprocess.PIPE, text=True)
    app = None
    try:
        line = server.stdout.readline()
        if not line.startswith("READY port="):
            print("fake server not started:", line, file=sys.stderr)
            return 1
        port = int(line.split("=")[1])
        with open(os.path.join(work, "config", "session.json"), "w") as f:
            json.dump({"server": f"http://127.0.0.1:{port}", "token": TOKEN}, f)

        log = os.path.join(work, "app.log")
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        with open(log, "w") as out:
            app = subprocess.Popen(
                [args.app, "--config-dir", os.path.join(work, "config"),
                 "--cache-dir", os.path.join(work, "cache"), "--verbose"],
                stdout=out, stderr=subprocess.STDOUT, env=env)
        if not wait_for(log, "Screen: home", 30, app):
            print("home screen not reached", file=sys.stderr)
            return 1
        time.sleep(1.5)  # posters being loaded: threads active at shutdown
        app.send_signal(signal.SIGTERM)
        try:
            code = app.wait(timeout=30)
        except subprocess.TimeoutExpired:
            print("the application does not close", file=sys.stderr)
            return 1

        text = open(log, encoding="utf-8", errors="replace").read()
        failures = []
        if code != 0:
            failures.append(f"code de sortie {code}")
        pos = -1
        for step in STEPS:
            found = text.find(step, pos + 1)
            if found < 0:
                failures.append(f"step missing or out of order: {step}")
            else:
                pos = found
        if "Sanitizer" in text or "runtime error" in text:
            failures.append("ASan/UBSan error in the log")
        if failures:
            print("\n".join(failures), file=sys.stderr)
            print(text[-3000:], file=sys.stderr)
            return 1
        print("sortie de l'application : OK")
        return 0
    finally:
        if app is not None and app.poll() is None:
            app.kill()
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()


if __name__ == "__main__":
    sys.exit(main())
