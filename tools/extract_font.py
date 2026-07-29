#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""extract_font.py — pull the font assets out of PCART.POD into a flat dir.

  extract_font.py <PCART.POD> <outdir>
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from podtool import Pod3  # noqa: E402


def main():
    pod = Pod3.load(sys.argv[1])
    outdir = sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    n = 0
    for e in pod.entries:
        base = e["name"].rsplit("\\", 1)[-1]
        if base.upper().endswith((".FNT", ".TEX")) and "FONT" in base.upper():
            with open(os.path.join(outdir, base.upper()), "wb") as f:
                f.write(pod.data(e))
            n += 1
    print("      extracted %d font files -> %s" % (n, outdir))


if __name__ == "__main__":
    main()
