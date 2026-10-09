#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KevinJCode and Pelagia contributors
# Installs (or updates) Pelagia on the PS5 as a websrv homebrew, and
# launches it if requested. Sends the content of build-ps5/Pelagia/ (or of a zip of
# the CI artifact "Pelagia" or of the release zip, --zip option):
#   /data/homebrew/Pelagia/eboot.elf
#   /data/homebrew/Pelagia/sce_sys/icon0.png
# Never touches data/ (session, cache) nor logs/.
#
# Requirements on the console: ftpsrv (port 2121) and websrv (port 8080). On the
# PC: curl, and unzip or python3 for --zip. Build first: ci/build-ps5.sh
#
# Usage : tools/deploy-ps5.sh [options] <ip-ps5>
#   --conf <file>     also sends this file as pelagia.conf (server= only)
#   --launch          then launches the application (websrv GET /hbldr) and
#                     shows its standard output (the logs) until it closes
#   --logs            fetches logs/pelagia.log (last run) and shows it
#   --build-dir <d>   default: build-ps5
#   --zip <file>      installs from the CI artifact zip "Pelagia" as
#                     downloaded (eboot.elf, sce_sys/ and pelagia.conf.example at the
#                     root; a leading Pelagia/ folder or homebrew/Pelagia/, as in
#                     the release zip, is also accepted);
#                     remplace --build-dir
#   --ftp-port N      default 2121
#   --web-port N      default 8080
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build-ps5"
ZIP=""
CONF=""
LAUNCH=0
LOGS=0
FTP_PORT=2121
WEB_PORT=8080
DIR="/data/homebrew/Pelagia"

usage() { sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

HOST=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --conf) CONF="$2"; shift 2 ;;
        --launch) LAUNCH=1; shift ;;
        --logs) LOGS=1; shift ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --zip) ZIP="$2"; shift 2 ;;
        --ftp-port) FTP_PORT="$2"; shift 2 ;;
        --web-port) WEB_PORT="$2"; shift 2 ;;
        -h|--help) usage ;;
        -*) echo "unknown option: $1" >&2; usage ;;
        *) [[ -z "$HOST" ]] || usage; HOST="$1"; shift ;;
    esac
done
[[ -n "$HOST" ]] || usage
FTP="ftp://${HOST}:${FTP_PORT}"

if [[ $LOGS -eq 1 ]]; then
    curl -sS --fail "${FTP}${DIR}/logs/pelagia.log"
    exit 0
fi

PKG="$BUILD_DIR/Pelagia"
if [[ -n "$ZIP" ]]; then
    [[ -f "$ZIP" ]] || { echo "file not found: $ZIP" >&2; exit 1; }
    # CI artifact zip: extracted into a new temporary folder (untrusted
    # content: never executed, only copied over FTP).
    STAGE="$(mktemp -d)"
    trap 'rm -rf "$STAGE"' EXIT
    if command -v unzip >/dev/null 2>&1; then
        unzip -q "$ZIP" -d "$STAGE" || { echo "unreadable zip: $ZIP" >&2; exit 1; }
    elif command -v python3 >/dev/null 2>&1; then
        python3 -I -m zipfile -e "$ZIP" "$STAGE" || { echo "unreadable zip: $ZIP" >&2; exit 1; }
    else
        echo "--zip needs unzip or python3" >&2
        exit 1
    fi
    # eboot.elf at the root of the zip (CI artifact), in homebrew/Pelagia/ (release
    # zip, ready for a USB stick) or under a single folder.
    if [[ -f "$STAGE/eboot.elf" ]]; then
        PKG="$STAGE"
    elif [[ -f "$STAGE/homebrew/Pelagia/eboot.elf" ]]; then
        PKG="$STAGE/homebrew/Pelagia"
    else
        PKG=""
        for d in "$STAGE"/*/; do
            if [[ -f "${d}eboot.elf" && -z "$PKG" ]]; then PKG="${d%/}"; fi
        done
        [[ -n "$PKG" ]] || { echo "eboot.elf missing from $ZIP" >&2; exit 1; }
    fi
    [[ -f "$PKG/sce_sys/icon0.png" ]] || { echo "sce_sys/icon0.png missing from $ZIP" >&2; exit 1; }
fi
[[ -f "$PKG/eboot.elf" ]] || { echo "missing: $PKG/eboot.elf (run ci/build-ps5.sh)" >&2; exit 1; }
if [[ -n "$CONF" ]]; then
    [[ -f "$CONF" ]] || { echo "file not found: $CONF" >&2; exit 1; }
    # Safeguard: never credentials in this file (they would be ignored).
    if grep -Eiq '^[[:space:]]*(user|password|pass|token)[[:space:]]*=' "$CONF"; then
        echo "refused: $CONF contains a credential; only the server= key is allowed" >&2
        exit 1
    fi
fi

echo "[deploy] eboot.elf ($(stat -c %s "$PKG/eboot.elf") bytes) -> ${FTP}${DIR}/"
curl -sS --fail --ftp-create-dirs -T "$PKG/eboot.elf" "${FTP}${DIR}/eboot.elf"
curl -sS --fail --ftp-create-dirs -T "$PKG/sce_sys/icon0.png" "${FTP}${DIR}/sce_sys/icon0.png"
if [[ -n "$CONF" ]]; then
    echo "[deploy] $CONF -> ${DIR}/pelagia.conf"
    curl -sS --fail -T "$CONF" "${FTP}${DIR}/pelagia.conf"
fi
echo "[deploy] content of ${DIR}:"
curl -sS --fail --list-only "${FTP}${DIR}/" | sed 's/^/    /'

if [[ $LAUNCH -eq 0 ]]; then
    echo "[deploy] done: start the "Pelagia" tile of websrv, or rerun with --launch."
    exit 0
fi
echo "[deploy] launching through http://${HOST}:${WEB_PORT}/hbldr (application logs below)"
curl -sS --fail -N --get \
    --data-urlencode "path=${DIR}/eboot.elf" \
    --data-urlencode "cwd=${DIR}" \
    --data-urlencode "pipe=1" \
    "http://${HOST}:${WEB_PORT}/hbldr"
echo "[deploy] the application closed"
