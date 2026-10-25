#!/usr/bin/env python3
"""Draws FI FujiPet's art: writes apps/FujiPet/petart.h and the launcher icon.

Every sprite is 16x16 with four values a pixel: '.' clear, '#' outline,
'o' body and '+' accent. The app picks the body and accent colours (the
pet type's on a colour Palm; white and a grey dither on a black-and-white
one), so one drawing serves both. A sprite whose rows are 8 wide is the
left half of a symmetric one: the right half is its mirror image.

    python3 tools/make_pet_art.py                 # petart.h and icon.bmp
    python3 tools/make_pet_art.py --preview x.png # also a sheet of every sprite
"""
import argparse
import os
import struct

# ---- Shared sprites -----------------------------------------------------

EGG = """
........
......##
....##oo
...#oooo
...#o++o
..#oo++o
..#ooooo
..#ooooo
.#o++ooo
.#o++ooo
.#oooooo
.#oooo++
..#ooo++
...##ooo
.....###
........
"""

BABY = """
........
........
........
........
........
........
.....###
...##ooo
..#ooooo
..#oo#oo
.#ooo#oo
.#o++ooo
.#ooooo#
..#ooooo
...#####
........
"""

ANGEL = """
...+++++
..+.....
...+++++
....####
...#oooo
..#ooooo
..#o#ooo
..#o#ooo
.##ooooo
#.#ooo##
..#ooooo
..#ooooo
..#ooooo
..#o#ooo
..##.#o#
........
"""

POOP = """
....#.....#.....
.....#.....#....
....#.....#.....
.....#.....#....
................
.......##.......
......#oo#......
.....#oooo#.....
....########....
....#oooooo#....
...#oooooooo#...
..############..
..#oooooooooo#..
.#oooooooooooo#.
.##############.
................
"""

SKULL = """
........
........
....####
...#oooo
..#ooooo
..#o##oo
..#o##oo
..#ooooo
...#oo#o
....#ooo
....#o#o
.....###
........
........
........
........
"""

HEART = """
................
................
................
...###....###...
..#+++#..#+++#..
.#+++++##+++++#.
.#++++++++++++#.
.#++++++++++++#.
..#++++++++++#..
...#++++++++#...
....#++++++#....
.....#++++#.....
......#++#......
.......##.......
................
................
"""

MEAL = """
........
........
.......#
......#o
.....#oo
....#ooo
...#oooo
..#ooooo
.#oooooo
.#oo####
#ooo#+++
#ooo#+++
#ooo#+++
.#oo#+++
..######
........
"""

SNACK = """
........
......##
.....#++
...###++
..#ooooo
.#oooooo
#ooooooo
#ooooooo
########
.#+#+#+#
.#+#+#+#
..#+#+#+
..#+#+#+
...#####
........
........
"""

SYRINGE = """
................
................
................
................
.#..............
.#.##########...
.###++++oooo#...
.#.#++++oooo####
.###++++oooo#...
.#.##########...
.#..............
................
................
................
................
................
"""

ATTENTION = """
........
....####
...#....
..#....#
.#.....#
.#.....#
#......#
#......#
#......#
.#......
.#.....#
..#.....
...#....
....####
........
........
"""

# ---- Icons for the two bars (outline only) -------------------------------

ICON_FEED = """
................
...#.#.#....#...
...#.#.#...##...
...#.#.#...##...
...#.#.#...##...
...#####...##...
....###....##...
.....#.....##...
.....#.....#.#..
.....#......#...
.....#......#...
.....#......#...
.....#......#...
.....#......#...
....###....###..
................
"""

ICON_LIGHT = """
................
......####......
.....#....#.....
....#......#....
....#......#....
....#......#....
....#......#....
.....#....#.....
......#..#......
......####......
......#..#......
......####......
.......##.......
................
................
................
"""

ICON_PLAY = """
................
.....######.....
....#......#....
...#..####..#...
..#..#....#..#..
..#.#......#.#..
..#.#......#.#..
..#.#......#.#..
..#..#....#..#..
...#..####..#...
....#......#....
.....######.....
................
................
................
................
"""

ICON_MEDICINE = """
................
.....######.....
.....#....#.....
....##....##....
...#........#...
...#...##...#...
...#...##...#...
...#.######.#...
...#.######.#...
...#...##...#...
...#...##...#...
...#........#...
...##########...
................
................
................
"""

ICON_CLEAN = """
................
...........##...
..........##....
.........##.....
........##......
.......##.......
......##........
....######......
...#.#.#.#......
..#.#.#.#.#.....
.#.#.#.#.#......
#.#.#.#.#.......
................
................
................
................
"""

