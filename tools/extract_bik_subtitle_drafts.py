#!/usr/bin/env python3
"""Create source-language subtitle drafts for the voiced Bink movies.

The output is intended for manual translation.  It does not install or encode
anything back into the game.
"""

from __future__ import annotations

import argparse
import csv
import os
import subprocess
import sys
from pathlib import Path


TARGETS = [
    ("INTRO_EN", "en", "INTRO_EN.bik"),
    ("INTRO_JP", "ja", "INTRO_JP.bik"),
    ("OUTRO5_EN", "en", "OUTRO5_EN.bik"),
    ("OUTRO5_JP", "ja", "OUTRO5_JP.bik"),
]


def die(msg: str) -> None:
    raise SystemExit(msg)


def find_ffmpeg() -> Path:
    env = os.environ.get("FFMPEG")
    if env and Path(env).is_file():
        return Path(env)
    try:
        import imageio_ffmpeg  # type: ignore

        return Path(imageio_ffmpeg.get_ffmpeg_exe())
    except Exception as exc:
        die(f"找不到 ffmpeg；请安装 imageio-ffmpeg 或设置 FFMPEG。错误：{exc}")


def run(cmd: list[str]) -> None:
    print(" ".join(str(x) for x in cmd), flush=True)
    subprocess.run(cmd, check=True)


def seconds_to_srt(t: float) -> str:
    if t < 0:
        t = 0
    ms_total = int(round(t * 1000))
    h, rem = divmod(ms_total, 3600_000)
    m, rem = divmod(rem, 60_000)
    s, ms = divmod(rem, 1000)
    return f"{h:02d}:{m:02d}:{s:02d},{ms:03d}"


def normalize_text(text: str) -> str:
    return " ".join(text.replace("\r", " ").replace("\n", " ").split())


def apply_manual_fixes(video: str, source: str, rows: list[dict[str, str]]) -> list[dict[str, str]]:
    # Keep a few obvious proper-noun fixes for the English track only.  Japanese
    # drafts are left as ASR output so they can be corrected during translation.
    if source != "en":
        return rows
    fixes = {
        "Princeton Society": "Brimstone Society",
        "Primstone Society": "Brimstone Society",
        "Limestone Society": "Brimstone Society",
        "The liars": "Beliar",
        "finished.": "is finished.",
        "rest reign": "rest, Rayne",
    }
    for row in rows:
        text = row["source_text"]
        for old, new in fixes.items():
            text = text.replace(old, new)
        row["source_text"] = text
    return rows


def write_srt(path: Path, rows: list[dict[str, str]], field: str = "source_text") -> None:
    blocks = []
    for idx, row in enumerate(rows, 1):
        blocks.append(
            f"{idx}\n{row['start_srt']} --> {row['end_srt']}\n{row[field]}\n"
        )
    path.write_text("\n".join(blocks), encoding="utf-8-sig")


def load_existing_translations(tsv: Path) -> dict[tuple[str, str], str]:
    if not tsv.is_file():
        return {}
    with tsv.open("r", encoding="utf-8-sig", newline="") as f:
        rows = csv.DictReader(f, delimiter="\t")
        return {
            (row.get("video", ""), row.get("index", "")): row.get("zh_text", "")
            for row in rows
            if row.get("video") and row.get("index") and row.get("zh_text")
        }


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("game_dir", nargs="?", default=".", help="游戏根目录")
    ap.add_argument("--source-dir", default="zh_cn_tools/backup/video_original", help="源 BIK 目录")
    ap.add_argument("--out-dir", default="zh_cn_tools/video_subtitle_drafts", help="输出目录")
    ap.add_argument("--model", default="small", help="faster-whisper 模型名")
    args = ap.parse_args(argv)

    root = Path(args.game_dir).resolve()
    source_dir = (root / args.source_dir).resolve()
    out_dir = (root / args.out_dir).resolve()
    audio_dir = out_dir / "_audio"
    out_dir.mkdir(parents=True, exist_ok=True)
    audio_dir.mkdir(parents=True, exist_ok=True)

    ffmpeg = find_ffmpeg()
    try:
        from faster_whisper import WhisperModel  # type: ignore
    except Exception as exc:
        die(f"找不到 faster-whisper；请先安装。错误：{exc}")

    model = WhisperModel(args.model, device="cpu", compute_type="int8")
    tsv = out_dir / "video_subtitles_to_translate.tsv"
    existing_zh = load_existing_translations(tsv)
    all_rows: list[dict[str, str]] = []

    for video, source_lang, filename in TARGETS:
        src = source_dir / filename
        if not src.is_file():
            die(f"缺少源视频：{src}")
        wav = audio_dir / f"{video}.wav"
        run([str(ffmpeg), "-hide_banner", "-y", "-i", str(src), "-vn", "-ac", "1", "-ar", "16000", str(wav)])

        segments, _info = model.transcribe(
            str(wav),
            language=source_lang,
            task="transcribe",
            beam_size=5,
            vad_filter=False,
            condition_on_previous_text=False,
        )
        rows: list[dict[str, str]] = []
        for segment in segments:
            text = normalize_text(segment.text)
            if not text:
                continue
            rows.append(
                {
                    "video": video,
                    "source_lang": source_lang,
                    "index": str(len(rows) + 1),
                    "start": f"{segment.start:.3f}",
                    "end": f"{segment.end:.3f}",
                    "start_srt": seconds_to_srt(segment.start),
                    "end_srt": seconds_to_srt(segment.end),
                    "source_text": text,
                    "zh_text": "",
                    "note": "ASR draft; please correct source text if needed",
                }
            )
        rows = apply_manual_fixes(video, source_lang, rows)
        for row in rows:
            row["zh_text"] = existing_zh.get((row["video"], row["index"]), "")
        write_srt(out_dir / f"{video}.source.srt", rows)
        all_rows.extend(rows)

    with tsv.open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.DictWriter(
            f,
            delimiter="\t",
            fieldnames=[
                "video",
                "source_lang",
                "index",
                "start",
                "end",
                "start_srt",
                "end_srt",
                "source_text",
                "zh_text",
                "note",
            ],
        )
        writer.writeheader()
        writer.writerows(all_rows)

    print(f"写出：{tsv}")
    for video, _source_lang, _filename in TARGETS:
        print(out_dir / f"{video}.source.srt")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
