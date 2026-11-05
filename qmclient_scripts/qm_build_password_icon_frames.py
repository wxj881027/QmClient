"""生成 data/qmclient/ui/password_icon_<weight>.png —— 密码显隐图标的形变帧条。

输入是仓库自带的 Phosphor 字体（不下载 SVG）：字形轮廓就是唯一几何来源，
因此帧条与字重、字形版本天然一致。

为什么不是逐点插值：Phosphor Bold 的 eye 是 4 条轮廓（眼眶外/内 + 瞳孔外/内），
eye-slash 是 3 条轮廓（眼眶被斜杠切断、瞳孔整体消失）。两者拓扑不同，
按点数重采样或按距离场插值都会在中间帧把瞳孔插成碎片。这里改用语义分解：

    瞳孔   : 缩放 1 -> 0            （t = 0.00 ~ 0.50）
    斜杠   : 按笔画长度生长          （t = 0.05 ~ 0.85）
    眼眶   : 保持不动
    末段   : 与真实 eye-slash 字形交叉落地（t = 0.85 ~ 1.00）

末段落地是必要的：真实字形里眼眶被斜杠切断，而生长过程画的是「眼眶 + 斜杠」
的并集，末段交叉可以掩盖切断处的差异。

用法：
    python qmclient_scripts/qm_build_password_icon_frames.py
    python qmclient_scripts/qm_build_password_icon_frames.py --probe
    python qmclient_scripts/qm_build_password_icon_frames.py --preview tmp/password_icon_preview.png
"""

from __future__ import annotations

import argparse
import math
import sys
from dataclasses import dataclass
from pathlib import Path

from fontTools.pens.basePen import BasePen
from fontTools.ttLib import TTFont
from PIL import Image, ImageChops

EYE = 0xE220
EYE_OFF = 0xE224

PLAY = 0xE3D0
PAUSE = 0xE39E
STAR_FAVORITE = 0xE46A

# 与 qm_icon.h 的 NormalizeQmIconWeight 保持一致：0=Light 1=Bold 2=Light 3=Regular 4=Fill。
WEIGHTS = {
	"light": "Phosphor-Light.ttf",
	"regular": "Phosphor-Regular.ttf",
	"bold": "Phosphor-Bold.ttf",
	"fill": "Phosphor-Fill.ttf",
}

DEFAULT_SIZE = 64
# 多档烘焙。档位 = 绘制边长的约 2 倍最接近字体渲染：位图缩小时边缘会被平均成
# 「更多、更暗」的像素，而字体在小字号下 hinting 会把笔画对齐成「更少、更亮」的
# 整像素；2 倍重采样正好抵消这一正一反。实测 1.1 倍太细太灰、4.4 倍明显偏粗，
# 2 倍附近与字形观感一致。64 档沿用无后缀文件名，其余带 _<像素> 后缀。
DEFAULT_SIZES = (24, 32, 48, 64)
DEFAULT_FRAMES = 24
SUPERSAMPLE = 4
# 瞳孔判定：bbox 对角线小于整字 bbox 对角线的该比例。眼眶环永远更大。
PUPIL_DIAGONAL_RATIO = 0.55
# 画布：以并集墨迹中心为原点、边长固定为 CANVAS_UNITS 的正方形（upm 1024 的 1.125 倍）。
# 固定边长让运行时不依赖字重，直接用 side = FontSize * CANVAS_UNITS / 1024 定位；
# 以墨迹中心为原点则与文本渲染器按字形高度居中的行为一致。
CANVAS_UNITS = 1152

# 动画分段区间。
PUPIL_RANGE = (0.00, 0.50)
SLASH_RANGE = (0.05, 0.85)
LANDING_RANGE = (0.85, 1.00)

# 旋转对：同一个字形绕墨迹中心转 0 → degrees。用于 chevron 上下、arrows 左右这类
# 「本来就是同一形状换个朝向」的图标——不需要轮廓配对，也不需要语义脚本。
# (资产名, 码位, 旋转角度)
ROTATE_PAIRS = [
	("chevron", 0xE136, 180.0), # caret-down → caret-up
]


class FlattenPen(BasePen):
	"""把字形轮廓拍平成折线。曲线按固定段数细分，保证同字重不同字形细分一致。"""

	def __init__(self, glyphSet, steps: int = 10):
		super().__init__(glyphSet)
		self.steps = steps
		self.contours: list[list[tuple[float, float]]] = []
		self._current: list[tuple[float, float]] | None = None

	def _moveTo(self, pt):
		self._current = [pt]

	def _lineTo(self, pt):
		self._current.append(pt)

	def _curveToOne(self, p1, p2, p3):
		p0 = self._current[-1]
		for i in range(1, self.steps + 1):
			t = i / self.steps
			mt = 1.0 - t
			self._current.append((
				mt**3 * p0[0] + 3 * mt**2 * t * p1[0] + 3 * mt * t**2 * p2[0] + t**3 * p3[0],
				mt**3 * p0[1] + 3 * mt**2 * t * p1[1] + 3 * mt * t**2 * p2[1] + t**3 * p3[1],
			))

	def _qCurveToOne(self, p1, p2):
		p0 = self._current[-1]
		for i in range(1, self.steps + 1):
			t = i / self.steps
			mt = 1.0 - t
			self._current.append((
				mt**2 * p0[0] + 2 * mt * t * p1[0] + t**2 * p2[0],
				mt**2 * p0[1] + 2 * mt * t * p1[1] + t**2 * p2[1],
			))

	def _closePath(self):
		if self._current and len(self._current) > 2:
			self.contours.append(self._current)
		self._current = None

	def _endPath(self):
		self._closePath()


def load_contours(font_path: Path, codepoint: int, steps: int = 10):
	font = TTFont(font_path)
	glyph_set = font.getGlyphSet()
	name = font.getBestCmap()[codepoint]
	pen = FlattenPen(glyph_set, steps)
	glyph_set[name].draw(pen)
	return pen.contours, font["head"].unitsPerEm


def bbox(contours):
	xs = [p[0] for c in contours for p in c]
	ys = [p[1] for c in contours for p in c]
	return min(xs), min(ys), max(xs), max(ys)