ICON_STATUS = """
................
................
............##..
............##..
........##..##..
........##..##..
....##..##..##..
....##..##..##..
##..##..##..##..
##..##..##..##..
################
................
................
................
................
................
"""

ICON_SCOLD = """
................
..###########...
.#...........#..
.#.....#.....#..
.#.....#.....#..
.#.....#.....#..
.#.....#.....#..
.#...........#..
.#.....#.....#..
..#####.######..
......#.#.......
.......##.......
................
................
................
................
"""

ICON_FRIENDS = """
........
...###..
..#...#.
..#...#.
..#...#.
...###..
..#####.
.#.....#
.#.....#
.#.....#
.#######
........
........
........
........
........
"""

SHARED = [
    ("EGG", EGG), ("BABY", BABY), ("ANGEL", ANGEL), ("POOP", POOP), ("SKULL", SKULL),
    ("HEART", HEART), ("MEAL", MEAL), ("SNACK", SNACK), ("SYRINGE", SYRINGE),
    ("ATTENTION", ATTENTION),
    ("ICON_FEED", ICON_FEED), ("ICON_LIGHT", ICON_LIGHT), ("ICON_PLAY", ICON_PLAY),
    ("ICON_MEDICINE", ICON_MEDICINE), ("ICON_CLEAN", ICON_CLEAN),
    ("ICON_STATUS", ICON_STATUS), ("ICON_SCOLD", ICON_SCOLD), ("ICON_FRIENDS", ICON_FRIENDS),
]

# ---- The four types, each grown seven ways --------------------------------
# child, good teen, poor teen, best adult, fair adult, poor adult, senior
# (a senior's accent is its white beard and brows).

CAT = [
"""
........
........
........
........
..#.....
..##....
..#+####
..#ooooo
.#oo#ooo
.#oo#ooo
##o++ooo
.#ooooo#
##oooo#o
..#ooooo
...#####
........
""", """
........
........
.#......
.##.....
.#+#....
.#++####
.#oooooo
#oo#oooo
#oo#oooo
#o++ooo#
.#oooo#o
..##oooo
..#ooo++
..#ooo++
..#o#ooo
..###.##
""", """
........
........
........
..#.....
..##.#..
..#+#+##
.#oooooo
#ooooooo
#o###ooo
#ooooooo
#oo+ooo#
.#ooo##o
.#oooooo
#.#ooooo
..#ooooo
...###.#
""", """
.#......
.##.....
.#+#....
.#++####
#ooooooo
#oo##ooo
#oo##ooo
#o++ooo#
.#oooo#o
..######
.#ooooo+
#oooo+++
#ooooo+o
.#oooooo
.#oo#ooo
.###.###
""", """
........
..#.....
..##....
..#+####
.#o#oooo
#ooooooo
#oo#oooo
#oo#oooo
#o++ooo#
#ooooo#o
#o#ooooo
#ooooooo
#o#ooooo
#ooooooo
.#oooooo
..######
""", """
........
........
........
.#......
.##.....
.#+#####
#ooooooo
#o##oooo
#ooo#ooo
#oo##ooo
#ooooooo
#oooo###
#ooo#ooo
#ooooooo
#ooooooo
.#######
""", """
........
.#......
.##.....
.#o#####
#ooooooo
#o+++ooo
#oo##ooo
#ooooooo
#ooooo##
.#o+++++
.#++++++
..#+++++
..#oo+++
..#ooooo
..#o#ooo
..###.##
"""]

DOG = [
"""
........
........
........
........
...#####
..#ooooo
.#+#oooo
#++#oooo
#++#o#oo
#++#o#oo
.##ooooo
..#ooo##
..#oooo#
..#ooooo
...#####
........
""", """
........
........
...#####
..#ooooo
.#+#oooo
#++#o#oo
#++#o#oo
#++#oooo
.##oo###
..#oooo#
...#####
..#ooooo
..#oo+++
..#ooooo
..#o#ooo
..###.##
""", """
........
........
........
...#.#.#
...#####
.##ooooo
#++#oooo
#++o###o
#++ooooo
.##oo###
..#oooo#
..#o#ooo
.#oooooo
.#oooo++
..#ooooo
...###.#
""", """
........
...#####
..#ooooo
.#+#oooo
#++#o##o
#++#o##o
#++#oooo
.##ooo##
..#oo#oo
...#oo##
..######
.#ooooo+
#oooo+++
#ooooo+o
.#oo#ooo
.###.###
""", """
........
........
..######
.#oooooo
#+#ooooo
#+#o#ooo
#+#o#ooo
#+#ooooo
#oooo###
#ooooo#o
#ooooooo
#oo++ooo
#oo++ooo
#ooooooo
.#oooooo
..######
""", """
........
........
........
..######
.#oooooo
#+##oooo
#++o#ooo
#++##ooo
#++ooo##
#ooooooo
#ooo####
#oo#oooo
#ooooooo
#oo+oooo
#ooooooo
.#######
""", """
........
...#####
..#ooooo
.#o#oooo
#oo#+++o
#oo#o##o
#oo#oooo
.##ooo##
..#+++++
..#+++++
...#++++
..#ooo++
..#ooooo
..#ooooo
..#o#ooo
..###.##
"""]

