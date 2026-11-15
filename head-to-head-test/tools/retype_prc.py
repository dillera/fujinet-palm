#!/usr/bin/env python3
"""Copy a .prc with a new database name and/or type in its header.

The h2h ROM carries NioLib this way (type NLrm): Palm OS loads shared
libraries by type and creator, so the ROM copy is never loaded from the slow
module ROM; H2H NL's Install NioLib copies it into RAM as libr again.

    tools/retype_prc.py NioLib.prc build/NioLib-rom.prc --name "NioLib (ROM copy)" --type NLrm
"""
import argparse
import sys

NAME, TYPE, CREATOR = slice(0, 32), slice(60, 64), slice(64, 68)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("src")
    p.add_argument("dst")
    p.add_argument("--name")
    p.add_argument("--type")
    a = p.parse_args(argv)
    data = bytearray(open(a.src, "rb").read())
    if len(data) < 78:
        sys.exit(f"{a.src}: not a PRC")
    if a.name is not None:
        name = a.name.encode("ascii")
        if len(name) > 31:
            sys.exit("--name: at most 31 characters")
        data[NAME] = name.ljust(32, b"\0")
    if a.type is not None:
        if len(a.type) != 4:
            sys.exit("--type: four characters")
        data[TYPE] = a.type.encode("ascii")
    open(a.dst, "wb").write(data)
    print(f"{a.dst}: {data[NAME].rstrip(bytes(1)).decode()} {data[TYPE].decode()}/{data[CREATOR].decode()}")


if __name__ == "__main__":
    main()
