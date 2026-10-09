#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KevinJCode and Pelagia contributors
# Installs apt packages in CI, robust to Azure mirror outages
# (azure.archive.ubuntu.com silent for several minutes, seen on GitHub
# runners): switches to archive.ubuntu.com, short timeouts, 3 attempts.
# Usage : ci/apt-install.sh paquet...
set -euo pipefail

for f in /etc/apt/sources.list /etc/apt/sources.list.d/*.sources /etc/apt/sources.list.d/*.list; do
    [[ -f "$f" ]] || continue
    sudo sed -i 's#http://azure.archive.ubuntu.com/ubuntu#http://archive.ubuntu.com/ubuntu#g' "$f"
done
# Recent images: mirror list (URIs: mirror+file:...); Azure is removed
# if another mirror remains.
MIRRORS=/etc/apt/apt-mirrors.txt
if [[ -f "$MIRRORS" ]] && grep -qv azure.archive.ubuntu.com "$MIRRORS"; then
    sudo sed -i '/azure\.archive\.ubuntu\.com/d' "$MIRRORS"
fi

APT=(sudo apt-get -o Acquire::Retries=3 -o Acquire::http::Timeout=20 -o Acquire::https::Timeout=20)
for attempt in 1 2 3; do
    if "${APT[@]}" update && "${APT[@]}" install -y "$@"; then
        exit 0
    fi
    echo "apt: attempt $attempt failed, retrying in 10 s" >&2
    sleep 10
done
exit 1
