#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KevinJCode and Pelagia contributors
# Linux build and tests - used locally and by the GitHub Actions CI.
set -euo pipefail

cd "$(dirname "$0")/.."

cmake -B build-linux -DPLATFORM=linux
cmake --build build-linux -j
ctest --test-dir build-linux --output-on-failure
