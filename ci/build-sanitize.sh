#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 steredbits and Pelagia contributors
# Debug build with libstdc++ assertions (-D_GLIBCXX_ASSERTIONS),
# AddressSanitizer and UBSan, then the whole ctest suite. Used locally and by the
# "sanitizers" CI job. Any error (std::vector overrun, out-of-bounds access,
# undefined behavior, memory leak) makes the test fail.
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="${BUILD_DIR:-build-sanitize}"

# Leaks are detected, full stack trace on UBSan errors; each process stops at its
# first error.
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:detect_stack_use_after_return=1:strict_string_checks=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"

cmake -B "$BUILD_DIR" -DPLATFORM=linux -DCMAKE_BUILD_TYPE=Debug -DPELAGIA_SANITIZE=ON
cmake --build "$BUILD_DIR" -j
ctest --test-dir "$BUILD_DIR" --output-on-failure -j"$(nproc)"
