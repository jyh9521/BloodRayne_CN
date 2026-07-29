#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
textio.py — export the game script to a translation sheet and merge it back.

Two file shapes live in a language POD:

  level .TXT     <key>, <speaker>, <text>
                 <key> is a .wav filename for spoken lines, or a symbolic id
                 for HUD/objective strings. Text may itself contain commas,
                 so only the first two are separators. Blank lines are
                 meaningful spacing and are preserved.

  MSGLIST.TXT    six metadata lines, then "<english>", "<localized>" pairs.

Export writes one UTF-8-BOM CSV (opens cleanly in Excel) with the English in
one column and an empty column to fill in. Import rebuilds the GBK text tree
from that CSV, using the English archive as the structural skeleton so line
order, blank lines and untouched rows survive exactly.

Usage
-----
  textio.py export <game-dir> <sheet.csv>
  textio.py import <game-dir> <sheet.csv> <outdir>
  textio.py stats  <sheet.csv>
"""

import csv
import os
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from podtool import Pod3  # noqa: E402

ENC = "cp936"
FIELDS = ["file", "line", "kind", "key", "speaker", "english", "chinese", "note"]


def dialect_for(path):
    """Pick the delimiter from the extension.

    TSV is safe here: no field in the script contains a real tab or newline.
    The \\t sequences in strings like "Resolution\\t%dx%d" are a literal
    backslash followed by 't', two ordinary characters, not a tab byte.
    """
    return "\t" if path.lower().endswith((".tsv", ".tab", ".txt")) else ","


def read_sheet(path):
    with open(path, encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f, delimiter=dialect_for(path)))


def write_sheet(path, rows, fields=None):
    with open(path, "w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields or FIELDS,
                           delimiter=dialect_for(path))
        w.writeheader()
        w.writerows(rows)

# The English script is CP1252, not ASCII: curly apostrophes plus a handful of
# real accented characters (Scheisse, Erzsebet, s'il vous plait). GBK cannot
# encode most of these, and once double-byte mode is on a stray high byte gets
# eaten as a lead byte and corrupts the rest of the line. Transliterate to
# ASCII rather than dropping to '?', so untranslated English fallback lines
# stay readable.
CP1252_TO_ASCII = {
    0x91: "'", 0x92: "'", 0x93: '"', 0x94: '"',
    0x96: "-", 0x97: "-", 0x85: "...", 0xA0: " ",
    0xDF: "ss", 0xE9: "e", 0xE8: "e", 0xEA: "e", 0xEE: "i", 0xEF: "i",
    0xE0: "a", 0xE1: "a", 0xE2: "a", 0xE4: "ae", 0xE7: "c",
    0xF4: "o", 0xF6: "oe", 0xFB: "u", 0xFC: "ue", 0xF1: "n",
    0xC9: "E", 0xC4: "Ae", 0xD6: "Oe", 0xDC: "Ue",
}


# Symbols that look at home in Chinese text but have no GBK code point.
# Replacing them with '?' would be silent damage, so map them to the
# convention the English source already uses.
NON_GBK_FALLBACK = {
    "™": "(TM)", "©": "(C)", "®": "(R)", "€": "EUR",
    "‐": "-", "‑": "-", "−": "-", "•": "·",
}


def apply_gbk_fallbacks(text):
    """Returns (text, [(char, replacement)]) for anything GBK can't hold."""
    used = []
    for ch, repl in NON_GBK_FALLBACK.items():
        if ch in text:
            text = text.replace(ch, repl)
            used.append((ch, repl))
    return text, used


def sanitize_latin(raw):
    """CP1252 bytes -> ASCII. Returns str."""
    out = []
    for b in raw:
        if b in CP1252_TO_ASCII:
            out.append(CP1252_TO_ASCII[b])
        elif b > 0x7F:
            out.append("?")
        else:
            out.append(chr(b))
    return "".join(out)


def load_english(game_dir):
    """Return {basename: [lines]} from ENGLISH.POD, transliterated to ASCII."""
    pod = Pod3.load(os.path.join(game_dir, "ENGLISH.POD"))
    out = {}
    for e in pod.entries:
        name = e["name"].rsplit("\\", 1)[-1].upper()
        if not name.endswith(".TXT"):
            continue
        out[name] = sanitize_latin(pod.data(e)).split("\r\n")
    return out


def split_msglist_pair(line):
    """Split '"english", "localized"' respecting backslash escapes."""
    if not line.startswith('"'):
        return None
    i, esc = 1, False
    while i < len(line):
        c = line[i]
        if esc:
            esc = False
        elif c == "\\":
            esc = True
        elif c == '"':
            break
        i += 1
    left = line[1:i]
    rest = line[i + 1:].lstrip()
    if not rest.startswith(","):
        return None
    rest = rest[1:].strip()
    right = rest[1:-1] if rest.startswith('"') and rest.endswith('"') else rest
    return left, right


def cmd_export(game_dir, out_csv):
    files = load_english(game_dir)
    rows = []
    for name in sorted(files):
        for n, line in enumerate(files[name]):
            if not line.strip():
                continue
            if name == "MSGLIST.TXT":
                if n < 6:
                    continue          # metadata block, handled by the builder
                pair = split_msglist_pair(line)
                if not pair:
                    continue
                rows.append({"file": name, "line": n, "kind": "ui",
                             "key": "", "speaker": "",
                             "english": pair[0], "chinese": "", "note": ""})
            else:
                parts = line.split(",", 2)
                if len(parts) < 3:
                    continue
                key, speaker, text = parts[0].strip(), parts[1].strip(), parts[2].strip()
                kind = "voice" if key.lower().endswith(".wav") else "hud"
                rows.append({"file": name, "line": n, "kind": kind,
                             "key": key, "speaker": speaker,
                             "english": text, "chinese": "", "note": ""})

    write_sheet(out_csv, rows)

    kinds = {}
    for r in rows:
        kinds[r["kind"]] = kinds.get(r["kind"], 0) + 1
    words = sum(len(r["english"].split()) for r in rows)
    print("exported %d rows -> %s" % (len(rows), out_csv))
    print("  by kind : %s" % kinds)
    print("  ~%d English words to translate" % words)


