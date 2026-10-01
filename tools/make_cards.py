#!/usr/bin/env python3
"""Converts the FujiNet Texas Hold'em card art into apps/Holdem/cardart.h.

    python3 tools/make_cards.py <fujinet-texasHoldEm checkout> [out.h] [preview.png]

Source art, from github.com/dillera/fujinet-texasHoldEm (GPL-3.0):

- Apple II hi-res charset, support/apple2/gen/charset: 127 glyphs of 8 bytes,
  7 pixels a row with bit 0 leftmost and a set bit lit (white). A table card
  is two glyphs wide (14 px) and built as src/apple2/graphics.c drawCardAt()
  builds it: an edge row, the rank glyph pair, a middle pair, the suit glyph
  pair and an edge row, 26 rows in all.
- Atari Lynx sprites, src/lynx/card_sprites/*.bmp: the 10x17 face-up and
  face-down mini cards, the half card back and the 5x6 suits. They are 4-bit
  indexed BMPs whose palette is the Lynx pen remap, so pixels are mapped by
  index, not colour.

Output rows are Palm 1-bit order: bit 15 (or 7) is the leftmost pixel and a
set bit is black. Table cards are inverted (the Apple's lit card body is the
Palm's white paper, the felt around it black), padded to 16 px with felt.
"""
import os
import struct
import sys
import zlib

APPLE_CHARSET = "support/apple2/gen/charset"
LYNX = "src/lynx/card_sprites"

RANKS = "23456789tjqka"
SUITS = "cdhs"  # the server's order
APPLE_SUIT = {"h": 0x0A, "d": 0x0C, "c": 0x0E, "s": 0x10}
APPLE_RANK = {"t": 0x71, "j": 0x73, "q": 0x75, "k": 0x77, "a": 0x79, "?": 0x7B}
APPLE_MID = 0x0900       # blank middle of a face-up card
APPLE_BACK = 0x7B7C      # the checked back, also its rank and suit rows
APPLE_EDGE = (0b01111100, 0b00111111)  # drawCardAt's top/bottom edge masks
APPLE_CHIP = 0x22
MINI_RANK = "23456789TJQKA"  # Apple II text glyphs on the Lynx mini card


