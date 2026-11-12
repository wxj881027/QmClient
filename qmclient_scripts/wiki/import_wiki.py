#!/usr/bin/env python3
"""把外部写好的 Markdown 导入成客户端 Wiki。

处理三件事：
1. 图片引用指向的本地视频/动图（mp4/webm/gif）转成动图 WebP，静态图复制或转码；
2. 行内与块级公式（$...$ / $$...$$）预渲染成 media/math/<slug>.webp；
3. 把 Markdown 写到 data/wiki/<语言>/<id>.md，图片引用改写成 media 目录里的文件名。

公式在 Markdown 里仍然是 TeX 原文，客户端按同一套 slug 规则去找预渲染图；
找不到时退回显示 TeX 源码，不会报错。
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    Image = None

MATH_BLOCK = re.compile(r"\$\$(.+?)\$\$", re.S)
MATH_INLINE = re.compile(r"(?<!\\)\$([^$\n]+?)(?<!\\)\$")
IMAGE = re.compile(r"!\[([^\]]*)\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
VIDEO_SUFFIXES = {".mp4", ".webm", ".mov"}
ANIMATED_SUFFIXES = {".gif", ".webp"}
STATIC_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp"}

KATEX_DIR = Path("tmp/wiki_tools/node_modules/katex/dist")
EDGE_PATHS = [
    Path(r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"),
    Path(r"C:\Program Files\Microsoft\Edge\Application\msedge.exe"),
]


def find_edge() -> Path | None:
    for path in EDGE_PATHS:
        if path.exists():
            return path
    found = shutil.which("msedge")
    return Path(found) if found else None


def math_slug(source: str) -> str:
    """与客户端 QmWikiMarkdownRender.cpp 的 MathSlug 保持逐字节一致。"""
    out: list[str] = []
    length = 0
    for byte in source.encode("utf-8"):
        ch = chr(byte)
        keep = (byte < 128 and ch.isalnum()) or ch in "-_"
        if keep:
            out.append(ch)
            length += 1
        elif out and out[-1] != "_":
            out.append("_")
            length += 1
        if length >= 72:
            break
    slug = "".join(out).rstrip("_")
    return slug or "formula"


def render_math(tex: str, out_path: Path, display: bool, font_px: int = 32) -> bool:
    edge = find_edge()
    if edge is None or not (KATEX_DIR / "katex.min.css").exists():
        print(f"跳过公式（缺少 Edge 或 KaTeX）：{tex[:40]}", file=sys.stderr)
        return False
    css = (KATEX_DIR / "katex.min.css").resolve().as_uri()
    js = (KATEX_DIR / "katex.min.js").resolve().as_uri()
    html = f"""<!doctype html><html><head><meta charset="utf-8">
