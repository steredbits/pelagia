#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KevinJCode and Pelagia contributors
# Test of ci/strip-ps5.sh on a small Linux executable (no PS5 SDK): the original is kept
# byte for byte as the debug copy, the payload loses its symbols and debug sections but
# keeps the same program headers and dynamic section, and still runs.
# Usage: test_strip_ps5.sh <strip-ps5.sh> <working directory>
set -euo pipefail

SCRIPT="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
mkdir -p "$(dirname "$2")"
WORK="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
rm -rf "$WORK"; mkdir -p "$WORK"
fail() { echo "FAIL: $*" >&2; exit 1; }

cat > "$WORK/hello.c" <<'EOF'
#include <stdio.h>
int main(void) { puts("ok"); return 0; }
EOF
cc -g -O0 -o "$WORK/eboot.elf" "$WORK/hello.c"
cp "$WORK/eboot.elf" "$WORK/original.elf"
readelf -SW "$WORK/original.elf" | grep -q '\.symtab' || fail "the test program has no symbols"
readelf -SW "$WORK/original.elf" | grep -q '\.debug_' || fail "the test program has no debug sections"

"$SCRIPT" "$WORK/eboot.elf" "$WORK/debug/pelagia-debug-symbols.elf" > "$WORK/out.txt" || fail "strip refused: $(cat "$WORK/out.txt")"
cmp "$WORK/original.elf" "$WORK/debug/pelagia-debug-symbols.elf" || fail "the debug copy is not the original"
[[ "$(stat -c %s "$WORK/eboot.elf")" -lt "$(stat -c %s "$WORK/original.elf")" ]] || fail "not smaller"
! readelf -SW "$WORK/eboot.elf" | grep -qE '\.symtab|\.debug_' || fail "symbols left"
[[ "$("$WORK/eboot.elf")" == ok ]] || fail "the stripped program no longer runs"
cmp <(readelf -dW "$WORK/original.elf") <(readelf -dW "$WORK/eboot.elf") || fail "dynamic section changed"

# Missing input and missing tool: refused.
if "$SCRIPT" "$WORK/absent.elf" "$WORK/x.elf" 2> /dev/null; then fail "missing payload accepted"; fi
if STRIP=/nonexistent/strip "$SCRIPT" "$WORK/eboot.elf" "$WORK/y.elf" 2> /dev/null; then fail "missing tool accepted"; fi
echo "test_strip_ps5 : OK"
