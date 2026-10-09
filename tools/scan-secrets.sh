#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KevinJCode and Pelagia contributors
# Secret scan with gitleaks: working tree, then Git history if there is one.
# Usage: tools/scan-secrets.sh [folder]   (default: repository root)
# Exit code: 0 nothing found, 1 secret(s) found, 2 gitleaks not found.
#
# gitleaks: https://github.com/gitleaks/gitleaks (version used: v8.30.1; >= 8.25 is required by
# the allowlist conditions of .gitleaks.toml)
# (the Go module path is still github.com/zricethezav/gitleaks/v8, although the project moved)
#   go install github.com/zricethezav/gitleaks/v8@v8.30.1
set -euo pipefail

DIR="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
GITLEAKS="$(command -v gitleaks || true)"
[[ -z "$GITLEAKS" && -x "$HOME/go/bin/gitleaks" ]] && GITLEAKS="$HOME/go/bin/gitleaks"
if [[ -z "$GITLEAKS" ]]; then
    echo "gitleaks not found (go install github.com/zricethezav/gitleaks/v8@v8.30.1)" >&2
    exit 2
fi

CONFIG="$DIR/.gitleaks.toml"
ARGS=(--no-banner --redact)
[[ -f "$CONFIG" ]] && ARGS+=(--config "$CONFIG")

status=0
echo "[gitleaks] working tree: $DIR"
# From inside the folder: findings carry paths relative to the root, which is what the
# `paths` of the allowlists in .gitleaks.toml (^tests/) are matched against.
(cd "$DIR" && "$GITLEAKS" dir "${ARGS[@]}" .) || status=1
if [[ -d "$DIR/.git" ]]; then
    echo "[gitleaks] Git history: $(git -C "$DIR" rev-list --count HEAD) commit(s)"
    "$GITLEAKS" git "${ARGS[@]}" "$DIR" || status=1
fi
[[ $status -eq 0 ]] && echo "[gitleaks] no secret detected"
exit $status
