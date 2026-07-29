#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
mkdbcsfont.py — build a Chinese DBCSFONT.TEX / DBCSFONT.FNT pair for
BloodRayne: Terminal Cut.

Formats (reverse-engineered from the shipped Japanese font)
----------------------------------------------------------
.TEX  792-byte header, then two 8-bit planes of width*height bytes:
        plane 0 = luminance (the shipped font is a constant 0x01)
        plane 1 = alpha / glyph coverage
      Header u32s: [2, 2, width, height, 0, 0, 0xFF000000, 0xFFFFFFFF]

.FNT  plain text, version 1001:
        // .FNT version
        1001
        // charSpacing, lineHeight, lineSpacing, shadowXOffset, shadowYOffset
        2,79,2,0,0
        // firstChar, charCount
        33,1253
        <code>,<x>,<y>,<w>,<h>,<topBearing>,<u0>,<v0>,<u1>,<v1>
      <code> is the raw multi-byte code point as a big-endian integer
      (Shift-JIS in the original; GBK/cp936 for Chinese). Glyphs are
      tightly cropped; <topBearing> is the gap from the line top.

The ASCII range is copied verbatim from the original atlas so Latin text,
digits and the HUD keep their exact original look.

Usage
-----
  mkdbcsfont.py --chars-from <dir-of-gbk-txt> \
                --orig-tex ART/DBCSFONT.TEX --orig-fnt DATA/DBCSFONT.FNT \
                --font /path/NotoSansSC-VF.ttf --variation Bold \
                --out-tex out/DBCSFONT.TEX --out-fnt out/DBCSFONT.FNT