BUNNY = [
"""
........
...##...
..#++#..
..#++#..
..#++#..
..#o+#..
...#o###
..#ooooo
.#oo#ooo
.#oo#ooo
.#o+oooo
.#ooooo+
.#oooo#o
..#ooooo
...#####
........
""", """
..##....
.#++#...
.#++#...
.#++#...
.#o+#...
..#o####
.#oooooo
#oo#oooo
#oo#oooo
#o+oooo+
.#oooo#o
..#ooooo
..#oo+++
..#oo+++
..#o#ooo
..###.##
""", """
........
........
........
##......
#+##....
.#++####
..#ooooo
.#oooooo
#o###ooo
#ooooooo
#oo+ooo+
.#oooo#o
#.#ooooo
.#oooooo
..#ooooo
...###.#
""", """
.##.....
#++#....
#++#....
#++#....
#o+#####
.#oooooo
#oo##ooo
#oo##ooo
#o+oooo+
.#oooo#o
..######
.#ooooo+
#oooo+++
#ooooo+o
.#oo#ooo
.###.###
""", """
..##....
.#++#...
.#++#...
.#o+####
#ooooooo
#oo#oooo
#oo#oooo
#o+oooo+
#ooooo#o
#ooooooo
#oo++ooo
#oo++ooo
#ooooooo
#ooooooo
##oooo##
########
""", """
........
........
##......
#+#.....
#++#####
#+#ooooo
##oooooo
#o##oooo
#ooo#ooo
#oo##ooo
#ooooooo
#oooo###
#ooo#ooo
#ooooooo
#ooooooo
.#######
""", """
..##....
.#oo#...
.#oo#...
.#oo####
#ooooooo
#o+++ooo
#oo##ooo
#ooooooo
#oooooo+
.#++++++
.#++++++
..#+++++
..#oo+++
..#ooooo
..#o#ooo
..###.##
"""]

DINO = [
"""
........
........
........
........
.....#.#
....#+#+
...#oooo
..#ooooo
.#oo#ooo
.#oo#ooo
.#oooooo
.#oooooo
.#o#####
..#ooooo
...#####
........
""", """
........
........
....#.#.
...#+#+#
..#ooooo
.#oooooo
.#o#oooo
.#o#oooo
.#oooooo
.#o#####
..#ooooo
.#ooo+++
#oooo+++
.#ooo+++
..#o#ooo
.###.###
""", """
........
........
......#.
....#+#+
..##oooo
.#oooooo
#o###ooo
#ooooooo
#ooooooo
#o######
#oo+oooo
.#oooooo
#.#ooooo
.#oooooo
..#ooooo
...###.#
""", """
.....#.#
....#+#+
...#oooo
..#ooooo
.#oo##oo
.#oo##oo
.#oooooo
.#o#oooo
..#o####
..#ooooo
.#ooooo+
#oooo+++
#ooooo+o
.#oooooo
.#oo#ooo
.###.###
""", """
........
....#.#.
...#+#+#
..#ooooo
.#oooooo
#oo#oooo
#oo#oooo
#ooooooo
#o######
#oo#o#oo
#ooooooo
#ooo++++
#ooo++++
#ooooooo
.#oooooo
..######
""", """
........
........
...#.#.#
..#+#+#+
.#oooooo
#o##oooo
#ooo#ooo
#oo##ooo
#ooooooo
#oo#####
#o#o#o#o
#ooooooo
#ooooooo
#oo+++++
#ooooooo
.#######
""", """
........
....#.#.
...#o#o#
..#ooooo
.#o+++oo
.#oo##oo
.#oooooo
.#oooooo
.#++++++
.#++++++
..#+++++
..#oo+++
..#ooooo
..#ooooo
..#o#ooo
..###.##
"""]

TYPES = [("CAT", CAT), ("DOG", DOG), ("BUNNY", BUNNY), ("DINO", DINO)]
VALUES = {".": 0, "#": 1, "o": 2, "+": 3}


def rows_of(text, name):
    rows = [r for r in text.strip("\n").split("\n")]
    if len(rows) != 16:
        raise SystemExit(f"{name}: {len(rows)} rows, not 16")
    out = []
    for i, row in enumerate(rows):
        if len(row) == 8:
            row = row + row[::-1]
        if len(row) != 16 or any(c not in VALUES for c in row):
            raise SystemExit(f"{name} row {i}: {row!r}")
        out.append(row)
    return out


