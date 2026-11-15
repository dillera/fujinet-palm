#!/usr/bin/env python3
"""Writes the 32x22 1-bit launcher icon, "H2H" over two arrows meeting, as a
BMP for PilRC (the writer is fujinet-palm's tools/make_icon.py)."""
import struct
import sys

H = ["##..##", "##..##", "##..##", "######", "##..##", "##..##", "##..##"]
TWO = [".####.", "##..##", "....##", "...##.", "..##..", ".##...", "######"]


def draw():
    grid = [["."] * 32 for _ in range(22)]
    for x0, glyph in ((5, H), (13, TWO), (21, H)):
        for y, row in enumerate(glyph):
            for x, c in enumerate(row):
                if c == "#":
                    grid[2 + y][x0 + x] = "#"
    # Two arrows, one from each side, heads nearly touching in the middle.
    mid = 15
    for x in range(2, 14):
        grid[mid][x] = grid[mid + 1][x] = "#"
        grid[mid][31 - x] = grid[mid + 1][31 - x] = "#"
    for d in range(1, 5):
        for y in (mid - d, mid + 1 + d):
            grid[y][14 - d] = "#"
            grid[y][17 + d] = "#"
    return ["".join(r) for r in grid]


def main(path):
    rows = draw()
    width, height = 32, len(rows)
    pixels = b""
    for row in reversed(rows):  # BMP rows run bottom-up
        bits = 0
        for c in row:
            bits = (bits << 1) | (c == "#")
        pixels += bits.to_bytes(4, "big")
    palette = bytes([255, 255, 255, 0, 0, 0, 0, 0])  # index 1 is black
    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 1, 0, len(pixels), 2835, 2835, 2, 0)
    offset = 14 + len(info) + len(palette)
    with open(path, "wb") as f:
        f.write(b"BM" + struct.pack("<IHHI", offset + len(pixels), 0, 0, offset))
        f.write(info + palette + pixels)


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[2] == "--show":
        print("\n".join(draw()))
    main(sys.argv[1])