def diagonal(box):
	return math.hypot(box[2] - box[0], box[3] - box[1])


def area(contour):
	total = 0.0
	for i in range(len(contour)):
		x0, y0 = contour[i]
		x1, y1 = contour[(i + 1) % len(contour)]
		total += x0 * y1 - x1 * y0
	return total * 0.5


def smoothstep(x: float) -> float:
	x = max(0.0, min(1.0, x))
	return x * x * (3.0 - 2.0 * x)


def segment_progress(t: float, span) -> float:
	start, end = span
	if end <= start:
		return 1.0
	return smoothstep((t - start) / (end - start))


@dataclass
class PairPlan:
	almond: list
	pupil: list
	pupil_center: tuple[float, float]
	slash_from: tuple[float, float]
	slash_to: tuple[float, float]
	slash_radius: float
	axis_normal: tuple[float, float]
	axis_base: float
	axis_length: float


def build_plan(eye, eye_off) -> PairPlan:
	glyph_box = bbox(eye)
	glyph_diag = diagonal(glyph_box)
	threshold = glyph_diag * PUPIL_DIAGONAL_RATIO

	pupil = []
	almond = []
	for contour in eye:
		(pupil if diagonal(bbox([contour])) < threshold else almond).append(contour)
	if not pupil:
		raise ValueError("瞳孔轮廓识别失败：该字重的 eye 可能没有独立的瞳孔环")
	if len(almond) < 2:
		raise ValueError("眼眶轮廓识别失败：该字重的 eye 可能已是实心字形")

	almond_outer = max(almond, key=lambda c: abs(area(c)))
	almond_inner = min(almond, key=lambda c: abs(area(c)))
	outer_h = bbox([almond_outer])[3] - bbox([almond_outer])[1]
	inner_h = bbox([almond_inner])[3] - bbox([almond_inner])[1]
	stroke = max(1.0, (outer_h - inner_h) * 0.5)
	slash_radius = stroke * 0.5

	pupil_box = bbox(pupil)
	pupil_center = ((pupil_box[0] + pupil_box[2]) * 0.5, (pupil_box[1] + pupil_box[3]) * 0.5)

	outer_contour = max(eye_off, key=lambda c: abs(area(c)))
	low = min(outer_contour, key=lambda p: p[1])
	high = max(outer_contour, key=lambda p: p[1])
	axis = (high[0] - low[0], high[1] - low[1])
	axis_length = math.hypot(*axis)
	if axis_length < diagonal(bbox(eye_off)) * 0.5:
		raise ValueError("斜杠轴线识别失败：端点间距过小")
	normal = (axis[0] / axis_length, axis[1] / axis_length)

	return PairPlan(
		almond=almond,
		pupil=pupil,
		pupil_center=pupil_center,
		slash_from=low,
		slash_to=high,
		slash_radius=slash_radius,
		axis_normal=normal,
		axis_base=normal[0] * low[0] + normal[1] * low[1],
		axis_length=axis_length,
	)


def scale_contour(contour, factor: float, center):
	cx, cy = center
	return [((x - cx) * factor + cx, (y - cy) * factor + cy) for x, y in contour]


def capsule(p0, p1, radius: float, cap_steps: int = 10):
	(x0, y0), (x1, y1) = p0, p1
	dx, dy = x1 - x0, y1 - y0
	length = math.hypot(dx, dy)
	ux, uy = dx / length, dy / length
	nx, ny = -uy, ux
	points = []
	for i in range(cap_steps + 1):
		a = math.pi * i / cap_steps
		points.append((x1 + radius * (nx * math.cos(a) + ux * math.sin(a)),
				y1 + radius * (ny * math.cos(a) + uy * math.sin(a))))
	for i in range(cap_steps + 1):
		a = math.pi * i / cap_steps
		points.append((x0 - radius * (nx * math.cos(a) + ux * math.sin(a)),
				y0 - radius * (ny * math.cos(a) + uy * math.sin(a))))
	return points


def clip_halfplane(polygon, normal, offset):
	"""保留 normal·p >= offset 的部分（单平面 Sutherland-Hodgman 裁剪）。"""
	nx, ny = normal
	out = []
	count = len(polygon)
	for i in range(count):
		cur = polygon[i]
		prev = polygon[i - 1]
		d_cur = nx * cur[0] + ny * cur[1] - offset
		d_prev = nx * prev[0] + ny * prev[1] - offset
		if d_cur >= 0:
			if d_prev < 0:
				t = d_prev / (d_prev - d_cur)
				out.append((prev[0] + t * (cur[0] - prev[0]), prev[1] + t * (cur[1] - prev[1])))
			out.append(cur)
		elif d_prev >= 0:
			t = d_prev / (d_prev - d_cur)
			out.append((prev[0] + t * (cur[0] - prev[0]), prev[1] + t * (cur[1] - prev[1])))
	return out


def rasterize(contours, size: int, box, supersample: int = SUPERSAMPLE):
	"""非零环绕扫描线填充，返回 size×size 的覆盖率图（L 模式）。

	先在 supersample 倍分辨率下逐行填充（bytearray 切片赋值），再用 BOX 重采样，
	避免逐像素的 Python 循环。
	"""
	bx0, by0, bx1, by1 = box
	scale = size / max(bx1 - bx0, by1 - by0)
	off_x = (size - (bx1 - bx0) * scale) * 0.5
	off_y = (size - (by1 - by0) * scale) * 0.5

	def project(pt):
		return ((pt[0] - bx0) * scale + off_x, size - ((pt[1] - by0) * scale + off_y))

	ss = supersample
	width = height = size * ss
	edges = []
	for contour in contours:
		points = [project(p) for p in contour]
		for i in range(len(points)):
			x0, y0 = points[i]
			x1, y1 = points[(i + 1) % len(points)]
			if y0 == y1:
				continue
			edges.append((x0 * ss, y0 * ss, x1 * ss, y1 * ss))

	high = Image.new("L", (width, height), 0)
	for py in range(height):
		y = py + 0.5
		crossings = []
		for x0, y0, x1, y1 in edges:
			if (y0 <= y < y1) or (y1 <= y < y0):
				t = (y - y0) / (y1 - y0)
				crossings.append((x0 + t * (x1 - x0), 1 if y1 > y0 else -1))
		if not crossings:
			continue
		crossings.sort()
		row = bytearray(width)
		wind = 0
		for i in range(len(crossings) - 1):
			wind += crossings[i][1]
			if wind == 0:
				continue
			start = max(0, int(math.ceil(crossings[i][0] - 0.5)))
			end = min(width, int(math.ceil(crossings[i + 1][0] - 0.5)))
			if end > start:
				row[start:end] = b"\xff" * (end - start)
		high.paste(Image.frombytes("L", (width, 1), bytes(row)), (0, py))
	return high.resize((size, size), Image.BOX)