def read_bmp4(path):
    """A 4-bit indexed BMP as rows of palette indices, top row first."""
    data = open(path, "rb").read()
    offset, = struct.unpack_from("<I", data, 10)
    width, height, _, bpp = struct.unpack_from("<iiHH", data, 18)
    if bpp != 4:
        raise SystemExit("%s: expected a 4-bit BMP" % path)
    stride = ((width * 4 + 31) // 32) * 4
    rows = []
    for y in range(abs(height)):
        start = offset + y * stride
        row = []
        for x in range(width):
            byte = data[start + x // 2]
            row.append(byte >> 4 if x % 2 == 0 else byte & 15)
        rows.append(row)
    return rows if height < 0 else rows[::-1]


class Apple:
    def __init__(self, path):
        self.data = open(path, "rb").read()

    def glyph(self, c):
        """8 rows of 7 lit/unlit pixels, left to right."""
        return [[(self.data[c * 8 + y] >> x) & 1 for x in range(7)] for y in range(8)]

    def pair(self, c):
        left, right = self.glyph(c >> 8), self.glyph(c & 0xFF)
        return [left[y] + right[y] for y in range(8)]


def lit_row(byte):
    return [(byte >> x) & 1 for x in range(7)]


def table_piece(rows):
    """Lit pixels to Palm words: white card, black felt, padded to 16 px."""
    words = []
    for row in rows:
        bits = 0
        for lit in row + [0, 0]:
            bits = (bits << 1) | (0 if lit else 1)
        words.append(bits)
    return words


def ink_word(row, width):
    bits = 0
    for ink in row:
        bits = (bits << 1) | ink
    return bits << (width - len(row))


def c_array(ctype, name, rows, width):
    digits = (width + 3) // 4
    out = ["static const %s %s = {" % (ctype, name)]
    for row in rows:
        if isinstance(row, list):
            out.append("    { " + ", ".join("0x%0*X" % (digits, v) for v in row) + " },")
        else:
            out.append("    0x%0*X," % (digits, row))
    out.append("};")
    return "\n".join(out)


def build(src):
    apple = Apple(os.path.join(src, APPLE_CHARSET))
    edge = [lit_row(APPLE_EDGE[0]) + lit_row(APPLE_EDGE[1])]

    ranks = []
    for r in RANKS + "?":
        c = APPLE_RANK.get(r, 0x61 + 2 * (ord(r) - ord("2")) if r.isdigit() else None)
        ranks.append(table_piece(apple.pair((c << 8) | (c + 1))))
    suits = [table_piece(apple.pair((APPLE_SUIT[s] << 8) | (APPLE_SUIT[s] + 1))) for s in SUITS]
    suits.append(table_piece(apple.pair(APPLE_BACK)))
    mid = table_piece(apple.pair(APPLE_MID))
    back = table_piece(apple.pair(APPLE_BACK))
    edge_word = table_piece(edge)[0]

    # Mini cards: Lynx frames, with Apple text glyphs for the rank.
    lynx = os.path.join(src, LYNX)
    face = read_bmp4(os.path.join(lynx, "card-face-up.bmp"))
    down = read_bmp4(os.path.join(lynx, "card-face-down.bmp"))
    half = read_bmp4(os.path.join(lynx, "half-card-face-down.bmp"))
    # Lynx pens: 0/6 are the cut corners, e the face-up border, f its
    # white face; on the back c is the blue field and 8/e its pattern.
    face_ink = {0: 0, 6: 0, 0xE: 1, 0xF: 0}
    back_ink = {0: 0, 6: 0, 0xC: 1, 0x8: 0, 0xE: 1}
    mini_face = [ink_word([face_ink[p] for p in row], 16) for row in face]
    mini_back = [ink_word([back_ink[p] for p in row], 16) for row in down]
    mini_half = [ink_word([back_ink[p] for p in row], 8) for row in half]
    mini_suits = []
    for name in ("club", "diamond", "heart", "spade"):
        rows = read_bmp4(os.path.join(lynx, name + ".bmp"))
        rows = rows + [[0xF] * 5] * (6 - len(rows))
        mini_suits.append([ink_word([1 if p != 0xF else 0 for p in row], 8) for row in rows])
    mini_ranks = [[ink_word(row, 8) for row in apple.glyph(ord(ch))] for ch in MINI_RANK]
    chip = [ink_word(row, 8) for row in apple.glyph(APPLE_CHIP)]

    return {
        "ranks": ranks, "suits": suits, "mid": mid, "back": back, "edge": edge_word,
        "mini_face": mini_face, "mini_back": mini_back, "mini_half": mini_half,
        "mini_suits": mini_suits, "mini_ranks": mini_ranks, "chip": chip,
    }


def write_header(art, path):
    parts = [
        "/* Card art for FN Texas Hold'em. Generated by tools/make_cards.py; do not edit.",
        " *",
        " * From github.com/dillera/fujinet-texasHoldEm (GPL-3.0): the table cards",
        " * are the Apple II hi-res client's (support/apple2/gen/charset, built as",
        " * src/apple2/graphics.c drawCardAt does); the mini cards and small suits",
        " * are the Atari Lynx client's sprites (src/lynx/card_sprites), with Apple II",
        " * text glyphs for their ranks.",
        " *",
        " * Rows are Palm 1-bit: the high bit is the leftmost pixel, a set bit black.",
        " */",
        "#ifndef CARDART_H",
        "#define CARDART_H",
        "",
        '#include "fn_types.h"',
        "",
        "#define ART_PIECE_H 8   /* rank, middle and suit pieces of a table card */",
        "#define ART_MINI_H 17   /* Lynx mini card */",
        "#define ART_MINI_SUIT_H 6",
        "",
        "/* 2..9, T, J, Q, K, A, then '?' */",
        c_array("fn_u16", "artRank[14][ART_PIECE_H]", art["ranks"], 16),
        "",
        "/* c, d, h, s, then '?' */",
        c_array("fn_u16", "artSuit[5][ART_PIECE_H]", art["suits"], 16),
        "",
        c_array("fn_u16", "artMiddle[ART_PIECE_H]", art["mid"], 16),
        "",
        c_array("fn_u16", "artBack[ART_PIECE_H]", art["back"], 16),
        "",
        "static const fn_u16 artEdge = 0x%04X;" % art["edge"],
        "",
        "/* 10 px wide, in the high bits */",
        c_array("fn_u16", "artMiniFace[ART_MINI_H]", art["mini_face"], 16),
        "",
        c_array("fn_u16", "artMiniBack[ART_MINI_H]", art["mini_back"], 16),
        "",
        "/* 5 px wide: the edge of a back peeking out from under another */",
        c_array("fn_u8", "artMiniHalf[ART_MINI_H]", art["mini_half"], 8),
        "",
        "/* c, d, h, s; 5 px wide */",
        c_array("fn_u8", "artMiniSuit[4][ART_MINI_SUIT_H]", art["mini_suits"], 8),
        "",
        "/* 2..9, T, J, Q, K, A; 7 px wide */",
        c_array("fn_u8", "artMiniRank[13][ART_PIECE_H]", art["mini_ranks"], 8),
        "",
        "/* the Apple II client's chip; 7 px wide */",
        c_array("fn_u8", "artChip[ART_PIECE_H]", art["chip"], 8),
        "",
        "#endif /* CARDART_H */",
        "",
    ]
    with open(path, "w") as f:
        f.write("\n".join(parts))


def write_preview(art, path):
    """A PNG of every table card and mini card, 3x, for checking by eye."""
    cards = []
    for s in range(4):
        for r in range(13):
            cards.append(compose(art, r, s))
    cards.append(compose(art, 13, 4))
    w, h = 18 * 14, 30 * 4 + 22 * 4
    px = [[200] * w for _ in range(h)]
    for i, rows in enumerate(cards):
        x0, y0 = (i % 14) * 18 + 1, (i // 14) * 30 + 2
        for y, word in enumerate(rows):
            for x in range(16):
                px[y0 + y][x0 + x] = 0 if (word >> (15 - x)) & 1 else 255
    for s in range(4):
        for r in range(13):
            rows = mini(art, r, s)
            x0, y0 = r * 18 + 1, 122 + s * 22
            for y, word in enumerate(rows):
                for x in range(10):
                    px[y0 + y][x0 + x] = 0 if (word >> (15 - x)) & 1 else 255
    scale = 3
    raw = b""
    for row in px:
        line = bytes([0] + [v for v in row for _ in range(scale)])
        raw += line * scale

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 0, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw)))
        f.write(chunk(b"IEND", b""))


def compose(art, rank, suit):
    """As apps/Holdem/cards.c composes a table card (for the preview)."""
    if rank == 13:
        return [art["edge"]] + art["back"] * 3 + [art["edge"]]
    return [art["edge"]] + art["ranks"][rank] + art["mid"] + art["suits"][suit] + [art["edge"]]


def mini(art, rank, suit):
    rows = list(art["mini_face"])
    for y, bits in enumerate(art["mini_ranks"][rank]):
        rows[1 + y] |= bits << 6
    for y, bits in enumerate(art["mini_suits"][suit]):
        rows[10 + y] |= bits << 5
    return rows


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    out = argv[2] if len(argv) > 2 else os.path.join(
        os.path.dirname(__file__), "..", "apps", "Holdem", "cardart.h")
    art = build(argv[1])
    write_header(art, out)
    if len(argv) > 3:
        write_preview(art, argv[3])


if __name__ == "__main__":
    main(sys.argv)
