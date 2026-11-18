#!/usr/bin/env python3
"""Copy an app .prc under its FJ- name: database name and Launcher name.

Every FujiNet NIO Palm app goes out as "FJ-<short name>", so the apps on a
Palm are easy to tell apart and to track. The name is chosen by creator code
(the creator, type and code are untouched, so settings carry over), and set
both in the PRC header (the database name HotSync and the ROM use) and in
the tAIN 1000 resource (the name the Launcher shows), which is replaced or
added. NioLib keeps its name: it is a library, and apps find it by name.

    tools/fj_brand.py H2H_NL.prc out/FJ-H2H.prc           # name by creator
    tools/fj_brand.py App.prc out/App.prc --name FJ-Thing  # explicit name
    tools/fj_brand.py --list                               # the name table
"""
import argparse
import struct
import sys

# creator -> FJ name (Palm database names: at most 31 characters)
FJ_NAMES = {
    "NiC1": "FJ-Config",    # NIO Config (nio-config)
    "ANHN": "FJ-H2H",       # H2H NL, head-to-head test (fujinet-palm)
    "NiT1": "FJ-LibTest",   # NioLibTest (fujinet-nio-lib example)
    "ANTS": "FJ-Test",      # FujiNetTest ("FI Test")
    "ANBW": "FJ-Bounce",    # BounceWorld ("FI Bounce")
    "ANLB": "FJ-Lobby",     # FujiLobby ("FI Lobby")
    "ANHS": "FJ-FujiCfg",   # FujiConfig ("FI Config"); FJ-Config is NIO Config
    "ANTH": "FJ-Holdem",    # Holdem ("FI Texas Hold'em")
    "ANFP": "FJ-Pet",       # FujiPet ("FI FujiPet")
}

HEADER = 78
TAIN, TAIN_ID = b"tAIN", 1000


def resources(data):
    n = struct.unpack_from(">H", data, 76)[0]
    entries = [struct.unpack_from(">4sHI", data, HEADER + 10 * i) for i in range(n)]
    ends = sorted({off for _, _, off in entries} | {len(data)})
    out = []
    for rtype, rid, off in entries:
        out.append([rtype, rid, data[off:ends[ends.index(off) + 1]]])
    return out


def build(header, res):
    table_end = HEADER + 10 * len(res) + 2  # two pad bytes after the table, as build-prc writes
    out = bytearray(header[:HEADER])
    struct.pack_into(">H", out, 76, len(res))
    off = table_end
    for rtype, rid, body in res:
        out += struct.pack(">4sHI", rtype, rid, off)
        off += len(body)
    out += b"\0\0"
    for _, _, body in res:
        out += body
    return bytes(out)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("src", nargs="?")
    p.add_argument("dst", nargs="?")
    p.add_argument("--name", help="explicit FJ- name (default: by creator)")
    p.add_argument("--list", action="store_true", help="print the creator -> name table")
    a = p.parse_args(argv)
    if a.list:
        for creator, name in FJ_NAMES.items():
            print(f"{creator}  {name}")
        return
    if not a.src or not a.dst:
        p.error("src and dst are required")
    data = open(a.src, "rb").read()
    if len(data) < HEADER or data[60:64] != b"appl":
        sys.exit(f"{a.src}: not an application PRC")
    creator = data[64:68].decode("ascii")
    name = a.name or FJ_NAMES.get(creator)
    if not name:
        sys.exit(f"{a.src}: creator {creator} has no FJ name; add it to FJ_NAMES or pass --name")
    if not name.startswith("FJ-"):
        sys.exit(f"--name {name}: FJ- names start with FJ-")
    raw = name.encode("ascii")
    if len(raw) > 31:
        sys.exit(f"{name}: at most 31 characters")
    res = resources(data)
    label = raw + b"\0" + (b"\0" if len(raw) % 2 == 0 else b"")  # NUL-terminated, even length
    for r in res:
        if r[0] == TAIN and r[1] == TAIN_ID:
            r[2] = label
            break
    else:
        res.append([TAIN, TAIN_ID, label])
    header = bytearray(data[:HEADER])
    header[0:32] = raw.ljust(32, b"\0")
    out = build(header, res)
    open(a.dst, "wb").write(out)
    old = data[:32].rstrip(b"\0").decode("latin-1")
    print(f"{a.dst}: \"{old}\" -> \"{name}\" ({creator}, {len(res)} resources)")


if __name__ == "__main__":
    main()
