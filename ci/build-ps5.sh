#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 steredbits and Pelagia contributors
# PS5 cross-compilation of the application - locally and in CI.
# Produit $BUILD_DIR/Pelagia/ (eboot.elf, sce_sys/icon0.png, pelagia.conf.example),
# to deploy to /data/homebrew/Pelagia (tools/deploy-ps5.sh). eboot.elf is stripped (smaller);
# the unstripped original, for analyzing a crash, is $BUILD_DIR/debug/pelagia-debug-symbols.elf
# (PELAGIA_NO_STRIP=1 keeps eboot.elf unstripped and writes no debug copy).
# Requirements: PacBrew SDK installed (ci/install-ps5-sdk.sh), PS5_PAYLOAD_SDK set.
set -euo pipefail

cd "$(dirname "$0")/.."
: "${PS5_PAYLOAD_SDK:?PS5_PAYLOAD_SDK is not set (see ci/install-ps5-sdk.sh)}"

BUILD_DIR="${BUILD_DIR:-build-ps5}"
cmake -B "$BUILD_DIR" -DPLATFORM=ps5 -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake" \
    -DCMAKE_VERBOSE_MAKEFILE=OFF
cmake --build "$BUILD_DIR" -j
# Symbols are checked on the unstripped file (a stripped one has no symbol table).
ci/check-ps5-symbols.sh "$BUILD_DIR/Pelagia/eboot.elf"
if [[ "${PELAGIA_NO_STRIP:-0}" != 1 ]]; then
    ci/strip-ps5.sh "$BUILD_DIR/Pelagia/eboot.elf" "$BUILD_DIR/debug/pelagia-debug-symbols.elf"
fi
find "$BUILD_DIR/Pelagia" -type f -exec ls -l {} \;