def render_frames(plan: PairPlan, eye, eye_off, size: int, frames: int, upm: int = 1024):
	union_box = bbox(eye + eye_off)
	center_x = (union_box[0] + union_box[2]) * 0.5
	center_y = (union_box[1] + union_box[3]) * 0.5
	half = CANVAS_UNITS * 0.5
	box = (center_x - half, center_y - half, center_x + half, center_y + half)
	if upm != 1024:
		raise ValueError(f"暂只支持 upm=1024 的字体，实际 {upm}")

	target = rasterize(eye_off, size, box)

	strip = Image.new("L", (size * frames, size), 0)
	for index in range(frames):
		t = index / (frames - 1)
		layers = [rasterize(plan.almond, size, box)]

		pupil_factor = 1.0 - segment_progress(t, PUPIL_RANGE)
		if pupil_factor > 0.02:
			layers.append(rasterize([scale_contour(c, pupil_factor, plan.pupil_center) for c in plan.pupil], size, box))

		reveal = segment_progress(t, SLASH_RANGE)
		if reveal > 0.001:
			offset = plan.axis_base + (1.0 - reveal) * (plan.axis_length - 0.001)
			piece = clip_halfplane(capsule(plan.slash_from, plan.slash_to, plan.slash_radius), plan.axis_normal, offset)
			if len(piece) > 3:
				layers.append(rasterize([piece], size, box))

		merged = layers[0]
		for layer in layers[1:]:
			merged = ImageChops.lighter(merged, layer)

		landing = segment_progress(t, LANDING_RANGE)
		if landing > 0.001:
			merged = Image.blend(merged, target, landing)
		strip.paste(merged, (index * size, 0))

	return strip, box


def rotate_contour(contour, center, degrees: float):
	"""绕 center 旋转一条轮廓（字体单位，y 向上）。"""
	radians = math.radians(degrees)
	cos_a = math.cos(radians)
	sin_a = math.sin(radians)
	cx, cy = center
	return [
		(cx + (x - cx) * cos_a - (y - cy) * sin_a,
			cy + (x - cx) * sin_a + (y - cy) * cos_a)
		for x, y in contour
	]


def render_rotate_frames(contours, size: int, frames: int, degrees: float, upm: int = 1024):
	"""把一个字形从 0° 匀速转到 degrees，逐帧光栅化；画布与语义对保持一致。"""
	if upm != 1024:
		raise ValueError(f"暂只支持 upm=1024 的字体，实际 {upm}")
	ink = bbox(contours)
	center = ((ink[0] + ink[2]) * 0.5, (ink[1] + ink[3]) * 0.5)
	half = CANVAS_UNITS * 0.5
	box = (center[0] - half, center[1] - half, center[0] + half, center[1] + half)

	strip = Image.new("L", (size * frames, size), 0)
	for index in range(frames):
		t = index / (frames - 1)
		rotated = [rotate_contour(contour, center, degrees * t) for contour in contours]
		strip.paste(rasterize(rotated, size, box), (index * size, 0))
	return strip


def shift_contour(contour, offset):
	dx, dy = offset
	return [(x + dx, y + dy) for x, y in contour]


def normalize_winding(contour):
	"""统一成逆时针（y 向上坐标系下 shoelace 面积为正），配对采样需要方向一致。"""
	return contour if area(contour) >= 0 else contour[::-1]


def cut_vertical(polygon, x_cut: float):
	"""沿竖直线剪成左右两块闭合多边形；整块在同一侧时另一块为空表。"""
	left = clip_halfplane(polygon, (-1.0, 0.0), -x_cut)
	right = clip_halfplane(polygon, (1.0, 0.0), x_cut)
	return left, right


def resample_polygon(polygon, count: int):
	"""周长均匀重采样成 count 个点；起点取最靠近自身包围盒左上角的采样点。

	起点对齐是轮廓配对的关键：两端形状都从「左上角」出发、同向绕行，
	插值时点与点之间的位移最小，中间帧才不会出现拧转。
	"""
	points = polygon[:]
	if points[0] == points[-1]:
		points = points[:-1]
	if len(points) < 3:
		return points
	segments = []
	total = 0.0
	for i in range(len(points)):
		x0, y0 = points[i]
		x1, y1 = points[(i + 1) % len(points)]
		length = math.hypot(x1 - x0, y1 - y0)
		segments.append((x0, y0, x1, y1, length))
		total += length
	if total <= 0.0:
		return points
	sampled = []
	for i in range(count):
		target = total * i / count
		accumulated = 0.0
		for x0, y0, x1, y1, length in segments:
			if accumulated + length >= target or (x0, y0, x1, y1, length) == segments[-1]:
				local = (target - accumulated) / length if length > 0 else 0.0
				local = max(0.0, min(1.0, local))
				sampled.append((x0 + (x1 - x0) * local, y0 + (y1 - y0) * local))
				break
			accumulated += length
	box = bbox([sampled])
	score = [x - box[0] + (box[3] - y) for x, y in sampled]
	start = min(range(count), key=lambda i: score[i])
	return sampled[start:] + sampled[:start]


def nearest_index(points, target):
	return min(range(len(points)), key=lambda i: math.hypot(points[i][0] - target[0], points[i][1] - target[1]))


