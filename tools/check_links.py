#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KevinJCode and Pelagia contributors
"""Checks that the relative links of Markdown files point to existing files.

Usage: python3 tools/check_links.py <folder> [--exclude-file <list>] [--overlay-dir <folder>]
  http(s) and mailto links and bare anchors are ignored. With --exclude-file (one pattern
  per line: folder/ or file or glob), the files it lists are left out of the published tree: they
  are not checked, and a link from another file to one of them is an error, unless
  --overlay-dir contains a replacement for it (same relative path).
Exit code: 0 everything valid, 1 broken link.
"""

import fnmatch
import os
import re
import sys

LINK = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")


def load_excludes(path):
    patterns = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                patterns.append(line)
    return patterns


def excluded(rel, patterns):
    for p in patterns:
        if p.endswith("/"):
            if rel.startswith(p):
                return True
        elif fnmatch.fnmatch(rel, p):
            return True
    return False


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    root = os.path.abspath(args[0])
    patterns = []
    if "--exclude-file" in args:
        patterns = load_excludes(args[args.index("--exclude-file") + 1])
    overlay = os.path.abspath(args[args.index("--overlay-dir") + 1]) if "--overlay-dir" in args else None
    bad = []
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in (".git", "third_party") and not d.startswith("build")]
        for name in files:
            if not name.endswith(".md"):
                continue
            path = os.path.join(base, name)
            rel = os.path.relpath(path, root)
            if excluded(rel, patterns):
                continue
            with open(path, encoding="utf-8") as f:
                text = f.read()
            for target in LINK.findall(text):
                if re.match(r"^[a-z][a-z0-9+.-]*:", target) or target.startswith("#"):
                    continue
                target = target.split("#", 1)[0]
                if not target:
                    continue
                dest = os.path.normpath(os.path.join(base, target))
                rel_dest = os.path.relpath(dest, root)
                if not os.path.exists(dest):
                    bad.append(f"{rel}: broken link to {target}")
                elif excluded(rel_dest, patterns) and not (
                        overlay and os.path.exists(os.path.join(overlay, rel_dest))):
                    bad.append(f"{rel}: link to {rel_dest}, a file that is not published")
    for b in bad:
        print(b, file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
