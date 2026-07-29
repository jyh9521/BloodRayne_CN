#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build a Chinese JAPANESE.POD from the Japanese-source translation sheet."""

import argparse
import csv
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from podtool import Pod3  # noqa: E402
from textio import apply_gbk_fallbacks, dialect_for  # noqa: E402

ENC = "cp936"
JP_ENC = "cp932"


def read_sheet(path):
    with open(path, encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f, delimiter=dialect_for(path)))


def merge_japanese_sheet(sheet, outdir):
    rows = read_sheet(sheet)
    by_file_line = {}
    bad_chars = {}
    fallbacks = {}
    filled = empty = 0

    for row in rows:
        line_value = str(row.get("line", "")).strip()
        if line_value == "+":
            continue
        try:
            line_no = int(line_value)
        except (KeyError, ValueError):
            continue
        by_file_line[(row["file"], line_no)] = row

    jp_root = os.path.join(outdir, "WORLD", "JP")
    for root, _dirs, files in os.walk(jp_root):
        for fn in files:
            if not fn.upper().endswith(".TXT"):
                continue
            path = os.path.join(root, fn)
            name = fn.upper()
            with open(path, "rb") as f:
                lines = f.read().decode(JP_ENC).split("\r\n")
            out = list(lines)
            for i, line in enumerate(lines):
                row = by_file_line.get((name, i))
                if not row:
                    continue
                zh = (row.get("chinese") or "").strip()
                if zh:
                    filled += 1
                else:
                    zh = row.get("japanese", "")
                    empty += 1
                zh, used = apply_gbk_fallbacks(zh)
                for ch, repl in used:
                    fallbacks.setdefault(ch, [repl, 0])
                    fallbacks[ch][1] += 1
                for ch in zh:
                    if ord(ch) < 0x80:
                        continue
                    try:
                        ch.encode(ENC)
                    except UnicodeEncodeError:
                        bad_chars[ch] = bad_chars.get(ch, 0) + 1

                if name == "MSGLIST.TXT":
                    if i == 3:
                        out[i] = "1"
                    elif i == 5:
                        out[i] = '"ＭＳ Ｐゴシック"'
                    else:
                        english = row.get("english", "")
                        out[i] = '"%s", "%s"' % (english, zh)
                else:
                    parts = line.split(",", 2)
                    if len(parts) >= 3:
                        out[i] = "%s,%s, %s" % (parts[0], parts[1], zh)

            if name == "MSGLIST.TXT":
                while out and not out[-1].strip():
                    out.pop()
                for row in rows:
                    if row.get("file", "").upper() != "MSGLIST.TXT":
                        continue
                    if str(row.get("line", "")).strip() != "+":
                        continue
                    zh = (row.get("chinese") or "").strip()
                    english = (row.get("english") or "").strip()
                    if not zh or not english:
                        continue
                    zh, used = apply_gbk_fallbacks(zh)
                    for ch, repl in used:
                        fallbacks.setdefault(ch, [repl, 0])
                        fallbacks[ch][1] += 1
                    for ch in zh:
                        if ord(ch) < 0x80:
                            continue
                        try:
                            ch.encode(ENC)
                        except UnicodeEncodeError:
                            bad_chars[ch] = bad_chars.get(ch, 0) + 1
                    out.append('"%s", "%s"' % (english, zh))
                    filled += 1
                out.append("")

            with open(path, "wb") as f:
                f.write("\r\n".join(out).encode(ENC, "replace"))

    if fallbacks:
        print("NOTE: substituted %d non-GBK symbol(s):" % len(fallbacks))
        for ch, (repl, n) in fallbacks.items():
            print("      %s -> %s  (x%d)" % (ch, repl, n))
    if bad_chars:
        raise SystemExit("GBK 无法编码这些字符：%s" % "".join(sorted(bad_chars)))
    print("merged %d translated rows (%d empty rows preserved/fell back to Japanese)" % (filled, empty))


