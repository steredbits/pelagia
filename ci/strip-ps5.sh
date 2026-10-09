#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 steredbits and Pelagia contributors
# Strips a PS5 payload in place and keeps the unstripped original as a separate file
# (symbols and debug information, to analyze a crash).
#
# Usage : ci/strip-ps5.sh <payload.elf> <unstripped-copy.elf>
#   STRIP=<tool>   stripping tool (default: llvm-strip-18, llvm-strip, the SDK's
#                  prospero-strip, then strip)
#
# Everything the loader uses (program headers, dynamic section) is checked to be the
# same before and after: only the symbol table and the debug sections go away.
set -euo pipefail

ELF="${1:?payload.elf}"
DEBUG="${2:?unstripped-copy.elf}"
[[ -f "${ELF}" ]] || { echo "not found: ${ELF}" >&2; exit 1; }

TOOL="${STRIP:-}"
if [[ -z "${TOOL}" ]]; then
    for candidate in llvm-strip-18 llvm-strip "${PS5_PAYLOAD_SDK:+${PS5_PAYLOAD_SDK}/bin/prospero-strip}" strip; do
        [[ -n "${candidate}" ]] || continue
        if command -v "${candidate}" >/dev/null 2>&1; then TOOL="${candidate}"; break; fi
    done
fi
[[ -n "${TOOL}" ]] || { echo "no strip tool found (llvm-strip, strip)" >&2; exit 1; }

# What the loader reads, without the file offsets (they may legitimately be repacked).
loader_view() {
    readelf -lW "$1" | awk '/^ +[A-Z][A-Z_0-9]+ +0x/ { $2 = ""; print }'
    readelf -dW "$1"
}

mkdir -p "$(dirname "${DEBUG}")"
cp -f -- "${ELF}" "${DEBUG}"
BEFORE="$(loader_view "${ELF}")"
SIZE_BEFORE="$(stat -c %s "${ELF}")"

"${TOOL}" --strip-all "${ELF}"

AFTER="$(loader_view "${ELF}")"
if [[ "${BEFORE}" != "${AFTER}" ]]; then
    echo "$(basename "${ELF}"): program headers or dynamic section changed by ${TOOL}" >&2
    diff <(echo "${BEFORE}") <(echo "${AFTER}") >&2 || true
    exit 1
fi
if readelf -SW "${ELF}" | grep -qE '\.(symtab|debug_[a-z_]+)\b'; then
    echo "$(basename "${ELF}"): symbols or debug sections are still present" >&2
    exit 1
fi
echo "$(basename "${ELF}"): stripped with ${TOOL}, ${SIZE_BEFORE} -> $(stat -c %s "${ELF}") bytes;" \
     "unstripped copy: ${DEBUG}"
