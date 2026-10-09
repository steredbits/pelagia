#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KevinJCode and Pelagia contributors
# Regenerates the screenshots of docs/screenshots/en and docs/screenshots/fr: fake
# Jellyfin server ("demo" catalog, then "tracks" for the tracks, 120 s test pattern, content
# in the language of the screenshots) + pelagia-capture (software rendering, no display).
# Usage: tools/update_screenshots.sh [build-folder]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build-linux}"
OUT="$ROOT/docs/screenshots"
WORK="$(mktemp -d)"
SERVER_PID=""

cleanup() {
  if [ -n "$SERVER_PID" ]; then kill "$SERVER_PID" 2>/dev/null || true; fi
  rm -rf "$WORK"
}
trap cleanup EXIT

# Starts the fake server with catalog $1 and content language $2; port in $PORT.
start_server() {
  if [ -n "$SERVER_PID" ]; then kill "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; fi
  python3 "$ROOT/tools/fake_jellyfin_server.py" --media "$WORK/pattern.mp4" --port 0 \
    --catalog "$1" --language "$2" --min-resume-duration 0 > "$WORK/server.out" 2>&1 &
  SERVER_PID=$!
  for _ in $(seq 1 100); do
    grep -q "^READY port=" "$WORK/server.out" 2>/dev/null && break
    sleep 0.1
  done
  PORT="$(sed -n 's/^READY port=//p' "$WORK/server.out")"
  [ -n "$PORT" ] || { echo "fake server not started" >&2; cat "$WORK/server.out" >&2; exit 1; }
}

python3 "$ROOT/tools/fake_jellyfin_server.py" --make-media "$WORK/pattern.mp4" --seconds 120
for LANG_CODE in en fr; do
  DEST="$OUT/$LANG_CODE"
  mkdir -p "$DEST"
  rm -f "$DEST"/*.png
  start_server demo "$LANG_CODE"
  "$BUILD/pelagia-capture" --server "http://127.0.0.1:$PORT" --work-dir "$WORK/state-$LANG_CODE" \
    --out "$DEST" --language "$LANG_CODE"
  start_server tracks "$LANG_CODE"
  "$BUILD/pelagia-capture" --server "http://127.0.0.1:$PORT" --work-dir "$WORK/state-tracks-$LANG_CODE" \
    --out "$DEST" --scenario tracks --language "$LANG_CODE"
done
echo "Screenshots written to $OUT"
