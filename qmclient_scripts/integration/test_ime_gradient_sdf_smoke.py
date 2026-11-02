import struct
import sys
import unittest
import zlib
from pathlib import Path
import tempfile

try:
	from qmclient_scripts.integration.ime_gradient_sdf_smoke import analyse_png, assert_metrics
except ModuleNotFoundError:
	sys.path.insert(0, str(Path(__file__).resolve().parent))
	from ime_gradient_sdf_smoke import analyse_png, assert_metrics  # type: ignore[no-redef]


WIDTH = 1280
HEIGHT = 720
PROBE_X = 8
PROBE_Y = 8
PROBE_W = 512
PROBE_H = 128
# 探针先铺纯黑底板，羽化像素落在固定底色上。
BACKGROUND = (0, 0, 0)


def write_png(path: Path, pixels: list[tuple[int, int, int]]) -> None:
	"""写 8-bit RGBA PNG；只用于测试分析器，不追求通用性。"""
	raw = bytearray()
	for y in range(HEIGHT):
		raw.append(0)
		for x in range(WIDTH):
			raw.extend((*pixels[y * WIDTH + x], 255))

	def chunk(tag: bytes, payload: bytes) -> bytes:
		return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

	header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0)
	path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))


def build_probe(*, aa: bool = True, gradient: bool = True) -> list[tuple[int, int, int]]:
	"""画一块和分析器预期一致的胶囊：绿边框 + 红蓝渐变填充。

	aa=False 时去掉边缘羽化（硬阶梯），gradient=False 时填充为纯红（无渐变），
	两者都是真实回归会出现的样子。
	"""
	pixels = [BACKGROUND] * (WIDTH * HEIGHT)
	# 与 shader 相同的圆角矩形 SDF；半径取高度一半，形状是两端半圆的胶囊，
	# 但顶边中段仍是平直的，所以顶部边框应覆盖整条平直段。
	half_x = PROBE_W / 2.0
	half_y = PROBE_H / 2.0
	radius = PROBE_H / 2.0
	for y in range(PROBE_H):
		for x in range(PROBE_W):
			qx = abs(x + 0.5 - half_x) - (half_x - radius)
			qy = abs(y + 0.5 - half_y) - (half_y - radius)
			outside = max(qx, 0.0) ** 2 + max(qy, 0.0) ** 2
			distance = outside**0.5 + min(max(qx, qy), 0.0) - radius
			if distance > 0.0:
				continue
			border = 2.0
			if distance > -border:
				color = (0, 255, 0)
			else:
				if gradient:
					t = (x + 0.5) / PROBE_W
					color = (int(round(255 * (1.0 - t))), 0, int(round(255 * t)))
				else:
					color = (255, 0, 0)
			# 与 shader 一致：羽化以 distance=0 为中心，宽约 1 像素，
			# 因此只在 -0.5 < distance < 0.5 的窄带上混背景。
			if aa and distance > -0.5:
				t = min(1.0, max(0.0, (distance + 0.5) / 1.0))
				coverage = 1.0 - (t * t * (3.0 - 2.0 * t))
				color = tuple(int(round(c * coverage + b * (1.0 - coverage))) for c, b in zip(color, BACKGROUND))
			pixels[(PROBE_Y + y) * WIDTH + (PROBE_X + x)] = color
	return pixels


class QmImeGradientSdfAnalysisTest(unittest.TestCase):
	def analyse(self, pixels: list[tuple[int, int, int]]) -> dict[str, float]:
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "probe.png"
			write_png(path, pixels)
			return analyse_png(path)

	def test_correct_surface_passes(self):
		metrics = self.analyse(build_probe())
		self.assertEqual(metrics["probe_width"], PROBE_W)
		self.assertEqual(metrics["probe_height"], PROBE_H)
		self.assertGreaterEqual(metrics["gradient_span"], 240)
		assert_metrics(metrics)

	def test_flat_fill_without_gradient_fails(self):
		metrics = self.analyse(build_probe(gradient=False))
		self.assertLess(metrics["gradient_span"], 240)
		with self.assertRaises(AssertionError) as raised:
			assert_metrics(metrics)
		self.assertIn("gradient span too small", str(raised.exception))

	def test_hard_edges_without_feather_fail(self):
		metrics = self.analyse(build_probe(aa=False))
		self.assertLess(metrics["feather_pixels"], 40)
		with self.assertRaises(AssertionError) as raised:
			assert_metrics(metrics)
		self.assertIn("not anti-aliased", str(raised.exception))

	def test_missing_probe_is_reported(self):
		pixels = [BACKGROUND] * (WIDTH * HEIGHT)
		with self.assertRaises(AssertionError) as raised:
			self.analyse(pixels)
		self.assertIn("probe border was not found", str(raised.exception))


if __name__ == "__main__":
	unittest.main()
