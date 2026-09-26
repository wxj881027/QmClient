#!/usr/bin/env python3
"""Run the pinned official msdf-atlas-gen tool and emit one QmClient icon page.

The client consumes the small QmClient manifest format, while the generator
uses the upstream JSON layout. This adapter keeps the baking tool replaceable
and makes the exact generator command reproducible for every published page.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path

from PIL import Image


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--font", type=Path, required=True)
    parser.add_argument("--charset", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="QmClient manifest output path")
    parser.add_argument("--image", type=Path, required=True, help="QmClient PNG output path")
    parser.add_argument("--font-name", default="")
    parser.add_argument("--font-index", type=int, default=None)
    parser.add_argument("--size", type=int, default=64)
    parser.add_argument("--px-range", type=int, default=8)
    parser.add_argument("--padding", type=int, default=9)
    parser.add_argument("--width", type=int, default=2048)
    parser.add_argument("--height", type=int, default=2048)
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.image.parent.mkdir(parents=True, exist_ok=True)
    raw = args.output.with_suffix(".bin")
    upstream = args.output.with_suffix(".upstream.json")

    font_path = args.font
    temp_font = None
    if args.font_index is not None and args.font.suffix.lower() == ".ttc":
        from fontTools.ttLib import TTCollection

        temp_font = tempfile.NamedTemporaryFile(suffix=".ttf", delete=False)
        temp_font.close()
        collection = TTCollection(str(args.font))
        if args.font_index < 0 or args.font_index >= len(collection.fonts):
            raise SystemExit(f"font index out of range: {args.font_index}")
        collection.fonts[args.font_index].save(temp_font.name)
        font_path = Path(temp_font.name)

    command = [
        str(args.tool),
        "-font",
        str(font_path),
        "-charset",
        str(args.charset),
        "-type",
        "mtsdf",
        "-format",
        "bin",
        "-size",
        str(args.size),
        "-pxrange",
        str(args.px_range),
        "-pxpadding",
        str(args.padding),
        "-yorigin",
        "top",
        "-dimensions",
        str(args.width),
        str(args.height),
        "-overlap",
        "-scanline",
        "-coloringstrategy",
        "simple",
        "-imageout",
        str(raw),
        "-json",
        str(upstream),
    ]
    if args.font_name:
        command.extend(["-fontname", args.font_name])
    try:
        subprocess.run(command, check=True)
    finally:
        if temp_font is not None:
            Path(temp_font.name).unlink(missing_ok=True)
    source = json.loads(upstream.read_text(encoding="utf-8"))
    atlas = source["atlas"]
    size = float(atlas["size"])
    glyphs = {}
    for glyph in source.get("glyphs", []):
        if "unicode" not in glyph or "atlasBounds" not in glyph or "planeBounds" not in glyph:
            continue
        bounds = glyph["atlasBounds"]
        plane = glyph["planeBounds"]
        glyphs[str(int(glyph["unicode"]))] = {
            "x": round(float(bounds["left"])),
            "y": round(float(bounds["top"])),
            "w": round(float(bounds["right"]) - float(bounds["left"])),
            "h": round(float(bounds["bottom"]) - float(bounds["top"])),
            "adv": float(glyph["advance"]) * size,
            "bx": float(plane["left"]) * size,
            "by": -float(plane["top"]) * size,
            "outline": True,
        }

    raw_bytes = raw.read_bytes()
    expected = args.width * args.height * 4
    if len(raw_bytes) != expected:
        raise SystemExit(f"unexpected official RGBA size: {len(raw_bytes)} != {expected}")
    Image.frombytes("RGBA", (args.width, args.height), raw_bytes).save(args.image)

    metrics = source["metrics"]
    manifest = {
        "version": 1,
        "kind": "msdf-glyphs",
        "distance_field": "mtsdf",
        "alpha_sdf": True,
        "px_range": float(atlas["distanceRange"]),
        "em_pixels": size,
        "padding": args.padding,
        "source_font": args.font_name or str(args.font),
        "generator": "msdf-atlas-gen@6148900d59423059bafde2f51a0cb303184404bd",
        "atlas": {"image": str(args.image).replace("\\", "/"), "width": args.width, "height": args.height},
        "ascent": -float(metrics["ascender"]) * size,
        "descent": float(metrics["descender"]) * size,
        "glyphs": glyphs,
    }
    args.output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"generated {len(glyphs)} glyphs: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