"""

import argparse
import os
import struct
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

from PIL import Image, ImageDraw, ImageFont

HEADER_SIZE = 792
ENCODING = "cp936"          # GBK
CJK_SIZE = 19               # em box in pixels, matches the Japanese atlas
PAD = 1


# 字体只在打包机上用来烘图集，最终进 POD 的是位图，玩家机器上不需要装字体。
# 但这也意味着：要公开发包就该用可再分发的字体（思源黑体 / Noto Sans CJK，
# OFL 许可），别用微软雅黑那种系统自带字体。
# 顺序 = 优先级：先自带的、再 OFL 的、最后才退回系统字体。
FONT_CANDIDATES = [
    ("fonts/NotoSansSC-VF.ttf", 0),
    ("fonts/SourceHanSansSC-Regular.otf", 0),
    ("fonts/SourceHanSansCN-Regular.otf", 0),
    ("fonts/NotoSansSC-Regular.otf", 0),
    ("fonts/NotoSansCJK-Regular.ttc", 2),
    ("C:/Windows/Fonts/NotoSansSC-VF.ttf", 0),
    ("C:/Windows/Fonts/SourceHanSansSC-Regular.otf", 0),
    ("C:/Windows/Fonts/msyh.ttc", 0),        # 微软雅黑，不可再分发
    ("C:/Windows/Fonts/simhei.ttf", 0),      # 黑体，不可再分发
    ("C:/Windows/Fonts/deng.ttf", 0),        # 等线
    ("C:/Windows/Fonts/simsun.ttc", 0),      # 宋体
    ("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 2),
    ("/System/Library/Fonts/PingFang.ttc", 0),
]
REDISTRIBUTABLE = ("sourcehan", "noto")


def find_font(here):
    """返回 (路径, ttc索引)。找不到就报错并列出候选，不静默失败。"""
    tried = []
    for rel, idx in FONT_CANDIDATES:
        p = rel if os.path.isabs(rel) or rel[1:2] == ":" else os.path.join(here, rel)
        tried.append(p)
        if os.path.exists(p):
            base = os.path.basename(p).lower()
            if not any(k in base for k in REDISTRIBUTABLE):
                print("NOTE: 用的是系统字体 %s。自用没问题，但公开发包请换成"
                      "思源黑体或 Noto Sans CJK（OFL 可再分发），"
                      "放到 zh_cn_tools/fonts/ 下即可。" % os.path.basename(p))
            return p, idx
    raise SystemExit(
        "找不到可用的中文字体。请把思源黑体放到 zh_cn_tools/fonts/ 下，"
        "或用 --font 显式指定。\n已尝试:\n  " + "\n  ".join(tried))


def parse_fnt(path):
    with open(path, "rb") as f:
        lines = [l.strip() for l in f.read().decode("latin-1").split("\n")]
    body = [l for l in lines if l and not l.startswith("//")]
    meta = body[1]              # charSpacing,lineHeight,...
    glyphs = []
    for l in body[3:]:
        p = l.split(",")
        if len(p) < 10:
            continue
        glyphs.append({
            "code": int(p[0]), "x": int(p[1]), "y": int(p[2]),
            "w": int(p[3]), "h": int(p[4]), "bearing": int(p[5]),
        })
    return meta, glyphs


def read_tex(path):
    with open(path, "rb") as f:
        raw = f.read()
    _t, _b, w, h = struct.unpack_from("<4I", raw, 0)
    header = raw[:HEADER_SIZE]
    body = raw[HEADER_SIZE:]
    alpha = Image.frombytes("L", (w, h), body[w * h:w * h * 2])
    return header, w, h, alpha


def write_tex(path, header, w, h, alpha):
    hdr = bytearray(header)
    struct.pack_into("<4I", hdr, 0, 2, 2, w, h)
    with open(path, "wb") as f:
        f.write(bytes(hdr))
        f.write(b"\x01" * (w * h))       # luminance plane
        f.write(alpha.tobytes())


def is_wanted(ch):
    """Anything the text contains that GBK can encode as a double byte.

    This deliberately mirrors the check build_release.py performs after
    packing: every non-ASCII character in the GBK text needs a glyph, full
    stop. An earlier version whitelisted Unicode blocks instead (CJK
    ideographs, CJK punctuation, full-width forms) and silently dropped
    General Punctuation - em dash, curly quotes, ellipsis - which are
    ordinary Chinese typography and perfectly encodable in GBK. Two different
    notions of "needed" is exactly how glyphs go missing, so there is now
    only one.

    Stray CP1252 bytes are handled at the source by textio.sanitize_latin,
    not here.
    """
    try:
        return len(ch.encode(ENCODING)) == 2
    except UnicodeEncodeError:
        return False


def collect_chars(paths):
    chars = set()
    strays = set()
    for p in paths:
        if os.path.isdir(p):
            files = [os.path.join(r, fn) for r, _d, fs in os.walk(p) for fn in fs
                     if fn.lower().endswith(".txt")]
        else:
            files = [p]
        for fp in files:
            with open(fp, "rb") as f:
                raw = f.read()
            try:
                text = raw.decode(ENCODING)
            except UnicodeDecodeError:
                text = raw.decode(ENCODING, "ignore")
            for ch in text:
                if ord(ch) <= 0x7F:
                    continue
                if is_wanted(ch):
                    chars.add(ch)
                else:
                    strays.add((fp, ch))
    if strays:
        files = sorted({fp for fp, _ in strays})
        print("NOTE: %d stray non-CJK high characters ignored, in %d file(s):"
              % (len(strays), len(files)))
        for fp in files[:5]:
            bad = "".join(sorted({c for f, c in strays if f == fp}))
            print("      %s  ->  %s" % (os.path.basename(fp), bad[:30]))
    return sorted(chars)


def plan_size(orig_w, orig_h, n_glyphs, cell, ascii_bottom, want=None):
    """Pick the smallest power-of-two atlas that fits, starting at the
    original size. The .FNT stores normalised UVs, so growing the texture is
    transparent to the engine as long as the header dimensions agree."""
    if want:
        return want, want
    w, h = orig_w, orig_h
    while True:
        per_row = max(1, w // cell)
        rows = -(-n_glyphs // per_row)
        if ascii_bottom + PAD + rows * cell <= h:
            return w, h
        if w >= 4096:
            raise SystemExit("%d glyphs will not fit even at 4096x4096; "
                             "lower --size" % n_glyphs)
        w, h = w * 2, h * 2


def build(args):
    header, W, H, orig_alpha = read_tex(args.orig_tex)
    orig_w, orig_h = W, H
    meta, orig_glyphs = parse_fnt(args.orig_fnt)

    ascii_glyphs = [g for g in orig_glyphs if g["code"] < 0x100]
    chars = collect_chars(args.chars_from)
    print("CJK/full-width glyphs needed: %d" % len(chars))

    ascii_bottom = max((g["y"] + g["h"]) for g in ascii_glyphs)
    W, H = plan_size(orig_w, orig_h, len(chars), args.size + PAD + 2,
                     ascii_bottom, args.atlas_size)
    if (W, H) != (orig_w, orig_h):
        print("atlas grown %dx%d -> %dx%d to fit" % (orig_w, orig_h, W, H))

    font_path, font_index = args.font, args.index
    if not font_path:
        font_path, font_index = find_font(os.path.dirname(os.path.abspath(__file__)))
        print("字体: %s (index %d)" % (font_path, font_index))
    try:
        font = ImageFont.truetype(font_path, args.size, index=font_index)
    except OSError as e:
        raise SystemExit("打不开字体 %s: %s" % (font_path, e))
    if args.variation:
        if not hasattr(font, "set_variation_by_name"):
            raise SystemExit("当前 Pillow 不支持可变字体字重选择：%s" % font_path)
        try:
            font.set_variation_by_name(args.variation)
            print("字体字重: %s" % args.variation)
        except Exception as e:
            names = []
            if hasattr(font, "get_variation_names"):
                try:
                    names = [
                        n.decode("ascii", "replace") if isinstance(n, bytes) else str(n)
                        for n in font.get_variation_names()
                    ]
                except Exception:
                    names = []
            raise SystemExit(
                "字体 %s 没有可用字重 %s；可用字重：%s；错误：%s"
                % (font_path, args.variation, ", ".join(names), e)
            )

    atlas = Image.new("L", (W, H), 0)
    out = []

    # 1. copy the ASCII strip verbatim, preserving its coordinates
    max_ascii_y = 0
    for g in ascii_glyphs:
        box = (g["x"], g["y"], g["x"] + g["w"], g["y"] + g["h"])
        atlas.paste(orig_alpha.crop(box), (g["x"], g["y"]))
        out.append(g)
        max_ascii_y = max(max_ascii_y, g["y"] + g["h"])

    # 2. lay CJK glyphs out in rows below the ASCII strip
    pen_x, pen_y = 0, ascii_bottom + PAD
    row_h = 0
    skipped = []
    for ch in chars:
        try:
            code = int.from_bytes(ch.encode(ENCODING), "big")
        except UnicodeEncodeError:
            skipped.append(ch)
            continue

        cell = Image.new("L", (args.size * 2, args.size * 2), 0)
        ImageDraw.Draw(cell).text((args.size // 4, 0), ch, fill=255, font=font)
        bbox = cell.getbbox()
        if bbox is None:                      # blank glyph, e.g. full-width space
            adv = args.size
            out.append({"code": code, "x": 0, "y": 0, "w": 0, "h": 0,
                        "bearing": adv})
            continue
        glyph = cell.crop(bbox)
        gw, gh = glyph.size
        bearing = bbox[1]

        if pen_x + gw + PAD > W:
            pen_x = 0
            pen_y += row_h + PAD
            row_h = 0
        if pen_y + gh > H:
            raise SystemExit("atlas overflow: %dx%d is too small for %d glyphs; "
                             "raise --height or lower --size" % (W, H, len(chars)))

        atlas.paste(glyph, (pen_x, pen_y))
        out.append({"code": code, "x": pen_x, "y": pen_y,
                    "w": gw, "h": gh, "bearing": bearing})
        pen_x += gw + PAD
        row_h = max(row_h, gh)

    if skipped:
        print("WARNING: %d chars are not representable in GBK and were dropped: %s"
              % (len(skipped), "".join(skipped[:40])))

    out.sort(key=lambda g: g["code"])
    # The second field is NOT a count, despite the comment the original files
    # carry. The loader allocates (second - first + 1) slots and reads exactly
    # that many entries, so it has to be an inclusive upper bound in a dense
    # index space: first + n - 1. Verified against all five shipped fonts
    # (DBCSFONT 33/1253 -> 1221 entries, BRFONT 33/255 -> 223, ...).
    # Writing the raw count here makes the engine silently drop every glyph
    # past entry (count - first), which shows up as blank characters in game.
    first = out[0]["code"]
    lines = ["// .FNT version", "1001",
             "// charSpacing, lineHeight, lineSpacing, shadowXOffset, shadowYOffset",
             meta,
             "// firstChar, charCount",
             "%d,%d" % (first, first + len(out) - 1)]
    for g in out:
        lines.append("%d,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f" % (
            g["code"], g["x"], g["y"], g["w"], g["h"], g["bearing"],
            g["x"] / W, g["y"] / H, (g["x"] + g["w"]) / W, (g["y"] + g["h"]) / H))

    os.makedirs(os.path.dirname(args.out_tex) or ".", exist_ok=True)
    os.makedirs(os.path.dirname(args.out_fnt) or ".", exist_ok=True)
    write_tex(args.out_tex, header, W, H, atlas)
    with open(args.out_fnt, "wb") as f:
        f.write(("\n".join(lines) + "\n").encode("latin-1"))
    if args.preview:
        atlas.save(args.preview)

    # Self-check: every declared glyph must actually have ink in the atlas,
    # and the header slot count must match the entry count exactly.
    declared = out[0]["code"] + len(out) - 1
    assert declared - out[0]["code"] + 1 == len(out), "slot count mismatch"
    empty = []
    for g in out:
        if g["w"] == 0 or g["h"] == 0:
            continue
        box = atlas.crop((g["x"], g["y"], g["x"] + g["w"], g["y"] + g["h"]))
        if not box.getbbox():
            empty.append(g["code"])
    if empty:
        print("WARNING: %d declared glyphs are blank in the atlas: %s"
              % (len(empty), [hex(c) for c in empty[:10]]))

    print("%d glyphs (%d ASCII + %d CJK), atlas used up to y=%d of %d"
          % (len(out), len(ascii_glyphs), len(out) - len(ascii_glyphs),
             pen_y + row_h, H))
    print("wrote %s / %s" % (args.out_tex, args.out_fnt))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--chars-from", nargs="+", required=True)
    ap.add_argument("--orig-tex", required=True)
    ap.add_argument("--orig-fnt", required=True)
    ap.add_argument("--font", help="不指定则自动探测")
    ap.add_argument("--index", type=int, default=0)
    ap.add_argument("--variation", help="可变字体字重名，例如 Regular / Medium / Bold")
    ap.add_argument("--size", type=int, default=CJK_SIZE)
    ap.add_argument("--atlas-size", type=int,
                    help="force a square atlas of this side length "
                         "(default: grow from the original 1024 as needed)")
    ap.add_argument("--out-tex", required=True)
    ap.add_argument("--out-fnt", required=True)
    ap.add_argument("--preview")
    build(ap.parse_args())


if __name__ == "__main__":
    sys.exit(main() or 0)
