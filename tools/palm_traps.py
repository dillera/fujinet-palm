#!/usr/bin/env python3
"""List the Palm OS calls in a built app that are newer than a given OS.

Every Palm OS call compiles to `trap #15` plus a trap number, and trap numbers
only grow from release to release, so the number alone says which OS first had
the call. Sections come from the "Palm OS x traps" markers in the 3.5 SDK's
CoreTraps.h. Calls made through a dispatch trap with a selector (the Text and
Int'l managers, for example) are not seen here, and neither are shared library
calls (0xA800 up), such as the old serial library's.

  palm_traps.py CoreTraps.h app-disassembly.txt [app-disassembly.txt ...]

The disassembly is `m68k-palmos-objdump -d` output; `make traps` runs it on
every app. The listed calls are fine where the app checks the OS first (the
Serial Manager's SerialDispatch, FN FujiPet's colour), and a crash on 3.1
anywhere else.
"""
import re
import sys

SECTIONS = [  # (marker in CoreTraps.h, OS that first had the traps after it)
    ("// Palm OS 3.1 traps", "3.1"),
    ("// Palm OS 3.2 & 3.3 traps", "3.2/3.3"),
    ("// WARNING!!  These are new traps for 3.5", "3.5"),
]


SYS_LIB_TRAP_BASE = 0xA800


def load_traps(path):
    names, release = {}, {}
    current = "<3.1"
    for line in open(path, encoding="latin-1"):
        for marker, os_name in SECTIONS:
            if line.startswith(marker):
                current = os_name
        m = re.match(r"#define\s+sysTrap(\w+)\s+0x([0-9A-Fa-f]{4})", line)
        if m and m.group(1) != "LastTrapNumber":
            number = int(m.group(2), 16)
            names.setdefault(number, m.group(1))
            release.setdefault(number, current)
    return names, release


def traps_in(path):
    used = set()
    lines = open(path, encoding="latin-1").read().splitlines()
    for i, line in enumerate(lines[:-1]):
        if "trap #15" in line:
            m = re.match(r"\s*[0-9a-f]+:\s+([0-9a-f]{4})\b", lines[i + 1])
            if m and int(m.group(1), 16) < SYS_LIB_TRAP_BASE:
                used.add(int(m.group(1), 16))
    return used


def main():
    names, release = load_traps(sys.argv[1])
    for path in sys.argv[2:]:
        late = sorted(t for t in traps_in(path) if release.get(t, "?") not in ("<3.1", "3.1"))
        print(f"{path}: {'nothing newer than 3.1' if not late else ''}")
        for t in late:
            print(f"   0x{t:04X} {names.get(t, '?'):32} Palm OS {release.get(t, '?')}")


if __name__ == "__main__":
    main()
