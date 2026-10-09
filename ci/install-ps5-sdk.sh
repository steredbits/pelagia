#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 steredbits and Pelagia contributors
# Installs the ps5-payload-dev SDK and the prebuilt PacBrew libraries.
#
# The pacbrew-repo release archive contains a complete /opt/ps5-payload-sdk:
# SDK (prospero toolchain, crt, sprx stubs), libc++, SDL2, ffmpeg, curl,
# openssl, freetype... Nothing to compile.
#
# Usage: ci/install-ps5-sdk.sh [destination directory]
#   default: /opt (the SDK ends up in /opt/ps5-payload-sdk; sudo if needed)
#   PACBREW_ARCHIVE=<file>: reuses an already downloaded archive (CI cache).
#
# Fedora requirements: sudo dnf install bash llvm-devel clang lld wget socat cmake \
#                      pkg-config python3
# Ubuntu requirements: sudo apt-get install clang-18 lld-18 socat cmake pkg-config
set -euo pipefail

PACBREW_VERSION="v0.40.2"
PACBREW_SHA256="a85f65de418a8e6a898c6c3e3c870d50fff7618a200e4dd59ea9692af6ecec4d"
PACBREW_URL="https://github.com/ps5-payload-dev/pacbrew-repo/releases/download/${PACBREW_VERSION}/ps5-payload-dev.tar.gz"

DEST="${1:-/opt}"
ARCHIVE="${PACBREW_ARCHIVE:-}"

if [[ -z "${ARCHIVE}" ]]; then
    ARCHIVE="$(mktemp -d)/ps5-payload-dev.tar.gz"
fi
if [[ ! -f "${ARCHIVE}" ]]; then
    echo "Downloading PacBrew ${PACBREW_VERSION} (~350 MB)..."
    curl -fL --retry 4 -o "${ARCHIVE}" "${PACBREW_URL}"
fi

echo "${PACBREW_SHA256}  ${ARCHIVE}" | sha256sum -c -

SUDO=""
if [[ ! -w "${DEST}" ]]; then
    SUDO="sudo"
fi
# The archive contains opt/ps5-payload-sdk/...: the "opt/" prefix is stripped.
${SUDO} mkdir -p "${DEST}"
${SUDO} tar -xzf "${ARCHIVE}" -C "${DEST}" --strip-components=1

SDK="${DEST}/ps5-payload-sdk"
echo "${PACBREW_VERSION}" | ${SUDO} tee "${SDK}/.pacbrew-version" >/dev/null
echo "SDK installed in ${SDK} (host clang: $("${SDK}/bin/prospero-llvm-config" --version))"
echo "Add to your shell: export PS5_PAYLOAD_SDK=${SDK}"
