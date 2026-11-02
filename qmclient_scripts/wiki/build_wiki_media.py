#!/usr/bin/env python3
"""把 Wiki 的图片与短动图转成客户端能直接播放的资源。

客户端只解码 PNG/WebP 静态图，不引入视频解码器；动图（gif/webp/avif/mp4）
在这里被抽帧并拼成“雪花图 + 元数据”，运行时按 fps 循环播放。

用法：
    python qmclient_scripts/wiki/build_wiki_media.py --input art/wiki --output data/wiki/media

输入可以是单个文件或目录；目录会递归处理 png/webp/gif/avif/mp4。
输出：
    静态图：<name>.png（宽度超过 --max-width 时等比缩小）
    动图：<name>.sheet.png + <name>.sheet.txt（cols/rows/frames/fps）
Markdown 里直接写源文件路径即可，例如 ![](voice.mp4)，客户端会自动找同名雪花图。
"""

from __future__ import annotations

import argparse
import math
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover - 环境缺少 Pillow 时给出可操作提示
    Image = None

ANIMATED_SUFFIXES = {".gif", ".webp", ".avif", ".mp4", ".webm"}
STATIC_SUFFIXES = {".png", ".webp"}
VIDEO_SUFFIXES = {".mp4", ".webm"}


def split_frames(image: "Image.Image") -> list["Image.Image"]:
    frames = []
    for index in range(getattr(image, "n_frames", 1)):
        image.seek(index)
        frames.append(image.convert("RGBA"))
    return frames


def decode_with_ffmpeg(path: Path, fps: float, max_frames: int) -> list["Image.Image"]:
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        raise RuntimeError("需要 ffmpeg 才能处理 mp4/avif，请安装后重试，或先导出为 gif/webp")
    with tempfile.TemporaryDirectory() as temp_dir:
        pattern = str(Path(temp_dir) / "frame-%04d.png")
        command = [
            ffmpeg,
            "-v",
            "error",
            "-i",
            str(path),
            "-vf",
            f"fps={fps}",
            "-frames:v",
            str(max_frames),
            pattern,
        ]
        subprocess.run(command, check=True)
        frames = [Image.open(frame).convert("RGBA") for frame in sorted(Path(temp_dir).glob("frame-*.png"))]
    if not frames:
        raise RuntimeError(f"未能从 {path} 解出任何帧")
    return frames


def build_sheet(frames: list["Image.Image"], fps: float, max_width: int, out_dir: Path, name: str) -> None:
    if not frames:
        raise RuntimeError("没有可写入的帧")
    frame_w, frame_h = frames[0].size
    if frame_w > max_width:
        scale = max_width / frame_w
        frame_w = max(1, int(frame_w * scale))
        frame_h = max(1, int(frame_h * scale))
        frames = [frame.resize((frame_w, frame_h), Image.LANCZOS) for frame in frames]

    count = len(frames)
    cols = max(1, math.ceil(math.sqrt(count)))
    rows = math.ceil(count / cols)
    sheet = Image.new("RGBA", (cols * frame_w, rows * frame_h), (0, 0, 0, 0))
    for index, frame in enumerate(frames):
        sheet.paste(frame, ((index % cols) * frame_w, (index // cols) * frame_h))
    sheet.save(out_dir / f"{name}.sheet.png")
    (out_dir / f"{name}.sheet.txt").write_text(
        f"cols={cols} rows={rows} frames={count} fps={fps:g}\n", encoding="utf-8"
    )


def read_fps(image: "Image.Image", fallback: float) -> float:
    duration = image.info.get("duration")
    if duration:
        try:
            return max(1.0, 1000.0 / float(duration))
        except (TypeError, ValueError, ZeroDivisionError):
            pass
    return fallback


def process_file(path: Path, out_dir: Path, max_width: int, fps: float, max_frames: int) -> str:
    out_dir.mkdir(parents=True, exist_ok=True)
    suffix = path.suffix.lower()
    name = path.stem
    if suffix in VIDEO_SUFFIXES:
        frames = decode_with_ffmpeg(path, fps, max_frames)
        build_sheet(frames, fps, max_width, out_dir, name)
        return f"动图 {path.name} -> {name}.sheet.png（{len(frames)} 帧）"
    if suffix not in STATIC_SUFFIXES and suffix not in ANIMATED_SUFFIXES:
        return f"跳过 {path.name}（不支持的格式）"
    if Image is None:
        raise RuntimeError("需要 Pillow 才能处理图片，请先安装 pillow")

    image = Image.open(path)
    if getattr(image, "n_frames", 1) > 1:
        frames = split_frames(image)[:max_frames]
        build_sheet(frames, read_fps(image, fps), max_width, out_dir, name)
        return f"动图 {path.name} -> {name}.sheet.png（{len(frames)} 帧）"

    image = image.convert("RGBA")
    if image.width > max_width:
        height = max(1, int(image.height * max_width / image.width))
        image = image.resize((max_width, height), Image.LANCZOS)
    image.save(out_dir / f"{name}.png")
    return f"静态图 {path.name} -> {name}.png（{image.width}x{image.height}）"


def main() -> int:
    parser = argparse.ArgumentParser(description="生成 Wiki 图片与动图资源")
    parser.add_argument("--input", required=True, help="图片/动图文件或目录")
    parser.add_argument("--output", default="data/wiki/media", help="输出目录，默认 data/wiki/media")
    parser.add_argument("--max-width", type=int, default=960, help="单帧最大宽度")
    parser.add_argument("--fps", type=float, default=12.0, help="源文件没有帧率时使用（视频同样按此抽帧）")
    parser.add_argument("--max-frames", type=int, default=24, help="最多保留的帧数")
    args = parser.parse_args()

    source = Path(args.input)
    if not source.exists():
        print(f"输入不存在：{source}", file=sys.stderr)
        return 2
    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)

    files = [source] if source.is_file() else sorted(p for p in source.rglob("*") if p.is_file())
    failures = 0
    for path in files:
        try:
            print(process_file(path, out_dir, args.max_width, args.fps, args.max_frames))
        except Exception as error:  # noqa: BLE001 - 逐文件报告，失败不中断其它资源
            failures += 1
            print(f"失败 {path.name}: {error}", file=sys.stderr)
    print(f"完成：{len(files) - failures} 个资源，输出到 {out_dir}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
