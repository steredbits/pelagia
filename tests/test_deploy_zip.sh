#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 steredbits and Pelagia contributors
# Test of tools/deploy-ps5.sh --zip with a simulated curl (no network):
# the CI artifact zip (files at the root) is accepted as is, like
# the one with a leading Pelagia/ folder; a zip without eboot.elf is refused.
# Usage: test_deploy_zip.sh <deploy-ps5.sh> <working directory>
set -euo pipefail

DEPLOY="$1"
WORK="$2"
rm -rf "$WORK"
mkdir -p "$WORK/bin" "$WORK/pkg/sce_sys" "$WORK/other"
printf 'ELF' > "$WORK/pkg/eboot.elf"
printf 'PNG' > "$WORK/pkg/sce_sys/icon0.png"
echo 'server=' > "$WORK/pkg/pelagia.conf.example"
echo x > "$WORK/other/readme.txt"

python3 -I - "$WORK" <<'PY'
import os, sys, zipfile
work = sys.argv[1]
def make(name, root, prefix=''):
    with zipfile.ZipFile(os.path.join(work, name), 'w') as z:
        for d, _, files in os.walk(os.path.join(work, root)):
            for f in files:
                p = os.path.join(d, f)
                z.write(p, os.path.join(prefix, os.path.relpath(p, os.path.join(work, root))))
make('flat.zip', 'pkg')
make('nested.zip', 'pkg', 'Pelagia')
make('release.zip', 'pkg', 'homebrew/Pelagia')
make('bad.zip', 'other')
PY

cat > "$WORK/bin/curl" <<'SH'
#!/usr/bin/env bash
echo "$*" >> "$CURL_LOG"
exit 0
SH
chmod +x "$WORK/bin/curl"

for name in flat nested release; do
    export CURL_LOG="$WORK/$name.log"
    : > "$CURL_LOG"
    PATH="$WORK/bin:$PATH" "$DEPLOY" --zip "$WORK/$name.zip" 192.0.2.1 > "$WORK/$name.out"
    grep -q -- '-T .*/eboot.elf ftp://192.0.2.1:2121/data/homebrew/Pelagia/eboot.elf' "$CURL_LOG" ||
        { echo "$name.zip : eboot.elf not sent" >&2; cat "$CURL_LOG" >&2; exit 1; }
    grep -q -- '-T .*/sce_sys/icon0.png ftp://192.0.2.1:2121/data/homebrew/Pelagia/sce_sys/icon0.png' "$CURL_LOG" ||
        { echo "$name.zip : icon not sent" >&2; exit 1; }
    if grep -q 'conf.example' "$CURL_LOG"; then
        echo "$name.zip : pelagia.conf.example must not be sent" >&2
        exit 1
    fi
done

export CURL_LOG="$WORK/bad.log"
: > "$CURL_LOG"
if PATH="$WORK/bin:$PATH" "$DEPLOY" --zip "$WORK/bad.zip" 192.0.2.1 > "$WORK/bad.out" 2>&1; then
    echo "bad.zip : should have been refused" >&2
    exit 1
fi
grep -q 'eboot.elf missing' "$WORK/bad.out" || { echo "bad.zip : expected message missing" >&2; exit 1; }
[[ ! -s "$CURL_LOG" ]] || { echo "bad.zip : nothing must be sent" >&2; exit 1; }
if PATH="$WORK/bin:$PATH" "$DEPLOY" --zip "$WORK/absent.zip" 192.0.2.1 > /dev/null 2>&1; then
    echo "zip not found: should have failed" >&2
    exit 1
fi
echo "deploy --zip : OK"
