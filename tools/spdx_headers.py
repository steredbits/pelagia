#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KevinJCode and Pelagia contributors
"""Adds or checks the license header (SPDX) of the Pelagia sources.

Usage: python3 tools/spdx_headers.py --check   (code 1 if a file lacks the exact header)
        python3 tools/spdx_headers.py --apply   (adds the missing header, or replaces another
                                                 license identifier)

Files concerned: C/C++ sources (core, platform, tests), shell and
Python (ci, tools, tests), CMake. Excluded: third_party and assets (their own
licenses), docs, and any folder that contains a tracked file named .spdx-skip.
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LICENSE_ID = "GPL-3.0-or-later"
COPYRIGHT = "Copyright (C) 2026 KevinJCode and Pelagia contributors"

EXCLUDED = ("third_party/", "assets/", "docs/", "build")
SKIP_MARKER = ".spdx-skip"
SLASH_EXT = (".cpp", ".h")
HASH_EXT = (".sh", ".py", ".cmake")
HASH_NAMES = ("CMakeLists.txt",)


def tracked_files():
    out = subprocess.run(["git", "ls-files"], cwd=ROOT, check=True, capture_output=True, text=True)
    paths = out.stdout.splitlines()
    skipped = tuple(os.path.dirname(p) + "/" for p in paths if os.path.basename(p) == SKIP_MARKER)
    return [p for p in paths if not p.startswith(EXCLUDED + skipped)]


def comment_prefix(path):
    base = os.path.basename(path)
    if path.endswith(SLASH_EXT):
        return "//"
    if path.endswith(HASH_EXT) or base in HASH_NAMES:
        return "#"
    return None


def header(prefix):
    return f"{prefix} SPDX-License-Identifier: {LICENSE_ID}\n{prefix} {COPYRIGHT}\n"


def has_header(text):
    """True if the header carries exactly the LICENSE_ID identifier."""
    head = "".join(text.splitlines(True)[:4])
    return f"SPDX-License-Identifier: {LICENSE_ID}\n" in head


def apply_header(path, prefix):
    full = os.path.join(ROOT, path)
    with open(full, encoding="utf-8") as f:
        text = f.read()
    if has_header(text):
        return False
    lines = text.splitlines(True)
    # Old identifier (another license): replaced in place, the copyright stays.
    for i, line in enumerate(lines[:4]):
        if "SPDX-License-Identifier:" in line:
            lines[i] = f"{prefix} SPDX-License-Identifier: {LICENSE_ID}\n"
            with open(full, "w", encoding="utf-8") as f:
                f.write("".join(lines))
            return True
    if lines and lines[0].startswith("#!"):
        new = lines[0] + header(prefix) + "".join(lines[1:])
    else:
        new = header(prefix) + text
    with open(full, "w", encoding="utf-8") as f:
        f.write(new)
    return True


def main():
    mode = sys.argv[1] if len(sys.argv) == 2 else ""
    if mode not in ("--check", "--apply"):
        print(__doc__)
        return 2
    missing = []
    for path in tracked_files():
        prefix = comment_prefix(path)
        if prefix is None:
            continue
        if mode == "--apply":
            if apply_header(path, prefix):
                missing.append(path)
        else:
            with open(os.path.join(ROOT, path), encoding="utf-8") as f:
                if not has_header(f.read()):
                    missing.append(path)
    if mode == "--check":
        for p in missing:
            print(f"missing SPDX header: {p}", file=sys.stderr)
        return 1 if missing else 0
    print(f"{len(missing)} file(s) modified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
