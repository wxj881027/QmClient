#!/usr/bin/env python3
"""Validate packaged Phosphor Bold atlases and generate their runtime manifest."""

import argparse
import json
from pathlib import Path
import re
import struct


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate field: {key}")
        result[key] = value
    return result


def load_atlas(directory, scale):
    stem = f"qm_icons_bold_{scale}x"
    document = json.loads(
        (directory / f"{stem}.json").read_text(encoding="utf-8"),
        object_pairs_hook=unique_object,
    )
    if document.get("version") != 1 or document.get("scale") != scale:
        raise ValueError("unsupported manifest version or scale")
    atlas = document["atlas"]
    width, height = atlas["width"], atlas["height"]
    if type(width) is not int or type(height) is not int or not (
        0 < width <= 4096 and 0 < height <= 4096
    ):
        raise ValueError("invalid atlas dimensions")
    path = f"qmclient/icons/{stem}.png"
    if atlas["image"] != path:
        raise ValueError("atlas image path mismatch")
    with (directory / f"{stem}.png").open("rb") as image:
        header = image.read(24)
    if header[:16] != b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR":
        raise ValueError("invalid PNG header")
    if struct.unpack(">II", header[16:24]) != (width, height):
        raise ValueError("PNG dimensions do not match manifest")
    icons = document["icons"]
    if not icons or len(icons) > 256:
        raise ValueError("invalid icon count")
    normalized = {}
    for name, bounds in sorted(icons.items()):
        if not re.fullmatch(r"[a-z][a-z0-9]*(?:-[a-z0-9]+)*", name):
            raise ValueError(f"invalid stable icon ID: {name}")
        x, y, w, h = (bounds[key] for key in ("x", "y", "w", "h"))
        if any(type(value) is not int for value in (x, y, w, h)):
            raise ValueError(f"invalid coordinates: {name}")
        if not (x >= 0 and y >= 0 and w > 0 and h > 0 and x + w <= width and y + h <= height):
            raise ValueError(f"icon outside atlas: {name}")
        normalized[name] = (x / width, y / height, (x + w) / width, (y + h) / height)
    return path, normalized


def generate(directory):
    paths = []
    reference = None
    for scale in (1, 2, 4):
        path, icons = load_atlas(directory, scale)
        if reference is not None and reference != icons:
            raise ValueError("stable IDs or normalized bounds differ between atlas scales")
        reference = icons
        paths.append(path)
    lines = [
        "#ifndef GENERATED_QM_ICON_MANIFEST_H",
        "#define GENERATED_QM_ICON_MANIFEST_H",
        "#include <array>",
        "enum class EQmUiIcon",
        "{",
    ]
    lines += [f"\t{name.upper().replace('-', '_')}," for name in reference]
    lines += [
        "\tCOUNT,",
        "};",
        "struct SQmUiIcon",
        "{",
        "\tconst char *m_pId;",
        "\tfloat m_U0, m_V0, m_U1, m_V1;",
        "};",
        f"inline constexpr std::array<SQmUiIcon, {len(reference)}> g_aQmUiIcons = {{{{",
    ]
    for name, bounds in reference.items():
        coordinates = ", ".join(f"{value:.9f}f" for value in bounds)
        lines.append(f'\t{{"{name}", {coordinates}}},')
    lines += [
        "}};",
        "inline constexpr std::array<const char *, 3> g_apQmUiIconAtlasPaths = {{",
    ]
    lines += [f'\t"{path}",' for path in paths]
    lines += ["}};", "#endif", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    content = generate(args.source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(content, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
