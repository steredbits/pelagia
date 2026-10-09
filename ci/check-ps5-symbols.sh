#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KevinJCode and Pelagia contributors
# Checks that every undefined symbol of a payload is exported by an SDK
# sprx stub. A missing symbol would make loading fail on the
# console: better to see it here.
#
# Usage : ci/check-ps5-symbols.sh <payload.elf>
set -euo pipefail

ELF="$1"
SDK="${PS5_PAYLOAD_SDK:?PS5_PAYLOAD_SDK is not set}"
NM="${SDK}/bin/prospero-nm"
[[ -x "${NM}" ]] || NM=nm

# Libraries actually requested by the ELF (DT_NEEDED), SDK .so stubs.
mapfile -t NEEDED < <(readelf -d "${ELF}" | sed -n 's/.*Shared library: \[\(.*\)\.sprx\]/\1/p')
EXPORTS="$(mktemp)"
trap 'rm -f "${EXPORTS}"' EXIT
for lib in "${NEEDED[@]}"; do
    stub="${SDK}/target/lib/${lib}.so"
    [[ -f "${stub}" ]] || { echo "stub missing: ${stub}" >&2; exit 1; }
    "${NM}" -D --defined-only "${stub}" | awk '{print $NF}' >> "${EXPORTS}"
done
sort -u -o "${EXPORTS}" "${EXPORTS}"

MISSING="$("${NM}" "${ELF}" | awk '$1=="U"{print $2}' | sort -u | comm -23 - "${EXPORTS}")"
echo "$(basename "${ELF}"): libraries ${NEEDED[*]}"
if [[ -n "${MISSING}" ]]; then
    echo "Unresolved symbols:" >&2
    echo "${MISSING}" >&2
    exit 1
fi
echo "$(basename "${ELF}"): all symbols are resolved by the SDK stubs"
