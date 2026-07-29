#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate per-video ASS hard-subtitle scripts from the translated TSV."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


HEADER = """[Script Info]
Title: BloodRayne {key} Chinese hard subtitles
ScriptType: v4.00+
PlayResX: 1280
PlayResY: 960
ScaledBorderAndShadow: yes
YCbCr Matrix: TV.601

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default, Microsoft YaHei UI, 44, &H00FFFFFF, &H000000FF, &H00000000, &H80000000, 0, 0, 0, 0, 100, 100, 0, 0, 1, 3.5, 0.8, 2, 80, 80, 220, 1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
"""


def ass_time(seconds: str) -> str:
    value = float(seconds)
    centis = int(round(value * 100))
    cs = centis % 100
    total = centis // 100
    s = total % 60
    total //= 60
    m = total % 60
    h = total // 60
    return f"{h}:{m:02d}:{s:02d}.{cs:02d}"


def ass_text(text: str) -> str:
    text = text.strip().replace("\\n", "\\N")
    text = text.replace("\r\n", "\\N").replace("\n", "\\N").replace("\r", "\\N")
    return text.replace("{", r"\{").replace("}", r"\}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("tsv", nargs="?", default="zh_cn_tools/video_subtitle_drafts/video_subtitles_to_translate.tsv")
    ap.add_argument("--out-dir", default="zh_cn_tools/hardsub_work/ass")
    args = ap.parse_args()

    rows = list(csv.DictReader(open(args.tsv, encoding="utf-8-sig", newline=""), delimiter="\t"))
    by_video: dict[str, list[dict[str, str]]] = {}
    missing: dict[str, list[str]] = {}
    for row in rows:
        if not (row.get("zh_text") or "").strip():
            missing.setdefault(row.get("video", ""), []).append(row.get("index", ""))
            continue
        by_video.setdefault(row["video"], []).append(row)
    if missing:
        for video, indexes in sorted(missing.items()):
            if video in by_video:
                raise SystemExit("missing zh_text rows in partially translated video %s: %s" % (video, indexes[:20]))
            print("skip untranslated video %s: %d rows" % (video, len(indexes)))

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for key, items in sorted(by_video.items()):
        items.sort(key=lambda r: int(r["index"]))
        lines = [HEADER.format(key=key)]
        for row in items:
            lines.append(
                "Dialogue: 0,{},{},Default,,0,0,0,,{}\n".format(
                    ass_time(row["start"]),
                    ass_time(row["end"]),
                    ass_text(row["zh_text"]),
                )
            )
        out = out_dir / f"{key}.ass"
        out.write_text("".join(lines), encoding="utf-8-sig")
        print("wrote %s (%d lines)" % (out, len(items)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
