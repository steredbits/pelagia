#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KevinJCode and Pelagia contributors
"""Generates pkg/icon0.png (512x512), the websrv tile icon of Pelagia.

Midnight-blue background, stylized jellyfish (original shape, unrelated to the
Jellyfin logo) filled with the Jellyfin color gradient, application name in
Noto Sans Bold (assets/fonts, OFL). Standard Python for the shape, ffmpeg
(drawtext) for the text.
Usage: python3 tools/make_icon.py [output]   (default: pkg/icon0.png)
"""

import math
import os
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZE = 512
BG = (16, 24, 48)
ACCENT = (0, 164, 220)
PURPLE = (170, 92, 195)
WHITE = (240, 240, 250)


def gradient(x, y):
    # Diagonal purple -> blue gradient (colors of the Jellyfin gradient, whose
    # brand rules allow its use; the shape below is original).
    t = min(max(((x - 120) + (y - 40)) / 420.0, 0.0), 1.0)
    return tuple(int(round(PURPLE[i] + (ACCENT[i] - PURPLE[i]) * t)) for i in range(3))


def in_jellyfish(x, y):
    # Bell: half ellipse, scalloped lower edge; five wavy tentacles.
    dx = x - 256
    if y <= 190 and (dx / 132.0) ** 2 + ((y - 190) / 112.0) ** 2 <= 1.0:
        return True
    if 190 < y <= 214 + 7 * math.cos(dx / 132.0 * math.pi * 4) and abs(dx) <= 132 - (y - 190) * 0.6:
        return True
    if 214 <= y <= 350:
        along = (y - 214) / 136.0
        half = 7.0 - 4.0 * along
        for i in range(5):
            cx = 256 - 88 + 44 * i + 11 * math.sin(along * 9.0 + i * 1.3)
            if abs(x - cx) <= half:
                return True
    return False


def pixel(x, y):
    # 3x3 supersampling: smooth edges.
    hits = 0
    for sy in range(3):
        for sx in range(3):
            if in_jellyfish(x + (sx + 0.5) / 3, y + (sy + 0.5) / 3):
                hits += 1
    if hits == 0:
        return BG
    fg = gradient(x, y)
    k = hits / 9.0
    return tuple(int(round(BG[i] + (fg[i] - BG[i]) * k)) for i in range(3))


def write_png(path):
    rows = []
    for y in range(SIZE):
        row = bytearray([0])
        for x in range(SIZE):
            row += bytes(pixel(x, y))
        rows.append(bytes(row))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "pkg", "icon0.png")
    font = os.path.join(ROOT, "assets", "fonts", "NotoSans-Bold.ttf")
    with tempfile.TemporaryDirectory() as tmp:
        base = os.path.join(tmp, "base.png")
        write_png(base)
        text = f"drawtext=fontfile={font}:text=Pelagia:fontsize=84:fontcolor=0xF0F0FA:x=(w-text_w)/2:y=380"
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", base, "-vf", text,
                        "-pix_fmt", "rgb24", out], check=True)
    print(out)


if __name__ == "__main__":
    main()