def glyph_gate(pod_path):
    pod = Pod3.load(pod_path)
    fnt = [e for e in pod.entries if e["name"].endswith("DBCSFONT.FNT")][0]
    body = [l.strip() for l in pod.data(fnt).decode("latin-1").split("\n")]
    body = [l for l in body if l and not l.startswith("//")]
    first, second = [int(x) for x in body[2].split(",")]
    rows = [l for l in body[3:] if l.count(",") >= 9]
    loaded = {int(l.split(",")[0]) for l in rows[: second - first + 1]}
    missing = set()
    for e in pod.entries:
        if not e["name"].upper().endswith(".TXT"):
            continue
        for ch in pod.data(e).decode(ENC, "replace"):
            if ord(ch) <= 0x7F:
                continue
            try:
                if int.from_bytes(ch.encode(ENC), "big") not in loaded:
                    missing.add(ch)
            except UnicodeEncodeError:
                missing.add(ch)
    if missing:
        raise SystemExit("font is incomplete: %s" % "".join(sorted(missing))[:80])
    print("glyph gate : OK, all %d loadable glyphs are present" % len(loaded))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game_dir")
    ap.add_argument("sheet")
    ap.add_argument("--font")
    ap.add_argument("--index", type=int, default=0)
    ap.add_argument("--variation", default="Bold", help="可变字体字重名；默认 Bold")
    ap.add_argument("--size", type=int, default=17)
    ap.add_argument("--out", default=os.path.join(HERE, "dist_release_jp"))
    ap.add_argument("--install", action="store_true")
    args = ap.parse_args()

    game_dir = os.path.abspath(args.game_dir)
    backup_pod = os.path.join(game_dir, "zh_cn_tools", "backup", "JAPANESE.POD.bak")
    src_pod = backup_pod if os.path.exists(backup_pod) else os.path.join(game_dir, "JAPANESE.POD")
    if not os.path.exists(src_pod):
        raise SystemExit("missing pristine Japanese POD: %s" % src_pod)

    work = os.path.join(tempfile.gettempdir(), "br_release_jp")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(args.out, exist_ok=True)

    def run(*cmd):
        env = os.environ.copy()
        env.setdefault("PYTHONIOENCODING", "utf-8")
        subprocess.run([sys.executable] + list(cmd), check=True, env=env)

    print("[1/4] unpacking pristine Japanese archive")
    run(os.path.join(HERE, "podtool.py"), "unpack", src_pod, work)

    print("[2/4] merging Japanese-source translations")
    merge_japanese_sheet(args.sheet, work)

    print("[3/4] building font atlas")
    fontsrc = tempfile.mkdtemp(prefix="brfont_")
    run(os.path.join(HERE, "extract_font.py"), os.path.join(game_dir, "PCART.POD"), fontsrc)
    cmd = [
        os.path.join(HERE, "mkdbcsfont.py"),
        "--chars-from", os.path.join(work, "WORLD"),
        "--orig-tex", os.path.join(fontsrc, "DBCSFONT.TEX"),
        "--orig-fnt", os.path.join(fontsrc, "DBCSFONT.FNT"),
        "--size", str(args.size),
        "--out-tex", os.path.join(work, "ART/DBCSFONT.TEX"),
        "--out-fnt", os.path.join(work, "DATA/DBCSFONT.FNT"),
        "--preview", os.path.join(args.out, "atlas.png"),
    ]
    if args.font:
        cmd.extend(["--font", args.font, "--index", str(args.index)])
    if args.variation:
        cmd.extend(["--variation", args.variation])
    run(*cmd)
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
    run(os.path.join(HERE, "podtool.py"), "pack", work, out_pod, "--template", src_pod)
    rc = subprocess.run([sys.executable, os.path.join(HERE, "podtool.py"), "verify", out_pod]).returncode
    if rc != 0:
        raise SystemExit("verification failed")
    glyph_gate(out_pod)
    if args.install:
        shutil.copy(out_pod, os.path.join(game_dir, "JAPANESE.POD"))
    print("-> %s (%.1f MB)" % (out_pod, os.path.getsize(out_pod) / 1e6))


if __name__ == "__main__":
    main()
