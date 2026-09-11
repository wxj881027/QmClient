#!/usr/bin/env python3
"""生成 QmClient 名牌专用 MSDF 字形图集（离线资源管线）。

产物写入 data/qmclient/nameplate_msdf/：
  <page>.png / <page>.json   图集页与字形清单（运行时按 manifest 加载）

字形来源与分层（每页只用一种字体，度量口径统一，不做跨页拼补）：
  page 0  base：DejaVuSans  —— 拉丁/拉丁扩展/希腊/西里尔/常用标点符号
  page 1  cjk ：SourceHanSans SC —— CJK 统一表意文字 + 假名 + CJK 标点，按码位升序截取

CJK 按码位升序截取而非按词频：U+4E00..U+9FFF 本身即康熙部首序，常用字集中在低段，
在有限图集预算下这是确定性强且覆盖「常用优先」的取法。

需要一次性构建的生成工具（正常客户端构建不依赖 msdfgen）：
  cmake -S qmclient_scripts/qm_nameplate_msdf_atlas -B <build> -DQM_PROJECT_ROOT=<repo>
  cmake --build <build> --config Release
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

# 基础脚本区：拉丁、希腊、西里尔、常用标点与符号
BASE_RANGES: tuple[tuple[int, int], ...] = (
    (0x0020, 0x007E),  # Basic Latin
    (0x00A0, 0x00FF),  # Latin-1 Supplement
    (0x0100, 0x017F),  # Latin Extended-A
    (0x0180, 0x024F),  # Latin Extended-B
    (0x0370, 0x03FF),  # Greek
    (0x0400, 0x04FF),  # Cyrillic
    (0x2010, 0x203A),  # General Punctuation（常用段）
    (0x20AC, 0x20AC),  # Euro
    (0x2116, 0x2116),  # №
    (0x2190, 0x2193),  # 箭头
    (0x25A0, 0x25CF),  # 几何图形（常用）
    (0x2605, 0x2606),  # ★☆
    (0x2665, 0x2665),  # ♥
)

CJK_BLOCKS: tuple[tuple[int, int], ...] = (
    (0x4E00, 0x9FFF),  # CJK 统一表意文字
    (0x3000, 0x303F),  # CJK 符号与标点
    (0x3040, 0x309F),  # 平假名
    (0x30A0, 0x30FF),  # 片假名
)

# 汉字区段：预算优先给汉字，假名/标点只在有余量时补
HAN_BLOCK = (0x4E00, 0x9FFF)

SOURCE_HAN_SC_FACE = 2  # SourceHanSans.ttc: 0 通用 / 2 简体


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", type=Path, required=True, help="qm-nameplate-msdf-atlas 可执行文件")
    parser.add_argument("--output", type=Path, required=True, help="输出目录（data/qmclient/nameplate_msdf）")
    parser.add_argument("--data-root", type=Path, default=Path("data"), help="data 根目录，用于定位字体")
    parser.add_argument("--em-pixels", type=int, default=27)
    parser.add_argument("--px-range", type=float, default=4.0)
    parser.add_argument("--base-size", type=int, default=2048)
    parser.add_argument("--cjk-size", type=int, default=4096)
    parser.add_argument("--cjk-max", type=int, default=8000, help="CJK 页最多烤多少字形（优先汉字）")
    return parser.parse_args()


def codepoints_in_ranges(ranges: tuple[tuple[int, int], ...]) -> list[int]:
    out: list[int] = []
    for low, high in ranges:
        out.extend(range(low, high + 1))
    return out


def font_coverage(path: Path, face_index: int = 0) -> set[int]:
    """返回字体覆盖的码位集合。"""
    from fontTools.ttLib import TTCollection, TTFont

    if path.suffix.lower() == ".ttc":
        font = TTCollection(str(path)).fonts[face_index]
    else:
        font = TTFont(str(path), fontNumber=face_index)
    return {cp for cp in font.getBestCmap() if cp > 0}


def write_charset(path: Path, codepoints: list[int]) -> None:
    path.write_text("".join(f"U+{cp:04X}\n" for cp in codepoints), encoding="utf-8")


def run_tool(args: argparse.Namespace, font: Path, face_index: int, codepoints: list[int], prefix: Path, size: int) -> dict:
    charset = prefix.with_suffix(".charset.txt")
    write_charset(charset, codepoints)
    command = [
        str(args.tool),
        "--font",
        str(font),
        "--font-index",
        str(face_index),
        "--charset",
        str(charset),
        "--output",
        str(prefix),
        "--em-pixels",
        str(args.em_pixels),
        "--px-range",
        str(args.px_range),
        "--size",
        str(size),
    ]
    print(f"  run: {font.name} face={face_index} chars={len(codepoints)} size={size}")
    result = subprocess.run(command, capture_output=True, text=True)
    if result.stdout.strip():
        sys.stdout.write("  " + result.stdout.strip().replace("\n", "\n  ") + "\n")
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise SystemExit(f"atlas tool failed ({result.returncode}) for {prefix}")
    manifest = json.loads(prefix.with_suffix(".json").read_text(encoding="utf-8"))
    # 页装不下会静默截断；这里显式失败，避免发布一个覆盖不全的图集
    packed = {int(key) for key in manifest["glyphs"]}
    missing = [cp for cp in codepoints if cp not in packed]
    if missing:
        raise SystemExit(
            f"atlas page too small for {font.name}: {len(missing)} codepoints unpacked "
            f"(first U+{missing[0]:04X}); increase --base-size/--cjk-size"
        )
    return manifest


def publish_page(args: argparse.Namespace, raw_prefix: Path, manifest: dict, image_name: str, font_label: str, out_dir: Path) -> dict:
    from PIL import Image

    width = manifest["atlas"]["width"]
    height = manifest["atlas"]["height"]
    data = raw_prefix.with_suffix(".rgba").read_bytes()
    if len(data) != width * height * 4:
        raise SystemExit(f"unexpected raw size for {raw_prefix}: {len(data)}")

    stem = Path(image_name).stem
    out_dir.mkdir(parents=True, exist_ok=True)
    # 图形上传路径使用 RGBA；RGB 通道是距离场，A 固定 255（与 SVG 图标图集一致）
    Image.frombytes("RGBA", (width, height), data).save(out_dir / f"{stem}.png", optimize=True)

    page = {
        "version": 1,
        "kind": "msdf-glyphs",
        "px_range": manifest["px_range"],
        "em_pixels": manifest["em_pixels"],
        "padding": manifest["padding"],
        "source_font": font_label,
        "atlas": {"image": image_name, "width": width, "height": height},
        "glyphs": manifest["glyphs"],
    }
    (out_dir / f"{stem}.json").write_text(json.dumps(page, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return page


def main() -> int:
    args = parse_args()
    if not args.tool.is_file():
        raise SystemExit(f"atlas tool not found: {args.tool}")

    fonts_dir = args.data_root / "fonts"
    dejavu = fonts_dir / "DejaVuSans.ttf"
    source_han = fonts_dir / "SourceHanSans.ttc"
    for font in (dejavu, source_han):
        if not font.is_file():
            raise SystemExit(f"missing source font: {font}")

    print("resolving coverage...")
    # 优先级顺序：ASCII → Latin-1 → Latin Ext-A → 标点符号 → 希腊/西里尔 → 其余。
    # 图集放不下时，工具按此顺序先满足靠前的字形。
    base_codepoints = [cp for cp in codepoints_in_ranges(BASE_RANGES) if cp in font_coverage(dejavu)]
    han_coverage = font_coverage(source_han, SOURCE_HAN_SC_FACE)
    han_codepoints = sorted(cp for cp in han_coverage if HAN_BLOCK[0] <= cp <= HAN_BLOCK[1])
    other_codepoints = sorted(
        cp
        for cp in han_coverage
        if any(low <= cp <= high for low, high in CJK_BLOCKS if (low, high) != HAN_BLOCK)
    )
    # 汉字优先：预算先给汉字，余量再补假名与 CJK 标点。
    # 只按码位取会让 253 个假名/标点挤进预算，汉字段被截到 U+5AAE，中文昵称几乎全部回退。
    cjk_codepoints = (han_codepoints + other_codepoints)[: args.cjk_max]
    baked_han = [cp for cp in cjk_codepoints if HAN_BLOCK[0] <= cp <= HAN_BLOCK[1]]
    print(f"  base: DejaVu covers {len(base_codepoints)} codepoints")
    print(
        f"  cjk : font covers {len(han_codepoints)} han; baking {len(cjk_codepoints)} "
        f"({len(baked_han)} han, up to U+{baked_han[-1]:04X})"
    )

    pages: list[dict] = []
    with tempfile.TemporaryDirectory(prefix="qm-nameplate-msdf-") as temp:
        temp_dir = Path(temp)

        if base_codepoints:
            prefix = temp_dir / "base"
            manifest = run_tool(args, dejavu, 0, base_codepoints, prefix, args.base_size)
            page = publish_page(
                args, prefix, manifest, "qmclient/nameplate_msdf/nameplate_base_msdf.png", "DejaVuSans", args.output
            )
            pages.append(page)
            print(f"  base page: {len(page['glyphs'])} glyphs {page['atlas']['width']}x{page['atlas']['height']}")

        if cjk_codepoints:
            prefix = temp_dir / "cjk"
            manifest = run_tool(args, source_han, SOURCE_HAN_SC_FACE, cjk_codepoints, prefix, args.cjk_size)
            page = publish_page(
                args,
                prefix,
                manifest,
                "qmclient/nameplate_msdf/nameplate_cjk_msdf.png",
                f"SourceHanSansSC#{SOURCE_HAN_SC_FACE}",
                args.output,
            )
            pages.append(page)
            print(f"  cjk page: {len(page['glyphs'])} glyphs {page['atlas']['width']}x{page['atlas']['height']}")

    if not pages:
        raise SystemExit("no glyph pages were produced")

    total = sum(len(page["glyphs"]) for page in pages)
    print(f"done: {len(pages)} page(s), {total} glyphs -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
