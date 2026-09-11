#!/usr/bin/env python3
"""按 median(MSDF) 把已发布的图集还原成 ASCII / PNG，供人工核对正反与朝向。

只用于开发期自检（不属于资源管线）：只读已发布的 .png/.json，不修改任何资产。
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def median(rgb: tuple[int, int, int]) -> float:
    r, g, b = (value / 255.0 for value in rgb)
    return max(min(r, g), min(max(r, g), b))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True, help="已发布的图集 manifest (.json)")
    parser.add_argument("--glyph", required=True, help="要检视的字符")
    parser.add_argument("--ascii", action="store_true", help="打印 ASCII 预览")
    parser.add_argument("--png", type=Path, help="导出该字形的 PNG（放大后便于肉眼核对）")
    parser.add_argument("--scale", type=int, default=8)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    from PIL import Image

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    image_path = args.manifest.parent / Path(manifest["atlas"]["image"]).name
    atlas = Image.open(image_path).convert("RGBA")
    entry = manifest["glyphs"].get(str(ord(args.glyph)))
    if entry is None:
        raise SystemExit(f"U+{ord(args.glyph):04X} not in atlas")
    if not entry["outline"]:
        print(f"U+{ord(args.glyph):04X} has no outline (advance-only)")
        return 0

    tile = atlas.crop((entry["x"], entry["y"], entry["x"] + entry["w"], entry["y"] + entry["h"]))
    width, height = tile.size
    pixels = tile.load()
    field = [[median(pixels[x, y][:3]) for x in range(width)] for y in range(height)]

    inside = sum(1 for row in field for value in row if value > 0.5)
    print(
        f"U+{ord(args.glyph):04X} '{args.glyph}' tile={width}x{height} inside={inside} "
        f"adv={entry['adv']:.2f} bx={entry['bx']:.2f} by={entry['by']:.2f} atlas={atlas.size}"
    )

    if args.ascii:
        for row in field:
            print("".join("#" if value > 0.5 else ("+" if value > 0.2 else ".") for value in row))

    if args.png is not None:
        scale = max(1, args.scale)
        out = Image.new("RGBA", (width * scale, height * scale))
        out_pixels = out.load()
        for y in range(height):
            for x in range(width):
                alpha = round(255 * max(0.0, min(1.0, (field[y][x] - 0.5) * 2.0)))
                for sy in range(scale):
                    for sx in range(scale):
                        out_pixels[x * scale + sx, y * scale + sy] = (255, 255, 255, alpha)
        args.png.parent.mkdir(parents=True, exist_ok=True)
        out.save(args.png)
        print(f"wrote {args.png}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
