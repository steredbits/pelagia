#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KevinJCode and Pelagia contributors
"""Prints the text of the "## [X.Y.Z]" section of CHANGELOG.md (release notes).

Usage : python3 tools/changelog_section.py 0.1.0 [CHANGELOG.md]
Exit code 1 if the section is missing or empty.
"""

import os
import re
import sys


def section(text, version):
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if re.match(r"^## \[" + re.escape(version) + r"\]", line):
            start = i + 1
            break
    if start is None:
        return None
    body = []
    for line in lines[start:]:
        if line.startswith("## ["):
            break
        body.append(line)
    return "\n".join(body).strip()


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "CHANGELOG.md")
    with open(path, encoding="utf-8") as f:
        body = section(f.read(), sys.argv[1])
    if not body:
        print(f"section [{sys.argv[1]}] missing or empty in {path}", file=sys.stderr)
        return 1
    print(body)
    return 0


if __name__ == "__main__":
    sys.exit(main())