def cmd_import(game_dir, in_csv, outdir):
    files = load_english(game_dir)
    rows = read_sheet(in_csv)

    # MSGLIST is a lookup keyed by the English source string. A chunk of the
    # setup/options UI is hardcoded in rayne1.exe and only renders translated
    # if a matching pair exists here, so those rows carry line="+" and get
    # appended rather than replacing an existing line.
    by_file = {}
    extras = []
    for r in rows:
        if str(r.get("line", "")).strip() in ("+", ""):
            if (r.get("chinese") or "").strip():
                extras.append(r)
            continue
        by_file.setdefault(r["file"], {})[int(r["line"])] = r

    os.makedirs(os.path.join(outdir, "WORLD", "JP"), exist_ok=True)
    filled = skipped = 0
    bad_chars = {}
    fallbacks = {}

    for name, lines in sorted(files.items()):
        edits = by_file.get(name, {})
        out = list(lines)
        for n, line in enumerate(lines):
            r = edits.get(n)
            if not r:
                continue
            zh = (r.get("chinese") or "").strip()
            if not zh:
                skipped += 1
                continue
            filled += 1
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
                    bad_chars.setdefault(ch, 0)
                    bad_chars[ch] += 1
            if name == "MSGLIST.TXT":
                pair = split_msglist_pair(line)
                out[n] = '"%s", "%s"' % (pair[0], zh)
            else:
                parts = line.split(",", 2)
                out[n] = "%s,%s, %s" % (parts[0], parts[1], zh)

        if name == "MSGLIST.TXT":
            out[3] = "1"                      # double byte support
            # Keep the original Japanese font marker. The engine's double-byte
            # font path is tied to this legacy marker; replacing it with a
            # Chinese face name can make it fail to resolve dbcsfont.tif even
            # though ART\DBCSFONT.TEX is present in the POD.
            out[5] = '"ＭＳ Ｐゴシック"'       # double byte font marker
            while out and not out[-1].strip():
                out.pop()
            for r in extras:
                zh, used = apply_gbk_fallbacks(r["chinese"].strip())
                for ch, repl in used:
                    fallbacks.setdefault(ch, [repl, 0])
                    fallbacks[ch][1] += 1
                out.append('"%s", "%s"' % (r["english"], zh))
                filled += 1
            out.append("")

        dest = os.path.join(outdir, "WORLD", "JP", name)
        text = "\r\n".join(out)
        with open(dest, "wb") as f:
            f.write(text.encode(ENC, "replace"))

    if fallbacks:
        print("NOTE: %d symbol(s) have no GBK code point and were substituted:"
              % len(fallbacks))
        for ch, (repl, n) in fallbacks.items():
            print("      %s -> %s  (x%d)" % (ch, repl, n))
    if bad_chars:
        print("WARNING: %d characters are not encodable in GBK and were "
              "replaced with '?': %s" % (len(bad_chars), "".join(bad_chars)))
        print("         GBK has no CJK Ext-B; avoid rare variant forms.")
    print("merged %d translated rows (%d still empty) -> %s"
          % (filled, skipped, outdir))
    return filled


def cmd_convert(src, dst):
    """CSV <-> TSV, preserving every cell verbatim."""
    rows = read_sheet(src)
    for r in rows:
        for k, v in r.items():
            if v and ("\t" in v or "\n" in v or "\r" in v):
                raise SystemExit("cell in row %s contains a real tab or "
                                 "newline; refusing to write TSV" % r.get("line"))
    write_sheet(dst, rows, fields=list(rows[0].keys()))
    done = sum(1 for r in rows if (r.get("chinese") or "").strip())
    print("%s -> %s  (%d rows, %d translated preserved)"
          % (os.path.basename(src), os.path.basename(dst), len(rows), done))


def cmd_stats(in_csv):
    rows = read_sheet(in_csv)
    done = [r for r in rows if (r.get("chinese") or "").strip()]
    chars = set()
    for r in done:
        for ch in r["chinese"]:
            if ord(ch) > 0x7F:
                chars.add(ch)
    print("rows        : %d" % len(rows))
    print("translated  : %d (%.1f%%)" % (len(done), 100.0 * len(done) / max(1, len(rows))))
    print("unique CJK  : %d glyphs needed so far" % len(chars))
    by_file = {}
    for r in rows:
        d = by_file.setdefault(r["file"], [0, 0])
        d[1] += 1
        if (r.get("chinese") or "").strip():
            d[0] += 1
    print("\nper file:")
    for name in sorted(by_file):
        d, t = by_file[name]
        print("  %-28s %4d/%-4d %s" % (name, d, t, "done" if d == t else ""))


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "export":
        cmd_export(argv[2], argv[3])
    elif cmd == "import":
        cmd_import(argv[2], argv[3], argv[4])
    elif cmd == "stats":
        cmd_stats(argv[2])
    elif cmd == "convert":
        cmd_convert(argv[2], argv[3])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