def landmark_arcs(polygon, landmarks, per_arc: int):
	"""按界标把闭合轮廓切成若干段弧，各段均匀重采样成 per_arc 个点（不含段起点）。

	landmarks 必须已按轮廓绕行顺序给出；重复的界标表示零长弧（顶点分裂成边）。
	返回展平后的多边形点列，总点数 = len(landmarks) * per_arc，首尾天然闭合。
	跨形状配对必须走这里而不是周长重采样：两端形状的边长比例不同，按周长对齐
	会把短边映到长边上，中间帧的弦来回拧——观感就是线条歪歪扭扭。
	"""
	points = polygon[:]
	if points[0] == points[-1]:
		points = points[:-1]
	n = len(points)
	ids = [nearest_index(points, lm) for lm in landmarks]
	start = ids[0]
	ordered = sorted(ids)
	ordered = ordered[ordered.index(start):] + ordered[:ordered.index(start)]
	gaps = [(b - a) % n for a, b in zip(ordered, ordered[1:] + ordered[:1])]
	if sum(gaps) != n:
		raise ValueError(f"界标顺序与轮廓绕行方向不符：ids={ids} gaps={gaps}")
	out = []
	for a, b in zip(ordered, ordered[1:] + ordered[:1]):
		span = (b - a) % n
		if span == 0:
			# 零长弧：顶点分裂的源头，per_arc 个点都钉在同一点上。
			out.extend(points[a] for _ in range(per_arc))
			continue
		arc = [points[(a + k) % n] for k in range(span + 1)]
		lengths = [math.hypot(arc[i + 1][0] - arc[i][0], arc[i + 1][1] - arc[i][1]) for i in range(span)]
		total = sum(lengths)
		if total <= 0.0:
			out.extend(arc[0] for _ in range(per_arc))
			continue
		for k in range(per_arc):
			target = total * (k + 1) / per_arc
			accumulated = 0.0
			for i, length in enumerate(lengths):
				if accumulated + length >= target or i == span - 1:
					local = (target - accumulated) / length if length > 0 else 0.0
					local = max(0.0, min(1.0, local))
					out.append((arc[i][0] + (arc[i + 1][0] - arc[i][0]) * local,
							arc[i][1] + (arc[i + 1][1] - arc[i][1]) * local))
					break
				accumulated += length
	return out


def lerp_polygon(first, second, t: float):
	return [(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t) for (x0, y0), (x1, y1) in zip(first, second)]


def square_marks(quad):
	"""方块（逆时针）：[左上, 左下, 右下, 右上]，圆角取对角线极值点。"""
	return [
		min(quad, key=lambda p: p[0] - p[1]),
		min(quad, key=lambda p: p[0] + p[1]),
		max(quad, key=lambda p: p[0] - p[1]),
		max(quad, key=lambda p: p[0] + p[1]),
	]


def centered(contours):
	"""把一组轮廓平移到墨迹中心为原点，返回 (轮廓, 中心)。"""
	box = bbox(contours)
	center = ((box[0] + box[2]) * 0.5, (box[1] + box[3]) * 0.5)
	return [shift_contour(c, (-center[0], -center[1])) for c in contours], center


def render_morph_frames(pairs, size: int, frames: int):
	"""通用轮廓配对形变：pairs = [(点列A, 点列B, 符号)]，点数一一对应。

	点列用 landmark_arcs 按界标分段采样（边对边、角对角，禁止周长对齐）；
	符号为负的多边形在输出时反转绕向（非零环绕挖孔）。画布固定为原点中心的
	CANVAS_UNITS 方框——调用方负责把轮廓平移到「墨迹中心为原点」的坐标系。
	"""
	resampled = [(a, b, sign) for a, b, sign in pairs]
	for a, b, _ in resampled:
		if len(a) != len(b) or len(a) < 3:
			raise ValueError(f"配对点数不一致或退化：{len(a)} vs {len(b)}")
	half = CANVAS_UNITS * 0.5
	box = (-half, -half, half, half)
	strip = Image.new("L", (size * frames, size), 0)
	for index in range(frames):
		t = index / (frames - 1)
		polys = []
		for first, second, sign in resampled:
			blended = lerp_polygon(first, second, t)
			polys.append(blended if sign > 0 else blended[::-1])
		strip.paste(rasterize(polys, size, box), (index * size, 0))
	return strip


def render_play_pause_frames(play, pause, size: int, frames: int, upm: int = 1024, per_arc: int = 24):
	"""play ↔ pause 轮廓配对形变。

	两个字形都是「外轮廓 + 内孔」的描边形状（非零环绕挖孔），且墨迹中心不重合
	（play 偏右 96 单位）。做法：
	1. 各自按墨迹中心平移到原点——运行时把帧条画在图标盒中心，静止态字形由渲染器
	   按墨迹宽度居中，端点必须同中心，否则动画起止会跳一下；
	2. play 的外/内三角形沿画布竖直中线剪成左右两块，分别与 pause 左/右杠的外/内
	   矩形配对。配对按「界标分段」：左块 = [左上, 左下, 切口下, 切口上]，右块 =
	   [切口上, 切口下, 顶点, 顶点]（顶点重复表示零长弧，插值中顶点展开成杠的外缘），
	   与杠的 [左上, 左下, 右下, 右上] 逐段对应——边对边、角对角，中间帧每段都是
	   直线，不会出现周长对齐导致的拧转；
	3. 内孔多边形保持负面积（顺时针）参与非零环绕，挖孔在所有中间帧都成立。
	"""
	if upm != 1024:
		raise ValueError(f"暂只支持 upm=1024 的字体，实际 {upm}")
	if len(play) != 2 or len(pause) != 4:
		raise ValueError(f"轮廓数不符：play={len(play)} pause={len(pause)}（期望 2/4，实心字形请跳过）")

	play_centered, _ = centered(play)
	pause_centered, _ = centered(pause)

	play_outer = next(normalize_winding(c) for c in play_centered if area(c) > 0)
	play_inner = next(normalize_winding(c) for c in play_centered if area(c) < 0)
	pause_bars = sorted(
		[(normalize_winding(c), normalize_winding(h))
		 for c in pause_centered if area(c) > 0 for h in pause_centered if area(h) < 0 and bbox([h])[0] >= bbox([c])[0] and bbox([h])[2] <= bbox([c])[2] + 1.0],
		key=lambda pair: bbox([pair[0]])[0])
	if len(pause_bars) != 2:
		raise ValueError(f"pause 内外轮廓配对失败：{len(pause_bars)} 组")

	def cut_ends(piece):
		"""切口（x≈0）上的上下端点。"""
		edge = [p for p in piece if abs(p[0]) < 1e-6]
		if len(edge) < 2:
			raise ValueError("剪裁后找不到竖直切口")
		return max(edge, key=lambda p: p[1]), min(edge, key=lambda p: p[1])

	pairs = []
	for side in (0, 1):
		for source, sign in ((play_outer, 1), (play_inner, -1)):
			piece = cut_vertical(source, 0.0)[side]
			top, bottom = cut_ends(piece)
			if side == 0:
				marks = [min(piece, key=lambda p: p[0] - p[1]), min(piece, key=lambda p: p[0] + p[1]), bottom, top]
			else:
				apex = max(piece, key=lambda p: p[0])
				marks = [top, bottom, apex, apex]
			target, target_hole = pause_bars[side]
			pairs.append((landmark_arcs(piece, marks, per_arc),
					landmark_arcs(target if sign > 0 else target_hole, square_marks(target if sign > 0 else target_hole), per_arc),
					sign))
	return render_morph_frames(pairs, size, frames)


