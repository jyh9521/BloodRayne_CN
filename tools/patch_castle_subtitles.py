#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
patch_castle_subtitles.py - fix DE_C1CASTLE missing in-game subtitles.

The original level script uses dbConversation(...) for the mobile armor banter.
Those calls play voice but do not show subtitles in Terminal Cut. The same
level already uses dbStartSay(...) for lines that do show subtitles, so this
tool rewrites:

    dbConversation(
    dbStartSay    (

The four spaces keep the SCB byte length unchanged, avoiding any script table
or POD layout changes. Only the modified POD entry CRC is updated.

Usage:
  python zh_cn_tools\\patch_castle_subtitles.py . --apply
  python zh_cn_tools\\patch_castle_subtitles.py . --restore
  python zh_cn_tools\\patch_castle_subtitles.py . --status
"""

import argparse
import os
import struct
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from podtool import Pod3, crc32_mpeg2  # noqa: E402

ENTRY_NAME = "WORLD\\DE_C1CASTLE.SCB"
ORIG = b"dbConversation("
PATCHED = b"dbStartSay    ("


def find_entry(pod):
    for i, e in enumerate(pod.entries):
        if e["name"].upper() == ENTRY_NAME.upper():
            return i, e
    raise SystemExit("%s not found in WORLD.POD" % ENTRY_NAME)


def patch_world(path, mode):
    raw = bytearray(open(path, "rb").read())
    pod = Pod3(bytes(raw))
    idx, e = find_entry(pod)
    data = bytearray(pod.data(e))
    n_orig = data.count(ORIG)
    n_patched = data.count(PATCHED)

    if mode == "status":
        if n_orig and not n_patched:
            state = "original"
        elif n_patched and not n_orig:
            state = "patched"
        else:
            state = "mixed/unknown"
        print("%s: %s (dbConversation=%d, dbStartSay-spaced=%d)"
              % (ENTRY_NAME, state, n_orig, n_patched))
        return 0

    if mode == "apply":
        if n_orig == 0 and n_patched:
            print("already patched (%d call sites)" % n_patched)
            return 0
        if n_patched:
            raise SystemExit("mixed state, refusing to patch")
        data = data.replace(ORIG, PATCHED)
        changed = n_orig
    else:
        if n_patched == 0 and n_orig:
            print("already original (%d call sites)" % n_orig)
            return 0
        if n_orig:
            raise SystemExit("mixed state, refusing to restore")
        data = data.replace(PATCHED, ORIG)
        changed = n_patched

    if len(data) != e["size"]:
        raise SystemExit("internal error: SCB size changed")
    raw[e["offset"]:e["offset"] + e["size"]] = data

    file_count = struct.unpack_from("<I", raw, 0x58)[0]
    index_off = struct.unpack_from("<I", raw, 0x108)[0]
    if idx >= file_count:
        raise SystemExit("internal error: index out of range")
    struct.pack_into("<I", raw, index_off + idx * 20 + 16, crc32_mpeg2(data))

    open(path, "wb").write(raw)
    print("%s: %s %d call site(s)" %
          (ENTRY_NAME, "patched" if mode == "apply" else "restored", changed))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game_dir")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--apply", action="store_true")
    g.add_argument("--restore", action="store_true")
    g.add_argument("--status", action="store_true")
    args = ap.parse_args()

    path = os.path.join(args.game_dir, "WORLD.POD")
    if not os.path.exists(path):
        raise SystemExit("WORLD.POD not found: %s" % path)
    mode = "apply" if args.apply else "restore" if args.restore else "status"
    return patch_world(path, mode)


if __name__ == "__main__":
    sys.exit(main())
