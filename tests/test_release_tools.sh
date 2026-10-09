#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KevinJCode and Pelagia contributors
# Release tools: version/tag (check-version.sh), notes (changelog_section.py)
# and zip ready for a USB stick (package_release.py), no network.
# Usage: test_release_tools.sh <repository root> <working directory>
set -euo pipefail

ROOT="$1"
WORK="$2"
rm -rf "$WORK"
mkdir -p "$WORK/pkg/sce_sys"
fail() { echo "FAIL: $*" >&2; exit 1; }

# --- version and tag -------------------------------------------------------------
VERSION="$("$ROOT/tools/check-version.sh")"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "unreadable version: $VERSION"
"$ROOT/tools/check-version.sh" "v$VERSION" > /dev/null || fail "tag v$VERSION refused"
"$ROOT/tools/check-version.sh" "v$VERSION-rc1" > /dev/null || fail "pre-release tag refused"
if "$ROOT/tools/check-version.sh" "v9.9.9" > /dev/null 2>&1; then fail "wrong tag accepted"; fi
if "$ROOT/tools/check-version.sh" "$VERSION" > /dev/null 2>&1; then fail "tag without \"v\" accepted"; fi

# The CMake version is the one the application displays (file generated at configuration).
if [[ -f "$ROOT/build-linux/generated/util/version.h" ]]; then
    grep -q "kVersion\[\] = \"$VERSION\"" "$ROOT/build-linux/generated/util/version.h" ||
        fail "util/version.h does not follow the CMake version"
fi

# --- release notes -----------------------------------------------------------
NOTES="$(python3 -I "$ROOT/tools/changelog_section.py" "$VERSION")"
[[ -n "$NOTES" ]] || fail "empty notes"
grep -q '^### Added' <<<"$NOTES" || fail "notes: Added section missing"
grep -q '^## \[' <<<"$NOTES" && fail "notes: overflow into another version"
if python3 -I "$ROOT/tools/changelog_section.py" "9.9.9" > /dev/null 2>&1; then fail "missing section accepted"; fi

# --- zip ------------------------------------------------------------------------
printf 'ELF' > "$WORK/pkg/eboot.elf"
printf 'PNG' > "$WORK/pkg/sce_sys/icon0.png"
printf 'server=\n' > "$WORK/pkg/pelagia.conf.example"
ZIP="$(python3 -I "$ROOT/tools/package_release.py" --version "$VERSION" --pkg-dir "$WORK/pkg" \
        --out "$WORK/dist" --repo-url https://example.org/pelagia)"
[[ "$ZIP" == "$WORK/dist/Pelagia-v$VERSION-ps5.zip" ]] || fail "zip name: $ZIP"
(cd "$WORK/dist" && sha256sum -c SHA256SUMS > /dev/null) || fail "invalid SHA256SUMS"

python3 -I - "$ZIP" "$VERSION" <<'PY' || exit 1
import sys, zipfile
path, version = sys.argv[1], sys.argv[2]
z = zipfile.ZipFile(path)
names = set(z.namelist())
need = {
    "INSTALL.txt",
    "homebrew/Pelagia/eboot.elf",
    "homebrew/Pelagia/sce_sys/icon0.png",
    "homebrew/Pelagia/pelagia.conf.example",
    "homebrew/Pelagia/LICENSE",
    "homebrew/Pelagia/THIRD_PARTY_NOTICES",
}
missing = need - names
assert not missing, f"missing from the zip: {sorted(missing)}"
# Everything is in INSTALL.txt (root) or under homebrew/Pelagia/: unzip at the root of a stick.
stray = [n for n in names if n != "INSTALL.txt" and not n.startswith("homebrew/Pelagia/")]
assert not stray, f"files outside homebrew/Pelagia/: {stray}"
text = z.read("INSTALL.txt").decode("ascii")
assert "@VERSION@" not in text and "@REPO_URL@" not in text, "template not filled in"
assert f"Pelagia {version} - unofficial Jellyfin client for PS5" in text
assert "client Jellyfin non officiel pour PS5" in text
assert "https://example.org/pelagia" in text
assert "ENGLISH" in text and "FRANCAIS" in text
assert "Not affiliated with the Jellyfin project or Sony" in text
PY

# Reproducible: same content, same hash.
python3 -I "$ROOT/tools/package_release.py" --version "$VERSION" --pkg-dir "$WORK/pkg" \
    --out "$WORK/dist2" --repo-url https://example.org/pelagia > /dev/null
cmp "$WORK/dist/SHA256SUMS" "$WORK/dist2/SHA256SUMS" || fail "zip not reproducible"

# Debug symbols: the unstripped eboot.elf is a separate asset, listed in SHA256SUMS, never in the zip.
printf 'UNSTRIPPED-ELF' > "$WORK/debug.elf"
ZIP4="$(python3 -I "$ROOT/tools/package_release.py" --version "$VERSION" --pkg-dir "$WORK/pkg" \
        --out "$WORK/dist4" --repo-url https://example.org/pelagia --debug-elf "$WORK/debug.elf")"
cmp "$WORK/debug.elf" "$WORK/dist4/pelagia-debug-symbols.elf" || fail "debug asset not copied as is"
(cd "$WORK/dist4" && sha256sum -c SHA256SUMS > /dev/null) || fail "SHA256SUMS with debug symbols invalid"
[[ "$(wc -l < "$WORK/dist4/SHA256SUMS")" == 2 ]] || fail "SHA256SUMS must list the zip and the debug file"
grep -q "  pelagia-debug-symbols.elf$" "$WORK/dist4/SHA256SUMS" || fail "debug file not listed"
python3 -I - "$ZIP4" <<'PY' || exit 1
import sys, zipfile
names = zipfile.ZipFile(sys.argv[1]).namelist()
assert not any("debug" in n for n in names), f"debug symbols inside the zip: {names}"
PY
cmp "$ZIP" "$ZIP4" || fail "the zip must not depend on the debug file"
if python3 -I "$ROOT/tools/package_release.py" --version "$VERSION" --pkg-dir "$WORK/pkg" \
        --out "$WORK/dist5" --debug-elf "$WORK/absent.elf" > /dev/null 2>&1; then fail "missing debug file accepted"; fi

# Missing mandatory file: refused.
rm "$WORK/pkg/sce_sys/icon0.png"
if python3 -I "$ROOT/tools/package_release.py" --version "$VERSION" --pkg-dir "$WORK/pkg" \
        --out "$WORK/dist3" > /dev/null 2>&1; then fail "incomplete pkg accepted"; fi
echo "test_release_tools : OK"