<link rel="stylesheet" href="{css}">
<style>
html,body{{margin:0;padding:24px;background:transparent;}}
#f{{font-size:{font_px}px;color:#ffffff;display:inline-block;}}
</style>
<script src="{js}"></script></head><body><span id="f"></span>
<script>katex.render({json.dumps(tex)}, document.getElementById("f"), {{displayMode: {"true" if display else "false"}, throwOnError: false}});</script>
</body></html>"""
    with tempfile.TemporaryDirectory() as temp_dir:
        html_path = Path(temp_dir) / "math.html"
        png_path = Path(temp_dir) / "math.png"
        html_path.write_text(html, encoding="utf-8")
        command = [
            str(edge),
            "--headless=new",
            "--disable-gpu",
            "--hide-scrollbars",
            "--force-device-scale-factor=2",
            "--default-background-color=00000000",
            "--window-size=1600,600",
            f"--screenshot={png_path}",
            html_path.as_uri(),
        ]
        subprocess.run(command, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=90)
        if not png_path.exists():
            return False
        image = Image.open(png_path).convert("RGBA")
    bbox = image.getbbox()
    if bbox is None:
        return False
    pad = 6
    left = max(0, bbox[0] - pad)
    top = max(0, bbox[1] - pad)
    right = min(image.width, bbox[2] + pad)
    bottom = min(image.height, bbox[3] + pad)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    cropped = image.crop((left, top, right, bottom))
    # 等比缩到合适宽度，避免 2x 截图过大。
    max_width = 1400
    if cropped.width > max_width:
        height = max(1, int(cropped.height * max_width / cropped.width))
        cropped = cropped.resize((max_width, height), Image.LANCZOS)
    cropped.save(out_path, "WEBP", lossless=True, method=6)
    return True


def convert_media(source: Path, media_dir: Path, max_width: int) -> tuple[str, bool]:
    suffix = source.suffix.lower()
    stem = re.sub(r"[^A-Za-z0-9_-]+", "_", source.stem).strip("_") or "media"
    target = media_dir / f"{stem}.webp"
    if suffix in VIDEO_SUFFIXES or suffix in ANIMATED_SUFFIXES:
        if suffix == ".webp" and source.stat().st_size and target == source:
            return target.name, False
        ffmpeg = shutil.which("ffmpeg")
        if ffmpeg is None:
            raise RuntimeError(f"需要 ffmpeg 才能转换 {source.name}")
        command = [
            ffmpeg, "-y", "-v", "error", "-i", str(source),
            "-vf", f"fps=12,scale='min({max_width},iw)':-2:flags=lanczos",
            "-loop", "0", "-c:v", "libwebp", "-lossless", "0", "-q:v", "72",
            str(target),
        ]
        subprocess.run(command, check=True)
        return target.name, True
    if suffix in STATIC_SUFFIXES:
        if suffix == ".png":
            shutil.copy2(source, media_dir / f"{stem}.png")
            return f"{stem}.png", False
        image = Image.open(source).convert("RGBA")
        if image.width > max_width:
            height = max(1, int(image.height * max_width / image.width))
            image = image.resize((max_width, height), Image.LANCZOS)
        image.save(target, "WEBP", lossless=True, method=6)
        return target.name, True
    raise RuntimeError(f"不支持的媒体格式: {source.name}")


def main() -> int:
    parser = argparse.ArgumentParser(description="导入外部 Markdown 到客户端 Wiki")
    parser.add_argument("markdown", help="外部 Markdown 文件")
    parser.add_argument("--id", required=True, help="卡片 stableId 或文档 id，例如 qm:voice / docs/video_export")
    parser.add_argument("--lang", default="simplified_chinese", help="语言目录，默认 simplified_chinese")
    parser.add_argument("--out", default=None, help="直接写到指定 md 路径（默认 data/wiki/<lang>/<id>.md）")
    parser.add_argument("--media-dir", default="data/wiki/media", help="媒体输出目录")
    parser.add_argument("--max-width", type=int, default=960, help="图片/动图最大宽度")
    parser.add_argument("--dry-run", action="store_true", help="只打印将要做什么")
    args = parser.parse_args()

    if Image is None:
        print("需要 Pillow", file=sys.stderr)
        return 2

    source_md = Path(args.markdown)
    text = source_md.read_text(encoding="utf-8")
    media_dir = Path(args.media_dir)
    media_dir.mkdir(parents=True, exist_ok=True)

    converted: list[str] = []
    missing: list[str] = []

    def replace_image(match: re.Match) -> str:
        alt, ref = match.group(1), match.group(2)
        if ref.startswith(("http://", "https://", "data:")):
            missing.append(ref)
            return match.group(0)
        path = (source_md.parent / ref).resolve()
        if not path.exists():
            missing.append(ref)
            return match.group(0)
        name, changed = convert_media(path, media_dir, args.max_width)
        if changed:
            converted.append(name)
        return f"![{alt}]({name})"

    text = IMAGE.sub(replace_image, text)

    formulas: list[tuple[str, bool]] = []
    for match in MATH_BLOCK.finditer(text):
        formulas.append((match.group(1).strip(), True))
    for match in MATH_INLINE.finditer(MATH_BLOCK.sub("", text)):
        formulas.append((match.group(1).strip(), False))

    rendered = 0
    for tex, display in formulas:
        slug = math_slug(tex)
        out_path = media_dir / "math" / f"{slug}.webp"
        if out_path.exists():
            continue
        if args.dry_run:
            print(f"[dry] formula {'$$' if display else '$'}{tex[:40]} -> {out_path}")
            continue
        if render_math(tex, out_path, display):
            rendered += 1

    target = Path(args.out) if args.out else Path(f"data/wiki/{args.lang}/{args.id.replace(':', '/')}.md")
    if args.dry_run:
        print(f"[dry] write {target}")
    else:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8", newline="")
        print(f"写入 {target}")

    print(f"媒体：{len(converted)} 个转换，公式：{rendered} 个渲染，共 {len(formulas)} 个")
    if missing:
        print(f"未处理的远程/缺失引用 {len(missing)} 个：{', '.join(missing[:5])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
