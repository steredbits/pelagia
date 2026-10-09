#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 steredbits and Pelagia contributors
# Project version number (project(pelagia VERSION x.y.z) of the root CMakeLists).
# Usage: tools/check-version.sh            prints the version
#         tools/check-version.sh vX.Y.Z[-suffixe]
#                                        fails if the tag does not match the
#                                        CMake version or if CHANGELOG.md has no
#                                        "## [X.Y.Z]" section (used by the release)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(sed -n 's/^project(pelagia VERSION \([0-9][0-9.]*\) .*/\1/p' "$ROOT/CMakeLists.txt" | head -n1)"
[[ -n "$VERSION" ]] || { echo "version not found in CMakeLists.txt" >&2; exit 1; }

TAG="${1:-}"
if [[ -z "$TAG" ]]; then
    echo "$VERSION"
    exit 0
fi

# A pre-release suffix (-rc1, -beta2) is allowed on the tag, not on the CMake version.
BASE="${TAG#v}"; BASE="${BASE%%-*}"
if [[ "$TAG" != v* || "$BASE" != "$VERSION" ]]; then
    echo "tag '$TAG' does not match the CMake version 'v$VERSION'" >&2
    exit 1
fi
grep -q "^## \[$VERSION\]" "$ROOT/CHANGELOG.md" ||
    { echo "CHANGELOG.md: section '## [$VERSION]' missing" >&2; exit 1; }
echo "$VERSION"
