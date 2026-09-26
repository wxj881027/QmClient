#!/usr/bin/env python3
"""构建期生成 QmClient 眼睛 morph（eye ↔ eye-slash）的采样几何数据。

数据来源：随包 Phosphor-Bold.ttf 的字形轮廓。不再依赖 datasrc 的 SVG 管线——
那条链已随图标图集改为「从字体烘焙」而整体删除（见 qm_build_icon_msdf_font.py）。

几何契约：
- 坐标映射 x/4、(y+64)/4 无翻转，由 git 历史里的 datasrc/qm_icons/phosphor_bold
  SVG（viewBox 0 0 256 256）逐轮廓包围盒校准而来；两枚图标共用同一映射，
  morph 的源与目标因此自洽。
- 每个表面（surface）由外轮廓与内轮廓两条闭合轮廓组成：环形容器（眼眶、虹膜）
  内外成对，实心形状（右下眼睑）的内轮廓退化为锚点扇面。
- **同一表面共享一套刚体参数（θ、σ）**：只用外轮廓做 Procrustes 对齐，内轮廓
  仅在固定 θ/σ 下搜索起点对应。旧实现让内外轮廓各自对齐，动画中段两者旋转与
  缩放速率不同，把连接带拧成自相交碎片，实机上渲染为散点。
- **表面按索引配对，且外轮廓全程保持实几何**：旧实现按角色分组，虹膜环与眼睑
  各自整体塌缩/膨胀，64 条带在中段退化成亚像素碎片，同样渲染为散点。

输出：.inc 片段，被 src/game/client/qm_icon_morph.cpp 直接包含。
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

from fontTools.pens.basePen import decomposeQuadraticSegment
from fontTools.pens.recordingPen import RecordingPen
from fontTools.ttLib import TTFont

SAMPLE_COUNT = 64
VIEWBOX_SIZE = 256.0  # 历史 SVG 的 viewBox 尺寸，渲染器的归一化基准
VIEWBOX_CENTER = VIEWBOX_SIZE * 0.5

# 字体 -> SVG 视图框映射（Phosphor upm=1024）：x/4，y 翻转 (960 - y)/4。
#
# 校准依据是与 git 历史里的 datasrc/qm_icons/phosphor_bold SVG 逐轮廓比对：
# eye 上下对称，翻转与否都能对上包围盒（早期据此误判为「无需翻转」）；eye-slash 的
# 斜线不对称，只有翻转版能对上全部 3 条轮廓（最大误差 0.11；不翻转时 36.03）。
# 漏掉翻转会让目标图形上下镜像，morph 期间条带交叉、斜线方向相反。
FONT_UNITS_PER_EM = 1024.0
FONT_Y_FLIP = 960.0
FONT_SCALE = VIEWBOX_SIZE / FONT_UNITS_PER_EM

# 二次曲线展平的细分数：Phosphor 单段曲线最长约 200 单位，12 段已有亚像素精度。
CURVE_SUBDIVISIONS = 12

# 字重：UI 默认（Bold）是唯一有运行时样例的字重，其余字重由调用方回退。
WEIGHT = "bold"
FONT_FILE = "Phosphor-Bold.ttf"

# 图标名对齐官方 Phosphor：eye / eye-slash（运行时的旧枚举名 EQmIcon::EYE_OFF 保留）。
ICON_NAMES = ("eye", "eye-slash")
ICON_NAME_ALIASES = {"eye-off": "eye-slash"}

# 表面拓扑（由字形轮廓渲染核对得到）：
#   eye       : 轮廓 0/1 = 眼眶环，2/3 = 虹膜环
#   eye-slash : 轮廓 0/1 = 斜线∪上左眼睑环，2 = 右下眼睑
# 元组含义：(种类, 外轮廓下标, 内轮廓下标)。
#   ring : 外/内是两条独立闭合轮廓（眼眶环、虹膜环）
#   band : 只有一条闭合轮廓，其外弧与内弧同属这条线（新月形眼睑）——在两端尖点处
#          切成两条弧分别作外/内边界；若按实心形状用退化内轮廓填充，会把月牙的
#          凹侧一并填上，动画端点比图标多出约四分之一面积。
SURFACE_LAYOUT: dict[str, tuple[tuple[str, int, int | None], ...]] = {
    "eye": (("ring", 0, 1), ("ring", 2, 3)),
    "eye-slash": (("ring", 0, 1), ("band", 2, None)),
}

EXPECTED_CONTOUR_COUNTS = {"eye": 4, "eye-slash": 3}

# 表面按**索引**配对（源第 i 表面对应目标第 i 表面），不做角色配对：
#   [0] 眼眶环 → 斜线∪上左眼睑环
#   [1] 虹膜环 → 右下眼睑带
# 旧实现按角色分组，导致虹膜环找不到 ring 对角而整体塌缩成一点、眼睑又从一点膨胀：
# 整个表面同时收缩时，64 条带全部退化成亚像素碎片，实机上渲染为散点。按索引配对后
# 每个表面两侧都是实几何，条带不会整体塌缩。


class PathPair:
    """一条轮廓的插值参数：源/目标残差坐标 + 共享的刚体变换。"""

    __slots__ = ("source", "target", "source_center", "target_center", "theta", "log_scale")

    def __init__(
        self,
        source: list[tuple[float, float]],
        target: list[tuple[float, float]],
        source_center: tuple[float, float],
        target_center: tuple[float, float],
        theta: float,
        log_scale: float,
    ) -> None:
        self.source = source
        self.target = target
        self.source_center = source_center
        self.target_center = target_center
        self.theta = theta
        self.log_scale = log_scale


class SurfacePair:
    """一个表面：外轮廓与内轮廓共用刚体参数（见模块头说明）。"""

    __slots__ = ("outer", "inner")

    def __init__(self, outer: PathPair, inner: PathPair) -> None:
        self.outer = outer
        self.inner = inner


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="从 Phosphor 字体生成眼睛 morph 采样数据")
    parser.add_argument("--fonts-dir", type=Path, required=True, help="Phosphor TTF 所在目录")
    parser.add_argument("--codepoints", type=Path, required=True, help="官方 name->codepoint 映射文件")
    parser.add_argument("--output", type=Path, required=True, help="输出的 .inc 路径")
    return parser.parse_args()


def load_codepoints(path: Path) -> dict[str, int]:
    icons: dict[str, int] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, codepoint = line.split()
        icons[name] = int(codepoint, 16)
    return icons


def distance(a: tuple[float, float], b: tuple[float, float]) -> float:
    return math.hypot(a[0] - b[0], a[1] - b[1])


def centroid(points: list[tuple[float, float]]) -> tuple[float, float]:
    if not points:
        return VIEWBOX_CENTER, VIEWBOX_CENTER
    return (
        sum(point[0] for point in points) / len(points),
        sum(point[1] for point in points) / len(points),
    )


def quadratic_point(
    start: tuple[float, float],
    control: tuple[float, float],
    end: tuple[float, float],
    t: float,
) -> tuple[float, float]:
    inv = 1.0 - t
    return (
        inv * inv * start[0] + 2.0 * inv * t * control[0] + t * t * end[0],
        inv * inv * start[1] + 2.0 * inv * t * control[1] + t * t * end[1],
    )


def flatten_glyph_contours(font: TTFont, codepoint: int) -> list[list[tuple[float, float]]]:
    """提取字形轮廓并展平为折线（TrueType 二次曲线按 CURVE_SUBDIVISIONS 细分）。"""
    cmap = font.getBestCmap()
    glyph_name = cmap.get(codepoint)
    if glyph_name is None:
        raise ValueError(f"Phosphor font does not contain U+{codepoint:04X}")

    pen = RecordingPen()
    font.getGlyphSet()[glyph_name].draw(pen)

    contours: list[list[tuple[float, float]]] = []
    current: list[tuple[float, float]] | None = None
    for op, points in pen.value:
        if op == "moveTo":
            current = [points[0]]
        elif op == "lineTo":
            if current is None:
                raise ValueError("glyph contour must start with moveTo")
            current.append(points[0])
        elif op == "qCurveTo":
            if current is None:
                raise ValueError("glyph contour must start with moveTo")
            for control, end in decomposeQuadraticSegment(points):
                start = current[-1]
                for step in range(1, CURVE_SUBDIVISIONS + 1):
                    current.append(quadratic_point(start, control, end, step / CURVE_SUBDIVISIONS))
        elif op == "curveTo":
            raise ValueError("Phosphor glyphs must not contain cubic segments")
        elif op == "closePath":
            if current is not None and len(current) >= 3:
                contours.append(current)
            current = None
        else:
            raise ValueError(f"unsupported glyph operator: {op}")
    if current is not None and len(current) >= 3:
        contours.append(current)
    return contours


def to_viewbox(points: list[tuple[float, float]]) -> list[tuple[float, float]]:
    # 字体是 y 向上、视图框是 y 向下，必须翻转；偏移 960 让字形落在 0..256 框内。
    return [(x * FONT_SCALE, (FONT_Y_FLIP - y) * FONT_SCALE) for x, y in points]


def signed_area(points: list[tuple[float, float]]) -> float:
    return 0.5 * sum(
        a[0] * b[1] - b[0] * a[1]
        for a, b in zip(points, points[1:] + [points[0]])
    )


def reverse_points(points: list[tuple[float, float]]) -> list[tuple[float, float]]:
    return list(reversed(points))


def rotate_points(points: list[tuple[float, float]], offset: int) -> list[tuple[float, float]]:
    return points[offset:] + points[:offset]


def canonicalize_contour(points: list[tuple[float, float]]) -> list[tuple[float, float]]:
    # 条带顶点要求两条边界沿同一方向行走；使用稳定的最右顶点作为起点，
    # 避免独立的轮廓在插值时扭转。
    if signed_area(points) > 0.0:
        points = reverse_points(points)
    center = centroid(points)
    start = max(
        range(len(points)),
        key=lambda index: (points[index][0], -abs(points[index][1] - center[1])),
    )
    return rotate_points(points, start)


def sample_closed(points: list[tuple[float, float]]) -> list[tuple[float, float]]:
    if len(points) < 3:
        raise ValueError("icon contour must be a closed polygon")
    if points[0] == points[-1]:
        points = points[:-1]
    lengths = [0.0]
    for a, b in zip(points, points[1:] + [points[0]]):
        lengths.append(lengths[-1] + distance(a, b))
    total = lengths[-1]
    if total <= 1e-12:
        return [points[0]] * SAMPLE_COUNT

    sampled: list[tuple[float, float]] = []
    for index in range(SAMPLE_COUNT):
        target = total * index / SAMPLE_COUNT
        segment = next(
            (i for i in range(len(points)) if lengths[i + 1] >= target),
            len(points) - 1,
        )
        segment_end = lengths[segment + 1]
        fraction = (target - lengths[segment]) / max(segment_end - lengths[segment], 1e-9)
        a = points[segment]
        b = points[(segment + 1) % len(points)]
        sampled.append((a[0] + (b[0] - a[0]) * fraction, a[1] + (b[1] - a[1]) * fraction))
    return sampled


def read_icon(fonts_dir: Path, codepoints: dict[str, int], name: str) -> list[list[tuple[float, float]]]:
    """读取一枚图标的全部轮廓（已归一化到 256 视图框、按弧长重采样到 SAMPLE_COUNT）。"""
    font = TTFont(str(fonts_dir / FONT_FILE))
    glyph_name = ICON_NAME_ALIASES.get(name, name)
    if glyph_name not in codepoints:
        raise ValueError(f"unknown icon name: {name}")
    contours = [
        sample_closed(canonicalize_contour(to_viewbox(contour)))
        for contour in flatten_glyph_contours(font, codepoints[glyph_name])
    ]
    expected = EXPECTED_CONTOUR_COUNTS.get(name)
    if expected is not None and len(contours) != expected:
        raise ValueError(f"{name} expects {expected} contours, got {len(contours)}")
    return contours


def procrustes(
    source: list[tuple[float, float]],
    target: list[tuple[float, float]],
    source_center: tuple[float, float],
    target_center: tuple[float, float],
) -> tuple[float, float, float]:
    """最优相似变换（无镜像）：target 经旋转缩放后对齐 source，返回 (θ, σ, 残差)。"""
    sxx = sxy = syx = syy = 0.0
    norm_source = norm_target = 0.0
    for (source_x, source_y), (target_x, target_y) in zip(source, target):
        source_x -= source_center[0]
        source_y -= source_center[1]
        target_x -= target_center[0]
        target_y -= target_center[1]
        sxx += source_x * target_x
        sxy += source_x * target_y
        syx += source_y * target_x
        syy += source_y * target_y
        norm_source += source_x * source_x + source_y * source_y
        norm_target += target_x * target_x + target_y * target_y

    if norm_source <= 1e-12 or norm_target <= 1e-12:
        return 0.0, 1.0, 0.0

    theta = math.atan2(sxy - syx, sxx + syy)
    numerator = math.cos(theta) * (sxx + syy) + math.sin(theta) * (sxy - syx)
    sigma = max(numerator / norm_source, 1e-6)
    residual = max(sigma * sigma * norm_source - 2.0 * sigma * numerator + norm_target, 0.0)
    return theta, sigma, math.sqrt(residual / norm_target)


def residual_with_transform(
    source: list[tuple[float, float]],
    target: list[tuple[float, float]],
    source_center: tuple[float, float],
    target_center: tuple[float, float],
    theta: float,
    sigma: float,
) -> float:
    """固定 θ/σ 时的对齐残差，用于内轮廓在共享刚体参数下搜索起点。"""
    cos_angle = math.cos(theta) * sigma
    sin_angle = math.sin(theta) * sigma
    offset_x = source_center[0] - (target_center[0] * cos_angle - target_center[1] * sin_angle)
    offset_y = source_center[1] - (target_center[0] * sin_angle + target_center[1] * cos_angle)
    error = 0.0
    norm_source = 0.0
    for (source_x, source_y), (target_x, target_y) in zip(source, target):
        error += (
            (offset_x + target_x * cos_angle - target_y * sin_angle - source_x) ** 2
            + (offset_y + target_x * sin_angle + target_y * cos_angle - source_y) ** 2
        )
        norm_source += (source_x - source_center[0]) ** 2 + (source_y - source_center[1]) ** 2
    if norm_source <= 1e-12:
        return 0.0
    return math.sqrt(error / norm_source)


def best_rotation(
    source: list[tuple[float, float]],
    target: list[tuple[float, float]],
    source_center: tuple[float, float],
    target_center: tuple[float, float],
    fixed: tuple[float, float] | None,
) -> tuple[list[tuple[float, float]], float, float]:
    """在 64 个起点里搜最优对应；fixed 给定时锁定 θ/σ（内轮廓路径）。"""
    best_score = float("inf")
    best_target = target
    best_theta = fixed[0] if fixed is not None else 0.0
    best_sigma = fixed[1] if fixed is not None else 1.0
    for offset in range(SAMPLE_COUNT):
        candidate = rotate_points(target, offset)
        if fixed is None:
            theta, sigma, residual = procrustes(source, candidate, source_center, target_center)
            score = residual + 0.05 * abs(theta) / math.pi
        else:
            theta, sigma = fixed
            score = residual_with_transform(source, candidate, source_center, target_center, theta, sigma)
        if score < best_score:
            best_score = score
            best_target = candidate
            best_theta = theta
            best_sigma = sigma
    return best_target, best_theta, best_sigma


def collapse_to(center: tuple[float, float]) -> list[tuple[float, float]]:
    """退化轮廓：全部采样点落在锚点上。锚点取成对轮廓的质心，让条带收成扇面而非糊成一片。"""
    return [center] * SAMPLE_COUNT


def sample_open(points: list[tuple[float, float]]) -> list[tuple[float, float]]:
    """按弧长把一条开放折线重采样成 SAMPLE_COUNT 个点（含两端）。"""
    if len(points) < 2:
        raise ValueError("band arc must contain at least two points")
    lengths = [0.0]
    for a, b in zip(points, points[1:]):
        lengths.append(lengths[-1] + distance(a, b))
    total = lengths[-1]
    if total <= 1e-12:
        return [points[0]] * SAMPLE_COUNT

    sampled: list[tuple[float, float]] = []
    for index in range(SAMPLE_COUNT):
        target = total * index / (SAMPLE_COUNT - 1)
        segment = next(
            (i for i in range(len(points) - 1) if lengths[i + 1] >= target),
            len(points) - 2,
        )
        segment_end = lengths[segment + 1]
        fraction = (target - lengths[segment]) / max(segment_end - lengths[segment], 1e-9)
        a = points[segment]
        b = points[segment + 1]
        sampled.append((a[0] + (b[0] - a[0]) * fraction, a[1] + (b[1] - a[1]) * fraction))
    return sampled


def split_band_contour(contour: list[tuple[float, float]]) -> tuple[list[tuple[float, float]], list[tuple[float, float]]]:
    """把一条「带状」闭合轮廓（新月形眼睑）切成外弧与内弧。

    切点取距离最远的一对采样点（新月两端的尖点）。外弧取离整条轮廓质心更远的一条：
    新月的外弧更鼓、内弧更平，用质心距离能稳定区分。两条弧都重采样为
    SAMPLE_COUNT 点，并统一从同一个尖点走向另一个尖点，使配对方向一致。
    """
    count = SAMPLE_COUNT
    best_pair = (0, 0)
    best_distance = -1.0
    for first in range(count):
        for second in range(first + 1, count):
            current = distance(contour[first], contour[second])
            if current > best_distance:
                best_distance = current
                best_pair = (first, second)

    first, second = best_pair
    arc_a = [contour[(first + step) % count] for step in range(second - first + 1)]
    arc_b = [contour[(second + step) % count] for step in range(count - (second - first) + 1)]
    sampled_a = sample_open(arc_a)
    sampled_b = sample_open(arc_b)

    center = centroid(contour)
    mean_a = sum(distance(point, center) for point in sampled_a) / count
    mean_b = sum(distance(point, center) for point in sampled_b) / count
    if mean_a >= mean_b:
        outer, inner = sampled_a, list(reversed(sampled_b))
    else:
        outer, inner = sampled_b, list(reversed(sampled_a))
    return outer, inner


def resolve_surface_contours(
    contours: list[list[tuple[float, float]]],
    layout_entry: tuple[str, int, int | None],
) -> tuple[list[tuple[float, float]] | None, list[tuple[float, float]] | None]:
    """按 layout 把轮廓解析成表面的外/内边界。"""
    kind, outer_index, inner_index = layout_entry
    if kind == "band":
        outer, inner = split_band_contour(contours[outer_index])
        return outer, inner
    if kind == "ring":
        return contours[outer_index], (contours[inner_index] if inner_index is not None else None)
    raise ValueError(f"unknown surface kind: {kind}")


def make_path_pair(
    source: list[tuple[float, float]],
    target: list[tuple[float, float]],
    transform: tuple[float, float] | None = None,
) -> PathPair:
    """生成一条轮廓的插值参数；transform 给定时复用外轮廓的 θ/σ。"""
    source_center = centroid(source)
    target_center = centroid(target)
    aligned_target, theta, sigma = best_rotation(source, target, source_center, target_center, transform)
    cos_angle = math.cos(-theta)
    sin_angle = math.sin(-theta)
    source_residual = [(x - source_center[0], y - source_center[1]) for x, y in source]
    target_residual = [
        (
            ((x - target_center[0]) * cos_angle - (y - target_center[1]) * sin_angle) / sigma,
            ((x - target_center[0]) * sin_angle + (y - target_center[1]) * cos_angle) / sigma,
        )
        for x, y in aligned_target
    ]
    return PathPair(source_residual, target_residual, source_center, target_center, theta, math.log(sigma))


def align_surface(
    source_outer: list[tuple[float, float]] | None,
    target_outer: list[tuple[float, float]] | None,
    source_inner: list[tuple[float, float]] | None,
    target_inner: list[tuple[float, float]] | None,
) -> SurfacePair:
    """对齐一个表面：外轮廓决定刚体参数，内轮廓复用同一套。

    缺失的外轮廓退化为视图中心，缺失的内轮廓退化为**成对轮廓的质心**——后者让实心
    形状收成以自身为锚点的扇面，避免条带整体塌缩成亚像素碎片（实机散点的成因之一）。
    """
    source_outer_points = source_outer if source_outer is not None else collapse_to((VIEWBOX_CENTER, VIEWBOX_CENTER))
    target_outer_points = target_outer if target_outer is not None else collapse_to((VIEWBOX_CENTER, VIEWBOX_CENTER))
    outer = make_path_pair(source_outer_points, target_outer_points)

    inner_transform = (outer.theta, math.exp(outer.log_scale))
    source_inner_points = source_inner if source_inner is not None else collapse_to(centroid(source_outer_points))
    target_inner_points = target_inner if target_inner is not None else collapse_to(centroid(target_outer_points))
    inner = make_path_pair(source_inner_points, target_inner_points, inner_transform)
    return SurfacePair(outer, inner)


def build_plan(fonts_dir: Path, codepoints_path: Path) -> list[tuple[str, int, SurfacePair]]:
    """按 SURFACE_LAYOUT 组装 bold 字重的全部表面（按索引配对），返回 (角色, 序号, 表面)。"""
    codepoints = load_codepoints(codepoints_path)
    source_contours = read_icon(fonts_dir, codepoints, "eye")
    target_contours = read_icon(fonts_dir, codepoints, "eye-slash")
    source_layout = SURFACE_LAYOUT["eye"]
    target_layout = SURFACE_LAYOUT["eye-slash"]
    if len(source_layout) != len(target_layout):
        raise ValueError("eye and eye-slash must expose the same number of surfaces for index pairing")

    surfaces: list[tuple[str, int, SurfacePair]] = []
    for index, (source_surface, target_surface) in enumerate(zip(source_layout, target_layout)):
        source_outer, source_inner = resolve_surface_contours(source_contours, source_surface)
        target_outer, target_inner = resolve_surface_contours(target_contours, target_surface)
        surfaces.append(
            (
                source_surface[0],
                index,
                align_surface(source_outer, target_outer, source_inner, target_inner),
            )
        )
    return surfaces


def resolve_point(path: PathPair, index: int, progress: float) -> tuple[float, float]:
    """与运行时 ResolveQmIconMorphPoint 等价的取点（帧生成/离线校验共用）。"""
    progress = max(-0.25, min(1.25, progress))
    clamped = max(0, min(SAMPLE_COUNT - 1, index))
    source_x, source_y = path.source[clamped]
    target_x, target_y = path.target[clamped]
    residual_x = source_x + (target_x - source_x) * progress
    residual_y = source_y + (target_y - source_y) * progress
    scale = math.exp(path.log_scale * progress)
    angle = path.theta * progress
    center_x = path.source_center[0] + (path.target_center[0] - path.source_center[0]) * progress
    center_y = path.source_center[1] + (path.target_center[1] - path.source_center[1]) * progress
    return (
        center_x + (residual_x * math.cos(angle) - residual_y * math.sin(angle)) * scale,
        center_y + (residual_x * math.sin(angle) + residual_y * math.cos(angle)) * scale,
    )


def format_float(value: float) -> str:
    if abs(value) < 0.0000005:
        value = 0.0
    return f"{value:.7f}f"


def format_points(points: list[tuple[float, float]]) -> str:
    return ", ".join(format_float(component) for point in points for component in point)


def format_path_data(name: str, path: PathPair, lines: list[str]) -> str:
    source_name = f"{name}_source"
    target_name = f"{name}_target"
    lines.append(f"static constexpr float {source_name}[{SAMPLE_COUNT * 2}] = {{{format_points(path.source)}}};")
    lines.append(f"static constexpr float {target_name}[{SAMPLE_COUNT * 2}] = {{{format_points(path.target)}}};")
    return (
        "{"
        + ", ".join(
            [
                source_name,
                target_name,
                format_float(path.source_center[0]),
                format_float(path.source_center[1]),
                format_float(path.target_center[0]),
                format_float(path.target_center[1]),
                format_float(path.theta),
                format_float(path.log_scale),
            ]
        )
        + "}"
    )


def generate(fonts_dir: Path, codepoints_path: Path) -> str:
    lines = [
        "// 由 qmclient_scripts/qm_build_icon_morph.py 从 Phosphor 字体生成，请勿手动编辑。",
        "",
    ]
    entries: list[str] = []
    for role, index, surface in build_plan(fonts_dir, codepoints_path):
        prefix = f"s_aEyeMorph_{WEIGHT}_{role}_{index}"
        outer = format_path_data(f"{prefix}_outer", surface.outer, lines)
        inner = format_path_data(f"{prefix}_inner", surface.inner, lines)
        entries.append("\t{" + outer + ", " + inner + "},")
        lines.append("")

    lines.append(f"static constexpr SQmIconMorphSurfaceData s_aEyeMorph_{WEIGHT}_surfaces[] = {{")
    lines.extend(entries)
    lines.append("};")
    lines.append(
        f"static constexpr SQmIconMorphPlan s_EyeMorphPlan_{WEIGHT} = "
        f"{{s_aEyeMorph_{WEIGHT}_surfaces, static_cast<int>(sizeof(s_aEyeMorph_{WEIGHT}_surfaces) / "
        f"sizeof(s_aEyeMorph_{WEIGHT}_surfaces[0]))}};"
    )
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generate(args.fonts_dir, args.codepoints), encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