def render_play_stop_frames(play, stop, size: int, frames: int, upm: int = 1024, per_arc: int = 24):
	"""play ↔ stop：三角形 ↔ 圆角方块，整字形配对（无需裁切）。

	三角形界标（逆时针）= [顶上, 底下, 顶点, 顶点]，顶点重复表示零长弧——
	插值中顶点展开成方块的右缘；方块 = [左上, 左下, 右下, 右上]。
	弧对应：左边缘↔左边缘、下斜边↔下边缘、零长弧↔右边缘、上斜边↔上边缘。
	内孔同样处理，保持负面积参与非零环绕。两字形墨迹中心不重合（play 偏右 96
	单位），各自平移到墨迹中心为原点。
	"""
	if upm != 1024:
		raise ValueError(f"暂只支持 upm=1024 的字体，实际 {upm}")
	if len(play) != 2 or len(stop) != 2:
		raise ValueError(f"轮廓数不符：play={len(play)} stop={len(stop)}（期望 2/2，实心字形请跳过）")

	play_centered, _ = centered(play)
	stop_centered, _ = centered(stop)
	play_outer = next(normalize_winding(c) for c in play_centered if area(c) > 0)
	play_inner = next(normalize_winding(c) for c in play_centered if area(c) < 0)
	stop_outer = next(normalize_winding(c) for c in stop_centered if area(c) > 0)
	stop_inner = next(normalize_winding(c) for c in stop_centered if area(c) < 0)

	def triangle_marks(tri):
		"""三角（逆时针）：[顶上, 底下, 顶点, 顶点]。"""
		apex = max(tri, key=lambda p: p[0])
		return [max(tri, key=lambda p: p[1]), min(tri, key=lambda p: p[1]), apex, apex]

	pairs = [
		(play_outer, triangle_marks(play_outer), stop_outer, square_marks(stop_outer), 1),
		(play_inner, triangle_marks(play_inner), stop_inner, square_marks(stop_inner), -1),
	]
	return render_morph_frames(
		[(landmark_arcs(a, la, per_arc), landmark_arcs(b, lb, per_arc), sign) for a, la, b, lb, sign in pairs],
		size, frames)


def render_plus_minus_frames(plus, minus, size: int, frames: int, upm: int = 1024, per_arc: int = 24):
	"""plus-square ↔ minus-square：外框方环两字形完全相同（常量），只变形内部符号。

	加号是「洞中洞」（外环 +1、方孔 -1、加号 -1，非零环绕下加号重新填实）。
	内部符号不从加号轮廓裁切——竖臂边缘与减号条厚度线完全重合，半平面裁切会
	产生零宽退化块——而是按「横条 = 圆帽胶囊」直接构造：横臂与减号条几何一致
	（同一胶囊，常量块），竖臂取加号包围盒与条厚构造，向横臂中心线压平
	（目标点列 = 自身所有点 y=中心线，逐点插值等价于线性压平，t=1 时零面积
	完全隐入横臂）。端点保真由端点差分验证。
	"""
	if upm != 1024:
		raise ValueError(f"暂只支持 upm=1024 的字体，实际 {upm}")
	if len(plus) != 3 or len(minus) != 3:
		raise ValueError(f"轮廓数不符：plus={len(plus)} minus={len(minus)}（期望 3/3）")

	plus_centered, plus_center = centered(plus)
	minus_centered, minus_center = centered(minus)
	if abs(plus_center[0] - minus_center[0]) > 1.0 or abs(plus_center[1] - minus_center[1]) > 1.0:
		raise ValueError(f"两字形墨迹中心不重合：{plus_center} vs {minus_center}")
	# 全部归一成逆时针再分段采样；负号块靠 render_morph_frames 的 sign 翻转回顺时针。
	# 不归一直接采样会让「CW 轮廓 + sign 翻转」变成 CCW，方孔反而把内部填实。
	plus_outer = normalize_winding(next(c for c in plus_centered if area(c) > 0))
	plus_hole = normalize_winding(max((c for c in plus_centered if area(c) < 0), key=lambda c: abs(area(c))))
	minus_outer = normalize_winding(next(c for c in minus_centered if area(c) > 0))
	minus_hole = normalize_winding(max((c for c in minus_centered if area(c) < 0), key=lambda c: abs(area(c))))
	minus_symbol = normalize_winding(min((c for c in minus_centered if area(c) < 0), key=lambda c: abs(area(c))))
	for a, b, name in ((plus_outer, minus_outer, "外环"), (plus_hole, minus_hole, "方孔")):
		if bbox([a]) != bbox([b]):
			raise ValueError(f"{name}在两字形中不一致：{bbox([a])} vs {bbox([b])}")

	symbol_box = bbox([minus_symbol])
	bar_top, bar_bottom = symbol_box[3], symbol_box[1]
	bar_cy = (bar_top + bar_bottom) * 0.5
	radius = (bar_top - bar_bottom) * 0.5
	plus_symbol_box = bbox([min(plus_centered, key=lambda c: abs(area(c)))])
	bar_left, bar_right = symbol_box[0] + radius, symbol_box[2] - radius
	# 竖臂：与加号同中心、同条厚，高度取加号包围盒（横臂之外的延伸段）。
	# 居中坐标系里加号竖臂在 x=0（plus_center 是原始字体坐标，不能直接用）。
	vertical = capsule((0.0, plus_symbol_box[3] - radius), (0.0, plus_symbol_box[1] + radius), radius)

	def bar_landmarks(bar):
		"""横条（逆时针）：[最左, 最右]，两段弧分别是上边缘和下边缘。"""
		return [min(bar, key=lambda p: p[0]), max(bar, key=lambda p: p[0])]

	horizontal = capsule((bar_left, bar_cy), (bar_right, bar_cy), radius)
	vertical_sampled = landmark_arcs(normalize_winding(vertical), [max(vertical, key=lambda p: p[1]), min(vertical, key=lambda p: p[1])], per_arc)
	vertical_flat = [(x, bar_cy) for x, _ in vertical_sampled]

	pairs = [
		(landmark_arcs(plus_outer, square_marks(plus_outer), per_arc),
			landmark_arcs(minus_outer, square_marks(minus_outer), per_arc), 1),
		(landmark_arcs(plus_hole, square_marks(plus_hole), per_arc),
			landmark_arcs(minus_hole, square_marks(minus_hole), per_arc), -1),
		(landmark_arcs(normalize_winding(horizontal), bar_landmarks(horizontal), per_arc),
			landmark_arcs(normalize_winding(minus_symbol), bar_landmarks(minus_symbol), per_arc), -1),
		(vertical_sampled, vertical_flat, -1),
	]
	return render_morph_frames(pairs, size, frames)