def all_sprites():
    sprites = [(name, rows_of(text, name)) for name, text in SHARED]
    for type_name, forms in TYPES:
        if len(forms) != 7:
            raise SystemExit(f"{type_name}: {len(forms)} forms, not 7")
        for i, text in enumerate(forms):
            sprites.append((f"{type_name}_{i}", rows_of(text, f"{type_name} form {i}")))
    return sprites


def packed(rows):
    out = []
    for row in rows:
        for i in range(0, 16, 4):
            b = 0
            for c in row[i:i + 4]:
                b = (b << 2) | VALUES[c]
            out.append(b)
    return out


def write_header(path, sprites):
    lines = [
        "/* FI FujiPet's sprites, written by tools/make_pet_art.py: edit that, not this. */",
        "#ifndef PETART_H", "#define PETART_H", "",
        "/* 16x16, 2 bits a pixel (0 clear, 1 outline, 2 body, 3 accent), the leftmost",
        " * pixel in the top bits, four bytes a row. */",
        "#define ART_SIZE 16", "#define ART_BYTES 64", "",
        "enum {",
    ]
    for i, (name, _) in enumerate(sprites[:len(SHARED)]):
        lines.append(f"    ART_{name} = {i},")
    lines += [f"    ART_TYPES = {len(SHARED)}  /* then 7 for each pet type */", "};",
              "#define ART_FORMS 7", "",
              "static const UInt8 petArt[][ART_BYTES] = {"]
    for name, rows in sprites:
        data = packed(rows)
        lines.append(f"    /* {name} */")
        for i in range(0, 64, 16):
            lines.append("    " + ("{ " if i == 0 else "  ") +
                         ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) +
                         (" }," if i == 48 else ","))
    lines += ["};", "", "#endif /* PETART_H */", ""]
    with open(path, "w") as f:
        f.write("\n".join(lines))


def write_bmp(path, rows):
    """A 1-bit BMP for PilRC, '#' black."""
    width, height = len(rows[0]), len(rows)
    stride = (width + 31) // 32 * 4
    pixels = b""
    for row in reversed(rows):
        bits = 0
        for c in row:
            bits = (bits << 1) | (c == "#")
        bits <<= stride * 8 - width
        pixels += bits.to_bytes(stride, "big")
    palette = bytes([255, 255, 255, 0, 0, 0, 0, 0])
    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 1, 0, len(pixels), 2835, 2835, 2, 0)
    offset = 14 + len(info) + len(palette)
    with open(path, "wb") as f:
        f.write(b"BM" + struct.pack("<IHHI", offset + len(pixels), 0, 0, offset))
        f.write(info + palette + pixels)


def icon_rows(sprites):
    """The launcher icon, 32x22: the Starcat beside a heart."""
    lookup = dict(sprites)
    canvas = [["."] * 32 for _ in range(22)]

    def stamp(rows, x, y, solid):
        for r, row in enumerate(rows):
            for c, ch in enumerate(row):
                if ch == "#" or (solid and ch == "+"):
                    if 0 <= y + r < 22 and 0 <= x + c < 32:
                        canvas[y + r][x + c] = "#"

    stamp(lookup["CAT_3"], 3, 4, False)
    stamp(lookup["HEART"], 17, 1, True)
    return ["".join(r) for r in canvas]


def write_preview(path, sprites):
    from PIL import Image

    colours = {0: (210, 230, 255), 1: (0, 0, 0), 2: (255, 170, 80), 3: (255, 120, 160)}
    scale, cols = 4, 8
    rows_n = (len(sprites) + cols - 1) // cols
    img = Image.new("RGB", (cols * 17 * scale, rows_n * 17 * scale), (255, 255, 255))
    for i, (_, rows) in enumerate(sprites):
        ox, oy = (i % cols) * 17 * scale, (i // cols) * 17 * scale
        for r, row in enumerate(rows):
            for c, ch in enumerate(row):
                colour = colours[VALUES[ch]]
                for dy in range(scale):
                    for dx in range(scale):
                        img.putpixel((ox + c * scale + dx, oy + r * scale + dy), colour)
    img.save(path)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    app = os.path.join(here, "..", "apps", "FujiPet")
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--header", default=os.path.join(app, "petart.h"))
    parser.add_argument("--icon", default=os.path.join(app, "icon.bmp"))
    parser.add_argument("--preview", help="also draw every sprite into this PNG")
    args = parser.parse_args()
    sprites = all_sprites()
    write_header(args.header, sprites)
    write_bmp(args.icon, icon_rows(sprites))
    if args.preview:
        write_preview(args.preview, sprites)


if __name__ == "__main__":
    main()
