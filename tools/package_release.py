#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 steredbits and Pelagia contributors
"""Builds the Pelagia release zip, ready to unzip at the root of a USB stick.

Contenu :
  INSTALL.txt                         installation (EN/FR), at the root of the zip
  homebrew/Pelagia/eboot.elf          l'application
  homebrew/Pelagia/sce_sys/icon0.png  icon of the websrv launcher tile
  homebrew/Pelagia/pelagia.conf.example
  homebrew/Pelagia/LICENSE, THIRD_PARTY_NOTICES, licenses/*

Usage : python3 tools/package_release.py --version 0.1.0 --pkg-dir build-ps5/Pelagia
            [--out dist] [--repo-url https://github.com/steredbits/pelagia]
            [--debug-elf build-ps5/debug/pelagia-debug-symbols.elf]
Writes <out>/Pelagia-v<version>-ps5.zip and <out>/SHA256SUMS; prints the zip path.
With --debug-elf (the unstripped eboot.elf, see ci/strip-ps5.sh), the file is also copied to
<out>/pelagia-debug-symbols.elf, listed in SHA256SUMS and NOT put in the zip: it is a separate
release asset, only useful to analyze a crash.
The zip is reproducible (fixed dates: SOURCE_DATE_EPOCH, otherwise 2026-01-01).
"""

import argparse
import hashlib
import os
import sys
import time
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = "Pelagia"
DEBUG_NAME = "pelagia-debug-symbols.elf"
DEFAULT_REPO = "https://github.com/steredbits/pelagia"

# (path in the repository, path under homebrew/Pelagia/)
EXTRA_FILES = [
    ("LICENSE", "LICENSE"),
    ("THIRD_PARTY_NOTICES", "THIRD_PARTY_NOTICES"),
    ("licenses/SDL2-zlib.txt", "licenses/SDL2-zlib.txt"),
    ("assets/fonts/OFL.txt", "licenses/NotoSans-OFL.txt"),
    ("third_party/cjson/LICENSE", "licenses/cJSON-MIT.txt"),
]
# (path in pkg-dir, path under homebrew/Pelagia/): mandatory
PKG_FILES = [
    ("eboot.elf", "eboot.elf"),
    ("sce_sys/icon0.png", "sce_sys/icon0.png"),
    ("pelagia.conf.example", "pelagia.conf.example"),
]


def read(path):
    with open(path, "rb") as f:
        return f.read()


def build(version, pkg_dir, out_dir, repo_url, debug_elf=None):
    entries = []  # (name in the zip, content)
    template = read(os.path.join(ROOT, "pkg", "INSTALL.txt.in")).decode("utf-8")
    install = template.replace("@VERSION@", version).replace("@REPO_URL@", repo_url)
    entries.append(("INSTALL.txt", install.encode("utf-8")))
    for src, dst in PKG_FILES:
        path = os.path.join(pkg_dir, src)
        if not os.path.isfile(path):
            sys.exit(f"file missing from {pkg_dir}: {src}")
        entries.append((f"homebrew/{APP}/{dst}", read(path)))
    for src, dst in EXTRA_FILES:
        entries.append((f"homebrew/{APP}/{dst}", read(os.path.join(ROOT, src))))

    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "1767225600"))  # 2026-01-01
    stamp = time.gmtime(epoch)[:6]
    os.makedirs(out_dir, exist_ok=True)
    zip_path = os.path.join(out_dir, f"{APP}-v{version}-ps5.zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in entries:
            info = zipfile.ZipInfo(name, date_time=stamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)
    sums = [(hashlib.sha256(read(zip_path)).hexdigest(), os.path.basename(zip_path))]
    if debug_elf:
        if not os.path.isfile(debug_elf):
            sys.exit(f"debug file missing: {debug_elf}")
        data = read(debug_elf)
        with open(os.path.join(out_dir, DEBUG_NAME), "wb") as f:
            f.write(data)
        sums.append((hashlib.sha256(data).hexdigest(), DEBUG_NAME))
    with open(os.path.join(out_dir, "SHA256SUMS"), "w", encoding="utf-8") as f:
        for digest, name in sums:
            f.write(f"{digest}  {name}\n")
    return zip_path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--version", required=True, help="version without the leading v (0.1.0, 0.1.0-rc1)")
    ap.add_argument("--pkg-dir", required=True, help="folder produced by ci/build-ps5.sh")
    ap.add_argument("--out", default="dist")
    ap.add_argument("--repo-url", default=DEFAULT_REPO)
    ap.add_argument("--debug-elf", help="unstripped eboot.elf, published next to the zip (not inside)")
    args = ap.parse_args()
    print(build(args.version, args.pkg_dir, args.out, args.repo_url, args.debug_elf))


if __name__ == "__main__":
    main()
