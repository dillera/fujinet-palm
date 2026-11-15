#!/usr/bin/env python3
"""Zip the testers' apps with their README (make dist).

    make_dist.py OUT.zip README-TEMPLATE KEY=VALUE... -- FILE...

The template's @KEY@ placeholders take the values given (and @DATE@ today's
date). The files go in at the top level of the zip, after README.txt.
"""
import datetime
import os
import re
import sys
import zipfile


def main(argv):
    if "--" not in argv or len(argv) < 4:
        raise SystemExit(__doc__)
    split = argv.index("--")
    out, template, pairs, files = argv[0], argv[1], argv[2:split], argv[split + 1:]
    values = {"DATE": datetime.date.today().isoformat()}
    values.update(p.split("=", 1) for p in pairs)
    text = open(template).read()
    for key, value in values.items():
        text = text.replace(f"@{key}@", value)
    left = re.findall(r"@[A-Z]+@", text)
    if left:
        raise SystemExit(f"{template}: no value for {', '.join(sorted(set(left)))}")
    tmp = out + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
        info = zipfile.ZipInfo("README.txt", datetime.datetime.now().timetuple()[:6])
        info.external_attr = 0o100644 << 16  # a regular file, rw-r--r--
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, text.replace("\n", "\r\n"))
        for f in files:
            z.write(f, os.path.basename(f))
    os.replace(tmp, out)
    print(f"{out}: README.txt " + " ".join(os.path.basename(f) for f in files))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
