#!/usr/bin/env python3
"""Burn Chinese subtitles into BloodRayne Bink cutscenes.

ffmpeg is used for subtitle burn-in and RAD Video Tools is used to encode Bink.
The script writes intermediates under zh_cn_tools/hardsub_work and final files
under zh_cn_tools/hardsub_dist by default. It never overwrites video/*.bik unless
--install is explicitly provided.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


TARGETS = {
    "INTRO_EN": ("INTRO_EN.bik", "zh_cn_tools/hardsub_intro.ass"),
    "INTRO_JP": ("INTRO_JP.bik", "zh_cn_tools/hardsub_intro.ass"),
    "OUTRO5_EN": ("OUTRO5_EN.bik", "zh_cn_tools/hardsub_outro5.ass"),
    "OUTRO5_JP": ("OUTRO5_JP.bik", "zh_cn_tools/hardsub_outro5.ass"),
}


def die(msg: str) -> None:
    raise SystemExit(msg)


def find_ffmpeg(root: Path) -> Path:
    env = os.environ.get("FFMPEG")
    if env and Path(env).is_file():
        return Path(env)
    found = shutil.which("ffmpeg")
    if found:
        return Path(found)
    try:
        import imageio_ffmpeg  # type: ignore

        return Path(imageio_ffmpeg.get_ffmpeg_exe())
    except Exception as exc:
        die(f"找不到 ffmpeg；可先安装 imageio-ffmpeg，或设置 FFMPEG。错误：{exc}")


def find_radvideo(root: Path) -> Path:
    env = os.environ.get("RADVIDEO")
    if env and Path(env).is_file():
        return Path(env)
    candidates = [
        root / "zh_cn_tools/radtools/portable/radvideo64.exe",
        root / "zh_cn_tools/radtools/extracted/radvideo64.exe",
    ]
    for cand in candidates:
        if cand.is_file():
            return cand
    found = shutil.which("radvideo64")
    if found:
        return Path(found)
    die("找不到 radvideo64.exe；请把 RAD Video Tools 解到 zh_cn_tools/radtools/portable。")


def ass_filter_path(path: Path) -> str:
    # ffmpeg filter paths on Windows want forward slashes and escaped ':'.
    s = path.resolve().as_posix()
    if len(s) >= 3 and s[1] == ":":
        s = s[0] + r"\:" + s[2:]
    return s.replace("'", r"\'")


def run(cmd: list[str]) -> None:
    print(" ".join(str(x) for x in cmd), flush=True)
    subprocess.run(cmd, check=True)


def build_one(root: Path, source_dir: Path, ffmpeg: Path, radvideo: Path, key: str, work: Path, out: Path, ass_dir: Path | None = None) -> Path:
    src_name, ass_rel = TARGETS[key]
    src = source_dir / src_name
    ass = (ass_dir / f"{key}.ass") if ass_dir else (root / ass_rel)
    if not src.is_file():
        die(f"缺少源视频：{src}")
    if not ass.is_file():
        die(f"缺少字幕：{ass}")

    work.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    mp4 = work / f"{key}.hardsub.mp4"
    bik = out / f"{key}.bik"

    vf = f"ass='{ass_filter_path(ass)}'"
    run([
        str(ffmpeg),
        "-hide_banner",
        "-y",
        "-i",
        str(src),
        "-vf",
        vf,
        "-c:v",
        "libx264",
        "-crf",
        "16",
        "-preset",
        "slow",
        "-pix_fmt",
        "yuv420p",
        "-c:a",
        "pcm_s16le",
        str(mp4),
    ])

    if bik.exists():
        bik.unlink()
    run([str(radvideo), "binkc", str(mp4), str(bik), "/#"])
    if not bik.is_file() or bik.stat().st_size == 0:
        die(f"RAD 没有生成有效 BIK：{bik}")
    return bik


def install_outputs(root: Path, built: list[Path]) -> None:
    backup = root / "zh_cn_tools/backup/video_original"
    backup.mkdir(parents=True, exist_ok=True)
    for bik in built:
        dst = root / "video" / bik.name
        if not dst.is_file():
            die(f"安装目标不存在：{dst}")
        bak = backup / dst.name
        if not bak.exists():
            shutil.copy2(dst, bak)
        shutil.copy2(bik, dst)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("game_dir", nargs="?", default=".", help="游戏根目录")
    ap.add_argument("--target", action="append", choices=sorted(TARGETS), help="只构建指定目标；可重复")
    ap.add_argument("--source-dir", help="源 BIK 目录；默认读取游戏 video 目录")
    ap.add_argument("--ass-dir", help="每视频 ASS 目录；若指定则读取 <target>.ass")
    ap.add_argument("--install", action="store_true", help="构建后覆盖安装到 video 目录，并备份原文件")
    args = ap.parse_args(argv)

    root = Path(args.game_dir).resolve()
    ffmpeg = find_ffmpeg(root)
    radvideo = find_radvideo(root)
    source_dir = (root / args.source_dir).resolve() if args.source_dir else root / "video"
    if args.target:
        keys = args.target
    elif args.ass_dir:
        ass_root = (root / args.ass_dir).resolve()
        keys = [key for key in TARGETS if (ass_root / f"{key}.ass").is_file()]
        if not keys:
            die(f"字幕目录中没有可构建的 ASS：{ass_root}")
    else:
        keys = list(TARGETS)
    work = root / "zh_cn_tools/hardsub_work/intermediate"
    out = root / "zh_cn_tools/hardsub_dist"
    ass_dir = (root / args.ass_dir).resolve() if args.ass_dir else None
    built = [build_one(root, source_dir, ffmpeg, radvideo, key, work, out, ass_dir) for key in keys]
    if args.install:
        install_outputs(root, built)
    print("完成：")
    for path in built:
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
