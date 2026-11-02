#!/usr/bin/env python3
"""IME 渐变圆角 SDF 的真实 GPU 像素回归。

候选栏是 `DrawRoundedGradientSurface` 的唯一生产调用点，而它只有 GPU 片元着色器才能
决定渐变是否连续、圆角边缘是否约 1 像素羽化。这里用真实 Vulkan 客户端把同一份生产
绘制函数画到交换链的固定矩形上，再用引擎自己的 `screenshot` 命令取回最终呈现像素，
按日志坐标逐像素断言：

- 渐变横向连续：R 通道跨度接近满量程、没有长平台、没有反向跳变；
- 圆角收窄：胶囊尖端列高明显小于中心列高；
- 边框存在：顶部边框带命中绿色边框色；
- 边缘羽化：中线上出现少量部分覆盖像素，而不是硬阶梯。

用法：python qmclient_scripts/integration/ime_gradient_sdf_smoke.py <build-dir>
证据（截图、日志）留在 <build-dir>/tmp/tests/ime-gradient-sdf-*。
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import struct
import sys
import tempfile
import time
import zlib

try:
	from qmclient_scripts.integration.process_harness import Process
	from qmclient_scripts.integration.vulkan_fsaa_blur_smoke import focus_client_window
except ModuleNotFoundError:
	from process_harness import Process  # type: ignore[no-redef]
	from vulkan_fsaa_blur_smoke import focus_client_window  # type: ignore[no-redef]


# 与 ime_gradient_probe.cpp 的常量保持一致。
PROBE_X = 8
PROBE_Y = 8
PROBE_WIDTH = 512
PROBE_HEIGHT = 128
PROBE_BORDER = 2

# 判据：解析 SDF + 片元渐变的可观察特征。
MIN_GRADIENT_SPAN = 240
MAX_FLAT_RUN = 4
MAX_REVERSALS = 0
# 外缘羽化环带上的覆盖度混合像素数；硬阶梯边缘为 0，解析 SDF 约 110。
MIN_FEATHER_PIXELS = 40
MIN_CAP_RATIO = 0.02
MAX_CAP_RATIO = 0.75
# 顶边只有中间平直段是满宽边框；两端被半径 64 的圆角吃掉，所以按 512-2*64 留余量。
MIN_BORDER_RUN = 370


def read_png(path: Path) -> tuple[int, int, list[tuple[int, int, int, int]]]:
	"""最小 PNG 解码：只处理 8-bit RGBA、非隔行，足够读取引擎截图。"""
	data = path.read_bytes()
	if data[:8] != b"\x89PNG\r\n\x1a\n":
		raise AssertionError(f"not a PNG: {path}")
	position = 8
	width = height = 0
	idat = bytearray()
	while position < len(data):
		length = struct.unpack(">I", data[position : position + 4])[0]
		chunk = data[position + 4 : position + 8]
		payload = data[position + 8 : position + 8 + length]
		position += 12 + length
		if chunk == b"IHDR":
			width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", payload)
			if depth != 8 or color != 6 or interlace != 0:
				raise AssertionError(f"unsupported PNG format: depth={depth} color={color} interlace={interlace}")
		elif chunk == b"IDAT":
			idat += payload
		elif chunk == b"IEND":
			break
	raw = zlib.decompress(bytes(idat))
	stride = width * 4
	pixels: list[tuple[int, int, int, int]] = []
	previous = bytearray(stride)
	offset = 0
	for _ in range(height):
		filter_type = raw[offset]
		offset += 1
		line = bytearray(raw[offset : offset + stride])
		offset += stride
		if filter_type == 1:
			for i in range(4, stride):
				line[i] = (line[i] + line[i - 4]) & 0xFF
		elif filter_type == 2:
			for i in range(stride):
				line[i] = (line[i] + previous[i]) & 0xFF
		elif filter_type == 3:
			for i in range(stride):
				left = line[i - 4] if i >= 4 else 0
				line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
		elif filter_type == 4:
			for i in range(stride):
				a = line[i - 4] if i >= 4 else 0
				b = previous[i]
				c = previous[i - 4] if i >= 4 else 0
				p = a + b - c
				pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
				pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
				line[i] = (line[i] + pred) & 0xFF
		elif filter_type != 0:
			raise AssertionError(f"unsupported PNG filter {filter_type}")
		for x in range(width):
			pixels.append((line[x * 4], line[x * 4 + 1], line[x * 4 + 2], line[x * 4 + 3]))
		previous = line
	if len(pixels) != width * height:
		raise AssertionError("PNG pixel count mismatch")
	return width, height, pixels


def analyse_png(path: Path) -> dict[str, float]:
	"""在最终合成画面上按颜色掩码测量探针。

	截图是合成后的不透明画面，alpha 恒为 255，因此不能用 alpha 判断覆盖度；
	这里改用「是否等于背景色」和「是否落在边框绿 / 渐变填充色域」来分类，
	部分覆盖的羽化像素会落在这些掩码之外，正好就是我们要数的 AA 像素。
	"""
	width, height, pixels = read_png(path)

	def pixel(x: int, y: int) -> tuple[int, int, int, int]:
		return pixels[y * width + x]

	# 截图分辨率可能和 gfx_screen_width 不同（DPI 缩放），按比例换算探针坐标。
	scale_x = width / 1280.0
	scale_y = height / 720.0
	if abs(scale_x - scale_y) > 0.02:
		raise AssertionError(f"screenshot aspect does not match window: {width}x{height}")

	def sx(value: int) -> int:
		return max(0, min(width - 1, int(round(value * scale_x))))

	def sy(value: int) -> int:
		return max(0, min(height - 1, int(round(value * scale_y))))

	# 探针先铺纯黑底板，羽化像素固定落在黑底上，便于逐像素判定。
	background = pixel(max(0, sx(PROBE_X) - 4), sy(PROBE_Y + PROBE_HEIGHT // 2))[:3]

	def is_border(x: int, y: int) -> bool:
		r, g, b = pixel(x, y)[:3]
		return g > 200 and r < 60 and b < 60

	def is_fill(x: int, y: int) -> bool:
		r, g, b = pixel(x, y)[:3]
		# 渐变填充的绿色分量恒为 0，红蓝随位置变化。
		return g < 70 and (r > 60 or b > 60)

	def is_probe(x: int, y: int) -> bool:
		return is_border(x, y) or is_fill(x, y)

	# 用边框绿定位探针实际位置，避免 DPI 缩放误差把取样点带偏。
	border_points = [(x, y) for y in range(0, min(height, sy(PROBE_Y + PROBE_HEIGHT + 16))) for x in range(0, min(width, sx(PROBE_X + PROBE_WIDTH + 16))) if is_border(x, y)]
	if not border_points:
		raise AssertionError("probe border was not found on screen; the probe did not reach the swapchain")
	border_min_x = min(p[0] for p in border_points)
	border_max_x = max(p[0] for p in border_points)
	border_min_y = min(p[1] for p in border_points)
	border_max_y = max(p[1] for p in border_points)
	probe_width = border_max_x - border_min_x + 1
	probe_height = border_max_y - border_min_y + 1

	middle_y = (border_min_y + border_max_y) // 2
	# 渐变取样避开两侧边框，留 4 像素给圆角过渡。
	left = border_min_x + sx(PROBE_BORDER + 4)
	right = border_max_x - sx(PROBE_BORDER + 4)

	minimum_red = 255
	maximum_red = 0
	flat_run = 0
	max_flat_run = 0
	reversals = 0
	previous_red: int | None = None
	last_direction = 0
	for x in range(left, right):
		red = pixel(x, middle_y)[0]
		minimum_red = min(minimum_red, red)
		maximum_red = max(maximum_red, red)
		if previous_red is not None:
			if red == previous_red:
				flat_run += 1
			else:
				max_flat_run = max(max_flat_run, flat_run + 1)
				flat_run = 0
			delta = red - previous_red
			if delta > 2 and last_direction < 0:
				reversals += 1
			if delta < -2:
				last_direction = -1
			elif delta > 2:
				last_direction = 1
		previous_red = red
	max_flat_run = max(max_flat_run, flat_run + 1)

	# 羽化强度：沿两端圆弧、紧贴外缘的 1 像素环带里，既不完全是边框绿、
	# 也不完全是纯黑底板的像素数量。解析 SDF 会按覆盖度把绿混到黑底上形成
	# 过渡带；硬阶梯边缘在该环带里只有纯绿或纯黑，因此这个计数直接对应
	# 「边缘是否被抗锯齿」。
	radius = probe_height / 2.0
	arc_cy = (border_min_y + border_max_y + 1) / 2.0
	caps = (border_min_x + radius, border_max_x + 1 - radius)
	feather_pixels = 0
	feather_samples = 0
	for cap_x in caps:
		for step in range(0, 360):
			angle = step * 3.14159265358979 / 180.0
			x = int(round(cap_x + (radius + 0.5) * __import__("math").cos(angle)))
			y = int(round(arc_cy + (radius + 0.5) * __import__("math").sin(angle)))
			if not (border_min_x <= x <= border_max_x and border_min_y <= y <= border_max_y):
				continue
			feather_samples += 1
			# 环带会同时切到边框绿、纯黑底板和渐变填充（胶囊内部），
			# 只有这三类之外的像素才是真正的覆盖度混合。
			if is_border(x, y) or is_fill(x, y):
				continue
			if pixel(x, y)[:3] == background:
				continue
			feather_pixels += 1

	# 圆角收窄：胶囊最左列的高度应远小于中心列。
	def column_height(x: int) -> int:
		return sum(1 for y in range(border_min_y, border_max_y + 1) if is_probe(x, y))

	center_height = max(1, column_height((border_min_x + border_max_x) // 2))
	cap_height = column_height(border_min_x + 1)
	cap_ratio = cap_height / center_height

	# 边框完整度：顶部和底部各有一整条边框。
	top_border = sum(1 for x in range(border_min_x, border_max_x + 1) if is_border(x, border_min_y))
	bottom_border = sum(1 for x in range(border_min_x, border_max_x + 1) if is_border(x, border_max_y))

	return {
		"gradient_span": maximum_red - minimum_red,
		"max_flat_run": max_flat_run,
		"reversals": reversals,
		"feather_pixels": feather_pixels,
		"feather_samples": feather_samples,
		"cap_ratio": cap_ratio,
		"probe_width": probe_width,
		"probe_height": probe_height,
		"top_border": top_border,
		"bottom_border": bottom_border,
		"center_red": pixel((border_min_x + border_max_x) // 2, middle_y)[0],
	}


def assert_metrics(metrics: dict[str, float]) -> None:
	failures = []
	if metrics["probe_width"] != PROBE_WIDTH or metrics["probe_height"] != PROBE_HEIGHT:
		failures.append(f"probe geometry changed: {metrics['probe_width']}x{metrics['probe_height']} != {PROBE_WIDTH}x{PROBE_HEIGHT}")
	if metrics["gradient_span"] < MIN_GRADIENT_SPAN:
		failures.append(f"gradient span too small: {metrics['gradient_span']} < {MIN_GRADIENT_SPAN}")
	if metrics["max_flat_run"] > MAX_FLAT_RUN:
		failures.append(f"gradient has a flat plateau: {metrics['max_flat_run']} > {MAX_FLAT_RUN}")
	if metrics["reversals"] > MAX_REVERSALS:
		failures.append(f"gradient reverses direction {metrics['reversals']} times")
	if metrics["feather_pixels"] < MIN_FEATHER_PIXELS:
		failures.append(f"edges are not anti-aliased: {metrics['feather_pixels']} blended pixels on the outer arc < {MIN_FEATHER_PIXELS}")
	if not (MIN_CAP_RATIO < metrics["cap_ratio"] < MAX_CAP_RATIO):
		failures.append(f"cap does not narrow: ratio {metrics['cap_ratio']:.3f} not in ({MIN_CAP_RATIO}, {MAX_CAP_RATIO})")
	if metrics["top_border"] < MIN_BORDER_RUN or metrics["bottom_border"] < MIN_BORDER_RUN:
		failures.append(f"border is incomplete: top={metrics['top_border']} bottom={metrics['bottom_border']} < {MIN_BORDER_RUN}")
	if failures:
		raise AssertionError("IME gradient SDF pixel checks failed:\n  " + "\n  ".join(failures))


def find_screenshot(root: Path, timeout: float = 20.0) -> Path:
	deadline = time.monotonic() + timeout
	while time.monotonic() < deadline:
		found = sorted(root.rglob("screenshots/*.png"))
		if found:
			return found[-1]
		time.sleep(0.1)
	raise AssertionError(f"screenshot was not written under {root}")


def smoke_ime_gradient_sdf(build_dir: Path) -> int:
	# 直接起进程而不经过 ProcessEnvironment：后者会先跑一次目标构建，
	# 而 LTCG 链接在 CI 里可能耗时数分钟，像素回归不需要重复链接。
	temp_dir = Path(tempfile.mkdtemp(prefix="qm_ime_gradient_", dir=build_dir))
	(temp_dir / "storage.cfg").write_text(f"add_path .\nadd_path {build_dir / 'data'}\n", encoding="utf-8")
	arguments = [
		str(build_dir / "DDNet.exe"),
		"gfx_fullscreen 0",
		"gfx_backend Vulkan",
		# FSAA 0 是重点：没有多重采样兜底时，圆角和渐变只能靠 SDF 自己。
		"gfx_fsaa_samples 0",
		"gfx_screen_width 1280",
		"gfx_screen_height 720",
		"gfx_vsync 0",
		"qm_gaussian_blur 0",
		"qm_ui_scale 100",
		"cl_skip_start_menu 1",
		"cl_save_settings 0",
		"qm_auto_update 0",
		"snd_enable 0",
		'logfile "client.log"',
	]
	client = Process(
		"client",
		arguments,
		temp_dir,
		fifo_command="cl_input_fifo",
		pipe_prefix="qm_ime_gradient_",
		# 探针只能由测试环境变量开启（正式构建不提供控制台命令），
		# 否则屏幕上会出现测试色块。
		env={"QMCLIENT_TEST_HIDE_DIALOG": "1", "QMCLIENT_TEST_IME_GRADIENT_PROBE": "1"},
	)
	try:
		client.wait_for(lambda line: line.startswith("client: version"), "client startup", 30)
		# 后台最小化的窗口不呈现交换链，screenshot 会一直等不到像素；
		# 聚焦本场景的开发实例后再取样。
		if not focus_client_window(client):
			raise AssertionError("could not focus the client window; screenshots require a presented swapchain")
		# 探针由环境变量开启，首帧渲染即绘制；等它确实画出固定矩形。
		drawn = client.wait_for(lambda line: "ime/probe: drawn" in line, "probe surface drawn", 20)
		match = re.search(r"drawn x=(\d+) y=(\d+) w=(\d+) h=(\d+) border=(\d+) sdf=(\d+)", drawn)
		if match is None:
			raise AssertionError(f"probe draw log was malformed: {drawn}")
		if int(match.group(6)) != 1:
			raise AssertionError("rounded-rect SDF is unavailable; the graded path is not active")
		if (int(match.group(3)), int(match.group(4))) != (PROBE_WIDTH, PROBE_HEIGHT):
			raise AssertionError(f"probe size changed: {match.group(3)}x{match.group(4)}")

		# screenshot 只在窗口持有输入焦点时才真正取像素（否则 WindowActive() 为假，
		# 命令被静默丢弃）。前台焦点可能被其它窗口抢走，所以每轮都重新聚焦并重试。
		time.sleep(0.5)
		screenshot = None
		for _ in range(5):
			focus_client_window(client)
			time.sleep(0.6)
			client.command("screenshot")
			try:
				screenshot = find_screenshot(temp_dir, timeout=8.0)
				break
			except AssertionError:
				time.sleep(1.0)
		if screenshot is None:
			raise AssertionError(
				f"screenshot was not written under {temp_dir}; "
				"the client window never held input focus, so the screenshot command was dropped"
			)
		metrics = analyse_png(screenshot)
		print("metrics:", metrics)
		print("screenshot:", screenshot)
		assert_metrics(metrics)
		client.command("quit")
		code = client.wait_for_exit(20)
		if code != 0:
			raise AssertionError(f"client exited with {code}")
	finally:
		client.stop()
	return 0


def main() -> int:
	parser = argparse.ArgumentParser(description="Real-GPU pixel check for the IME gradient rounded SDF")
	parser.add_argument("build_dir", type=Path)
	args = parser.parse_args()
	build_dir = args.build_dir.resolve()
	if not (build_dir / "DDNet.exe").is_file():
		print(f"missing client binary: {build_dir / 'DDNet.exe'}", file=sys.stderr)
		return 2
	try:
		return smoke_ime_gradient_sdf(build_dir)
	except AssertionError as exc:
		print(f"FAILED: {exc}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
