#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
build_release.py — CSV translation sheet -> installable JAPANESE.POD.

This is the pipeline you run for every build once translation is underway:

  翻译表.csv --textio--> GBK text tree
                          |
                          +--> glyph set actually used
                          |         |
                          |    mkdbcsfont (atlas grows automatically)
                          |         |
                          +---------+--> podtool pack --> dist_release/JAPANESE.POD

Rows with an empty `chinese` cell fall back to English, so partial sheets
build and run fine — translate a level, build, test, repeat.

Usage:
  build_release.py <game-dir> <sheet.csv> [--font F] [--index N] [--variation V] [--size 17]
                   [--install]
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from podtool import Pod3  # noqa: E402
import textio  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game_dir")
    ap.add_argument("sheet")
    ap.add_argument("--font", help="不指定则自动探测系统中文字体")
    ap.add_argument("--index", type=int, default=0)
    ap.add_argument("--variation", default="Bold", help="可变字体字重名；默认 Bold")
    ap.add_argument("--size", type=int, default=17)
    ap.add_argument("--out", default=os.path.join(HERE, "dist_release"))
    ap.add_argument("--install", action="store_true",
                    help="also copy the result over the game's JAPANESE.POD")
    args = ap.parse_args()

    work = os.path.join(tempfile.gettempdir(), "br_release")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(args.out, exist_ok=True)

    backup = os.path.join(args.game_dir, "zh_cn_tools", "backup",
                          "JAPANESE.POD.bak")
    src_pod = backup if os.path.exists(backup) else os.path.join(
        args.game_dir, "JAPANESE.POD")

    def run(*cmd):
        env = os.environ.copy()
        env.setdefault("PYTHONIOENCODING", "utf-8")
        subprocess.run([sys.executable] + list(cmd), check=True, env=env)

    print("[1/4] unpacking the pristine archive")
    run(os.path.join(HERE, "podtool.py"), "unpack", src_pod, work)

    print("[2/4] merging translations")
    textio.cmd_import(args.game_dir, args.sheet, work)

    print("[3/4] building the font atlas for the glyphs actually used")
    charset_dirs = [os.path.join(work, "WORLD")]
    # Terminal Cut has SRT parsing leftovers, but no working Bink subtitle
    # renderer. Release builds use hard-subtitled .bik files instead, so SRT
    # files are intentionally ignored here.

    fontsrc = tempfile.mkdtemp(prefix="brfont_")
    run(os.path.join(HERE, "extract_font.py"),
        os.path.join(args.game_dir, "PCART.POD"), fontsrc)
    run(os.path.join(HERE, "mkdbcsfont.py"),
        "--chars-from", *charset_dirs,
        "--orig-tex", os.path.join(fontsrc, "DBCSFONT.TEX"),
        "--orig-fnt", os.path.join(fontsrc, "DBCSFONT.FNT"),
        "--size", str(args.size),
        *(["--font", args.font, "--index", str(args.index)] if args.font else []),
        *(["--variation", args.variation] if args.variation else []),
        "--out-tex", os.path.join(work, "ART/DBCSFONT.TEX"),
        "--out-fnt", os.path.join(work, "DATA/DBCSFONT.FNT"),
        "--preview", os.path.join(args.out, "atlas.png"))
    shutil.rmtree(fontsrc, ignore_errors=True)

    man = os.path.join(work, "_pod_manifest.txt")
    have = open(man, encoding="utf-8").read()
    with open(man, "a", encoding="utf-8") as f:
        if "DBCSFONT.TEX" not in have:
            f.write("ART\\DBCSFONT.TEX\t1609780916\n")
        if "DBCSFONT.FNT" not in have:
            f.write("DATA\\DBCSFONT.FNT\t1609780916\n")

    print("[4/4] packing")
    out_pod = os.path.join(args.out, "JAPANESE.POD")
    run(os.path.join(HERE, "podtool.py"), "pack", work, out_pod,
        "--template", src_pod)
    rc = subprocess.run([sys.executable, os.path.join(HERE, "podtool.py"),
                         "verify", out_pod]).returncode
    if rc != 0:
        raise SystemExit("verification failed, refusing to install")

    # Final gate: every glyph the text needs must be in the font the engine
    # will actually load.
    pod = Pod3.load(out_pod)
    fnt = [e for e in pod.entries if e["name"].endswith("DBCSFONT.FNT")][0]
    body = [l.strip() for l in pod.data(fnt).decode("latin-1").split("\n")]
    body = [l for l in body if l and not l.startswith("//")]
    first, second = [int(x) for x in body[2].split(",")]
    rows = [l for l in body[3:] if l.count(",") >= 9]
    loaded = {int(l.split(",")[0]) for l in rows[:second - first + 1]}
    missing = set()
    for e in pod.entries:
        if not e["name"].upper().endswith(".TXT"):
            continue
        for ch in pod.data(e).decode("cp936", "replace"):
            if ord(ch) > 0x7F:
                try:
                    if int.from_bytes(ch.encode("cp936"), "big") not in loaded:
                        missing.add(ch)
                except UnicodeEncodeError:
                    missing.add(ch)
    print("glyph gate : %s" % ("OK, all %d needed glyphs are loadable"
                               % len(loaded) if not missing
                               else "MISSING %d: %s" % (len(missing),
                                                        "".join(sorted(missing))[:40])))
    if missing:
        raise SystemExit("font is incomplete, refusing to install")

    print("\n-> %s (%.1f MB)" % (out_pod, os.path.getsize(out_pod) / 1e6))
    if args.install:
        shutil.copy(out_pod, os.path.join(args.game_dir, "JAPANESE.POD"))
        print("installed to the game directory")


if __name__ == "__main__":
    main()
