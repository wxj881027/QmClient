#!/usr/bin/env python3
"""预烘焙眼睛 morph 的 MSDF 中间帧（flipbook）。

为什么这样做：MSDF 的三个通道各自编码不同边的方向，不能运行时插值/平均；把中间形状
预先烤成 MSDF 帧，就能让形变走**与图标完全相同的抗锯齿路径**，不再依赖 FSAA，也不会
出现几何直出的亚像素散点。

几何来源：`qm_build_icon_morph` 的条带几何（表面内共享刚体参数、无整体塌缩，见该模块
说明）。每帧的条带四边形按统一绕向写成临时 TTF 的字形轮廓——TrueType 用非零环绕，
重叠多边形天然渲染为并集。

坐标：与 Phosphor 字形同一 em 空间（`qm_build_icon_morph.to_viewbox` 的逆映射
`x*4`、`960 - y*4`），因此帧与图标共享同一尺寸基准。
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import qm_build_icon_morph as morph  # noqa: E402

# 帧数：8 帧把 0..1 分成 7 段，配合相邻帧 alpha 混合，肉眼已无分段感。
FRAME_COUNT = 8
# 临时字体内部的私有码点，只用于烘焙，不进入运行时命名空间。
FRAME_CODEPOINT_BASE = 0xE000
FRAME_GLYPH_PREFIX = "morphframe"


def frame_progress_values(count: int = FRAME_COUNT) -> list[float]:
    if count < 2:
        raise ValueError("at least two frames are required")
    return [index / (count - 1) for index in range(count)]


def quad_signed_area(points: list[tuple[float, float]]) -> float:
    return 0.5 * sum(
        points[index][0] * points[(index + 1) % len(points)][1]
        - points[(index + 1) % len(points)][0] * points[index][1]
        for index in range(len(points))
    )


def frame_contours(surfaces, progress: float) -> list[list[tuple[float, float]]]:
    """一帧的全部条带四边形（视图框坐标，绕向统一为非零环绕安全的方向）。

    绕向必须一致：非零环绕下反向四边形会在重叠处抵消、把形状挖出空洞。
    """
    contours: list[list[tuple[float, float]]] = []
    for _, _, surface in surfaces:
        for index in range(morph.SAMPLE_COUNT):
            nxt = (index + 1) % morph.SAMPLE_COUNT
            quad = [
                morph.resolve_point(surface.outer, index, progress),
                morph.resolve_point(surface.outer, nxt, progress),
                morph.resolve_point(surface.inner, nxt, progress),
                morph.resolve_point(surface.inner, index, progress),
            ]
            if abs(quad_signed_area(quad)) <= 1e-6:
                continue
            if quad_signed_area(quad) < 0.0:
                quad.reverse()
            contours.append(quad)
    return contours


def to_font_units(point: tuple[float, float]) -> tuple[float, float]:
    """视图框坐标 -> 字体单位（与读取时的映射互为逆运算）。"""
    return point[0] * morph.FONT_UNITS_PER_EM / morph.VIEWBOX_SIZE, (
        morph.FONT_Y_FLIP - point[1] * morph.FONT_UNITS_PER_EM / morph.VIEWBOX_SIZE
    )


def build_frame_font(output_path: Path, frames: list[list[list[tuple[float, float]]]], font_name: str) -> list[int]:
    """把每帧的轮廓写成临时 TTF，返回帧对应的码点列表。"""
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen

    if not frames:
        raise ValueError("no frames to bake")

    glyph_order = [".notdef"]
    glyphs = {}
    pen = TTGlyphPen(None)
    glyphs[".notdef"] = pen.glyph()

    codepoints: list[int] = []
    for index, contours in enumerate(frames):
        name = f"{FRAME_GLYPH_PREFIX}{index}"
        glyph_order.append(name)
        pen = TTGlyphPen(None)
        for contour in contours:
            first = to_font_units(contour[0])
            pen.moveTo(first)
            for point in contour[1:]:
                pen.lineTo(to_font_units(point))
            pen.closePath()
        glyphs[name] = pen.glyph()
        codepoints.append(FRAME_CODEPOINT_BASE + index)

    cmap = {codepoint: f"{FRAME_GLYPH_PREFIX}{index}" for index, codepoint in enumerate(codepoints)}
    units_per_em = int(morph.FONT_UNITS_PER_EM)
    builder = FontBuilder(units_per_em, isTTF=True)
    builder.setupGlyphOrder(glyph_order)
    builder.setupCharacterMap(cmap)
    builder.setupGlyf(glyphs)
    builder.setupHorizontalMetrics({name: (units_per_em, 0) for name in glyph_order})
    ascent = int(morph.FONT_Y_FLIP)
    descent = ascent - units_per_em
    builder.setupHorizontalHeader(ascent=ascent, descent=descent)
    builder.setupNameTable({"familyName": font_name, "styleName": "Regular"})
    builder.setupOS2(
        sTypoAscender=ascent,
        sTypoDescender=descent,
        usWinAscent=ascent,
        usWinDescent=-descent,
    )
    builder.setupPost()
    builder.save(str(output_path))
    return codepoints


def glyph_frame_contours(fonts_dir: Path, codepoints: dict[str, int], name: str) -> list[list[tuple[float, float]]]:
    """真实字形轮廓（视图框坐标，**保留原始绕向**）。

    首末帧必须用真字形：几何 morph 在 p=1 的形状带扫掠过填，直接当作末帧会让
    动画结束时与静态图标之间跳一下。洞的挖除依赖原始绕向，因此这里不做绕向统一。
    """
    from fontTools.ttLib import TTFont

    font = TTFont(str(fonts_dir / morph.FONT_FILE))
    glyph_name = morph.ICON_NAME_ALIASES.get(name, name)
    if glyph_name not in codepoints:
        raise ValueError(f"unknown icon name: {name}")
    return [morph.to_viewbox(contour) for contour in morph.flatten_glyph_contours(font, codepoints[glyph_name])]


def build_frame_font_from_plan(
    output_path: Path,
    fonts_dir: Path,
    codepoints_path: Path,
    frame_count: int = FRAME_COUNT,
    font_name: str = "QmMorphFrames",
) -> tuple[list[int], list[float]]:
    """从 morph 几何生成临时字体，返回 (码点列表, 进度列表)。

    首末帧用真实字形，中间帧用几何 morph：端点与静态图标完全一致（形状、尺寸、
    绕向），中段仍是由几何驱动的连续形变。
    """
    surfaces = morph.build_plan(fonts_dir, codepoints_path)
    codepoints = morph.load_codepoints(codepoints_path)
    progress_values = frame_progress_values(frame_count)
    frames = [glyph_frame_contours(fonts_dir, codepoints, "eye")]
    frames.extend(frame_contours(surfaces, progress) for progress in progress_values[1:-1])
    frames.append(glyph_frame_contours(fonts_dir, codepoints, "eye-slash"))
    return build_frame_font(output_path, frames, font_name), progress_values