def star_marks(polygon, cx: float, cy: float):
	"""五角星界标（按绕行顺序交错：尖顶、凹谷、尖顶……）。

	字体轮廓有曲率抖动，逐点局部极值会把噪声当尖顶；先对「到心距离」做循环
	移动平均（窗口 ~n/18）再找局部最大，得到干净的 5 个尖顶；凹谷不用平滑值——
	圆角尖顶两端会产生假极小——而在相邻尖顶的索引区间内取原始距离最小点。
	"""
	points = polygon[:]
	if points[0] == points[-1]:
		points = points[:-1]
	n = len(points)
	dists = [math.hypot(x - cx, y - cy) for x, y in points]

	window = max(3, (n // 18) | 1)
	half = window // 2
	smoothed = [sum(dists[(i + k) % n] for k in range(-half, half + 1)) / window for i in range(n)]
	peaks = [i for i in range(n) if smoothed[i] >= smoothed[(i - 1) % n] and smoothed[i] > smoothed[(i + 1) % n]]
	if len(peaks) != 5:
		raise ValueError(f"星形尖顶识别失败：平滑后局部最大 {len(peaks)} 个（期望 5）")

	marks = []
	for k in range(5):
		a = peaks[k]
		b = peaks[(k + 1) % 5]
		marks.append(points[a])
		span = range(a, b + 1) if a < b else list(range(a, n)) + list(range(0, b + 1))
		valley = min(span, key=lambda i: dists[i])
		if valley == a:
			raise ValueError(f"第 {k} 个凹谷与尖顶重合（索引 {a}）")
		marks.append(points[valley])
	return marks


def render_star_favorite_frames(outline_contours, fill_contours, size: int, frames: int, upm: int = 1024, per_arc: int = 20):
	"""star 收藏点亮：描边星（外环 + 内孔）↔ 实心星（Fill 字面单轮廓）。

	两端码位相同、外环 bbox 完全一致（实测 896x864），morph 分解为：
	1. 外环 ↔ Fill 外环：五角星 10 界标（5 尖顶 + 5 凹谷）按绕行顺序逐段配对；
	   Fill 轮廓绕向与 Regular 外环相反（CW vs CCW），先反转成同向再采样，
	   否则顶点插值中途出现自交蝴蝶结；
	2. 内孔收缩闭合：孔也是星形，采样后目标点列全部取孔质心——插值中孔对称
	   缩到零面积，非零环绕下整星填实，与 Fill 端点重合。
	两字形墨迹中心一致（同 bbox），无需额外归心。
	"""
	if upm != 1024:
		raise ValueError(f"暂只支持 upm=1024 的字体，实际 {upm}")
	if len(outline_contours) != 2 or len(fill_contours) != 1:
		raise ValueError(f"轮廓数不符：outline={len(outline_contours)} fill={len(fill_contours)}（期望 2/1）")

	# 画布是「原点中心」的方框，字形坐标必须先归心（描边星与实心星墨迹中心一致，
	# 用同一个平移量，端点才能与运行时居中绘制的字形对齐）。
	outline_centered, star_center = centered(outline_contours)
	fill_centered, _ = centered(fill_contours)
	outer = normalize_winding(next(c for c in outline_centered if area(c) > 0))
	hole = normalize_winding(next(c for c in outline_centered if area(c) < 0))
	fill_ccw = fill_centered[0][::-1] if area(fill_centered[0]) < 0 else fill_centered[0]
	fill_ccw = normalize_winding(fill_ccw)
	outline_box = bbox([outer])
	fill_box = bbox([fill_ccw])
	# 各字重外环尺寸与 Fill 字面允许有差异（bold 更大、light 更小），只要墨迹
	# 中心一致即可——morph 中外环轻微缩放是预期的平滑过渡。
	outline_center = ((outline_box[0] + outline_box[2]) * 0.5, (outline_box[1] + outline_box[3]) * 0.5)
	fill_center = ((fill_box[0] + fill_box[2]) * 0.5, (fill_box[1] + fill_box[3]) * 0.5)
	if abs(outline_center[0] - fill_center[0]) > 1.0 or abs(outline_center[1] - fill_center[1]) > 1.0:
		raise ValueError(f"外环与 Fill 轮廓墨迹中心不一致：{outline_center} vs {fill_center}")

	hole_box = bbox([hole])
	hole_center = ((hole_box[0] + hole_box[2]) * 0.5, (hole_box[1] + hole_box[3]) * 0.5)
	hole_sampled = landmark_arcs(hole, star_marks(hole, hole_center[0], hole_center[1]), per_arc)
	hole_collapsed = [(hole_center[0], hole_center[1]) for _ in hole_sampled]

	outer_center = ((outline_box[0] + outline_box[2]) * 0.5, (outline_box[1] + outline_box[3]) * 0.5)
	marks_outer = star_marks(outer, outer_center[0], outer_center[1])
	marks_fill = star_marks(fill_ccw, outer_center[0], outer_center[1])

	def polar(point):
		return math.atan2(point[1] - outer_center[1], point[0] - outer_center[0])

	# 两端轮廓起点索引不同，界标序列的物理起点不对应——按极角序列找最优旋转，
	# 否则 A 的第 0 个尖顶会插值到 B 的第 k 个尖顶，中间帧星形来回扭转。
	angles_outer = [polar(q) for q in marks_outer]
	best_shift, best_error = 0, None
	for shift in range(10):
		rotated = marks_fill[shift:] + marks_fill[:shift]
		error = 0.0
		for point, angle in zip(rotated, angles_outer):
			delta = (polar(point) - angle + math.pi) % (2.0 * math.pi) - math.pi
			error += delta * delta
		if best_error is None or error < best_error:
			best_shift, best_error = shift, error
	marks_fill = marks_fill[best_shift:] + marks_fill[:best_shift]

	pairs = [
		(landmark_arcs(outer, marks_outer, per_arc),
			landmark_arcs(fill_ccw, marks_fill, per_arc), 1),
		(hole_sampled, hole_collapsed, -1),
	]
	return render_morph_frames(pairs, size, frames)


def strip_to_rgba(strip: Image.Image) -> Image.Image:
	"""帧条转成「白色 + 覆盖率做 alpha」的 RGBA，便于运行时按图标色乘算。"""
	rgba = Image.new("RGBA", strip.size, (255, 255, 255, 0))
	rgba.putalpha(strip)
	return rgba


def outline_radius(size: int) -> int:
	"""描边半径：对齐 CTextRender::AdjustOutlineThicknessToFontSize（<18px 字号取 1，
	18..47 取 2，>=48 取 4）。帧边长为 size 时字号约 size / 1.125。"""
	return max(1, int(round(size / 16.0)))


def grow_outline(strip: Image.Image, radius: int) -> Image.Image:
	"""按 QmGrowGlyphOutline 的圆盘掩模扩张覆盖率，逐帧独立处理避免相邻帧串色。

	字体渲染的每个字形都有「填充 + 描边」两张纹理，描边由同一算法从填充外扩得到；
	帧条只有填充时，图标就没有那圈 1px 暗边，视觉上会显得发白、发糊。
	"""
	if radius <= 0:
		return strip.copy()
	size = strip.height
	offsets = [(dx, dy, 1.0 - min(max(math.hypot(dx, dy) - radius, 0.0), 1.0))
		for dy in range(-radius, radius + 1)
		for dx in range(-radius, radius + 1)]
	out = Image.new("L", strip.size, 0)
	src = strip.load()
	dst = out.load()
	for frame in range(strip.width // size):
		x0 = frame * size
		for y in range(size):
			for x in range(size):
				best = src[x0 + x, y]
				if best < 255:
					for dx, dy, weight in offsets:
						xx = x + dx
						yy = y + dy
						if xx < 0 or xx >= size or yy < 0 or yy >= size:
							continue
						value = int(src[x0 + xx, yy] * weight)
						if value > best:
							best = value
							if best >= 255:
								break
				dst[x0 + x, y] = best
	return out


def stack_fill_and_outline(fill: Image.Image, outline: Image.Image) -> Image.Image:
	"""上半填充、下半描边：同一张纹理分两趟绘制，资产数量不翻倍。"""
	canvas = Image.new("L", (fill.width, fill.height * 2), 0)
	canvas.paste(fill, (0, 0))
	canvas.paste(outline, (0, fill.height))
	return canvas


def probe(fonts_dir: Path) -> int:
	for weight, filename in WEIGHTS.items():
		path = fonts_dir / filename
		if not path.exists():
			print(f"[{weight}] 缺少 {path}")
			continue
		eye, upm = load_contours(path, EYE)
		eye_off, _ = load_contours(path, EYE_OFF)
		print(f"[{weight}] upm={upm} eye={len(eye)} 条 eye_off={len(eye_off)} 条")
		for name, contours in (("eye", eye), ("eye_off", eye_off)):
			for i, contour in enumerate(contours):
				cb = bbox([contour])
				print(f"    {name}[{i}] pts={len(contour):4d} area={area(contour):12.0f} bbox=({cb[0]:.0f},{cb[1]:.0f})-({cb[2]:.0f},{cb[3]:.0f})")
		try:
			plan = build_plan(eye, eye_off)
			print(f"    plan ok: stroke/2={plan.slash_radius:.0f} pupil_center=({plan.pupil_center[0]:.0f},{plan.pupil_center[1]:.0f}) axis_len={plan.axis_length:.0f}")
		except ValueError as error:
			print(f"    plan 失败：{error}")
	return 0


def strip_filename(base: str, weight: str, size: int) -> str:
	"""64 档保持旧文件名（无后缀），其余档带 _<像素>；与运行时 FrameAssetName 对齐。"""
	suffix = "" if size == DEFAULT_SIZE else f"_{size}"
	return f"{base}_{weight}{suffix}.png"


def main(argv: list[str]) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--fonts-dir", default="data/fonts/Phosphor")
	parser.add_argument("--out-dir", default="data/qmclient/ui")
	parser.add_argument("--sizes", default=",".join(str(s) for s in DEFAULT_SIZES), help="逗号分隔的帧像素边长档位")
	parser.add_argument("--frames", type=int, default=DEFAULT_FRAMES)
	parser.add_argument("--weights", default=",".join(WEIGHTS))
	parser.add_argument("--probe", action="store_true", help="只打印字形结构，不写资产")
	parser.add_argument("--preview", default=None, help="额外输出一张对照图")
	args = parser.parse_args(argv)

	fonts_dir = Path(args.fonts_dir)
	if args.probe:
		return probe(fonts_dir)

	out_dir = Path(args.out_dir)
	out_dir.mkdir(parents=True, exist_ok=True)
	sizes = [int(s) for s in args.sizes.split(",") if s.strip()]
	if not sizes:
		print("--sizes 为空", file=sys.stderr)
		return 2

	generated = []
	for weight in [w.strip() for w in args.weights.split(",") if w.strip()]:
		filename = WEIGHTS.get(weight)
		if filename is None:
			print(f"跳过未知字重 {weight}", file=sys.stderr)
			continue
		path = fonts_dir / filename
		if not path.exists():
			print(f"跳过 {weight}：缺少 {path}", file=sys.stderr)
			continue
		eye, upm = load_contours(path, EYE)
		eye_off, _ = load_contours(path, EYE_OFF)
		try:
			plan = build_plan(eye, eye_off)
		except ValueError as error:
			print(f"跳过 {weight}：{error}", file=sys.stderr)
			continue
		for size in sizes:
			strip, _ = render_frames(plan, eye, eye_off, size, args.frames, upm)
			target = out_dir / strip_filename("password_icon", weight, size)
			outline = grow_outline(strip, outline_radius(size))
			strip_to_rgba(stack_fill_and_outline(strip, outline)).save(target)
			generated.append((weight, strip, target))
			print(f"写入 {target} ({target.stat().st_size // 1024} KiB, {args.frames} 帧 × {size}px, 填充+描边)")

		# 旋转对资产：与语义对共用画布尺寸与帧数，运行时用同一套 side/UV 采样。
		# Fill 字重的 chevron 是实心块，语义上不需要过渡，跳过。
		if weight != "fill":
			for name, codepoint, degrees in ROTATE_PAIRS:
				contours, _ = load_contours(path, codepoint)
				for size in sizes:
					rotate_strip = render_rotate_frames(contours, size, args.frames, degrees, upm)
					rotate_target = out_dir / strip_filename(f"{name}_swap", weight, size)
					rotate_outline = grow_outline(rotate_strip, outline_radius(size))
					strip_to_rgba(stack_fill_and_outline(rotate_strip, rotate_outline)).save(rotate_target)
					print(f"写入 {rotate_target} ({rotate_target.stat().st_size // 1024} KiB, {args.frames} 帧 × {size}px, 填充+描边)")

			# play ↔ pause：轮廓配对形变（三角形顶点分裂成双杠），Fill 实心字形跳过。
			# play ↔ stop：三角形 ↔ 圆角方块，整字形配对。
			# plus-square ↔ minus-square：外框常量，加号横臂常量、竖臂压平。
			shape_pairs = [
				("play_pause_swap", 0xE3D0, 0xE39E, render_play_pause_frames, (2, 4)),
				("play_stop_swap", 0xE3D0, 0xE46C, render_play_stop_frames, (2, 2)),
				("plus_minus_swap", 0xED4A, 0xED4C, render_plus_minus_frames, (3, 3)),
			]
			for name, cp_a, cp_b, renderer, expected in shape_pairs:
				try:
					contours_a, pair_upm = load_contours(path, cp_a)
					contours_b, _ = load_contours(path, cp_b)
					if (len(contours_a), len(contours_b)) != expected:
						raise ValueError(f"轮廓数不符：{len(contours_a)}/{len(contours_b)}（期望 {expected[0]}/{expected[1]}）")
					for size in sizes:
						pair_strip = renderer(contours_a, contours_b, size, args.frames, pair_upm)
						pair_target = out_dir / strip_filename(name, weight, size)
						pair_outline = grow_outline(pair_strip, outline_radius(size))
						strip_to_rgba(stack_fill_and_outline(pair_strip, pair_outline)).save(pair_target)
						generated.append((weight, pair_strip, pair_target))
						print(f"写入 {pair_target} ({pair_target.stat().st_size // 1024} KiB, {args.frames} 帧 × {size}px, 填充+描边)")
				except ValueError as error:
					print(f"跳过 {name}（{weight}）：{error}")

			# star 收藏点亮：A=当前字重描边星，B=Fill 字面实心星（跨字面配对，
			# 与运行时 DrawQmIcon 按 m_QmUiFavoriteIconFilled 切换 ICON_FONT_FILL 一致）。
			# fill 字重本身恒实心、无切换，跳过。
			if weight != "fill":
				try:
					star_outline, star_upm = load_contours(path, STAR_FAVORITE)
					star_fill, _ = load_contours(fonts_dir / WEIGHTS["fill"], STAR_FAVORITE)
					for size in sizes:
						star_strip = render_star_favorite_frames(star_outline, star_fill, size, args.frames, star_upm)
						star_target = out_dir / strip_filename("star_favorite_swap", weight, size)
						star_outline_img = grow_outline(star_strip, outline_radius(size))
						strip_to_rgba(stack_fill_and_outline(star_strip, star_outline_img)).save(star_target)
						generated.append((weight, star_strip, star_target))
						print(f"写入 {star_target} ({star_target.stat().st_size // 1024} KiB, {args.frames} 帧 × {size}px, 填充+描边)")
				except ValueError as error:
					print(f"跳过 star_favorite（{weight}）：{error}")

	if args.preview and generated:
		pad = 8
		width = max(strip.width for _, strip, _ in generated) + pad * 2
		height = sum(strip.height + pad for _, strip, _ in generated) + pad
		canvas = Image.new("RGBA", (width, height), (24, 24, 24, 255))
		y = pad
		for _, strip, _ in generated:
			canvas.paste(strip_to_rgba(strip), (pad, y), strip_to_rgba(strip))
			y += strip.height + pad
		Path(args.preview).parent.mkdir(parents=True, exist_ok=True)
		canvas.save(args.preview)
		print(f"写出预览 {args.preview}")

	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
