from __future__ import annotations

import importlib.util
import math
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = REPO_ROOT / "qmclient_scripts"
FRAMES_PATH = SCRIPTS / "qm_build_icon_morph_frames.py"
FONTS_DIR = REPO_ROOT / "data/qmclient/fonts/Phosphor"
CODEPOINTS = REPO_ROOT / "datasrc/qm_icons/phosphor.codepoints"
ATLAS_JSON = REPO_ROOT / "data/qmclient/icons/qm_icons_bold_msdf.json"
ATLAS_PNG = REPO_ROOT / "data/qmclient/icons/qm_icons_bold_msdf.png"
# 未清理的图集背景色。它的 alpha 在 MTSDF 真 SDF 语义下是「远在形状内部」，
# 一旦被绘制框采到就会渲染成实色（实机上的那圈白框）。
UNTOUCHED_BACKGROUND = (0, 0, 0, 255)
sys.path.insert(0, str(SCRIPTS))
SPEC = importlib.util.spec_from_file_location("qm_build_icon_morph_frames", FRAMES_PATH)
assert SPEC is not None and SPEC.loader is not None
FRAMES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FRAMES)
MORPH = FRAMES.morph


class QmBuildIconMorphFramesTest(unittest.TestCase):
    def test_progress_values_span_the_full_range(self) -> None:
        values = FRAMES.frame_progress_values(FRAMES.FRAME_COUNT)
        self.assertEqual(len(values), FRAMES.FRAME_COUNT)
        self.assertEqual(values[0], 0.0)
        self.assertEqual(values[-1], 1.0)
        self.assertTrue(all(b > a for a, b in zip(values, values[1:])))

    def test_every_quad_is_wound_consistently(self) -> None:
        """非零环绕要求绕向一致：反向四边形会在重叠处抵消、把形状挖出空洞。"""
        surfaces = MORPH.build_plan(FONTS_DIR, CODEPOINTS)
        for progress in FRAMES.frame_progress_values():
            contours = FRAMES.frame_contours(surfaces, progress)
            self.assertGreater(len(contours), 0)
            for contour in contours:
                self.assertEqual(len(contour), 4)
                self.assertGreater(FRAMES.quad_signed_area(contour), 0.0)

    def test_first_frame_reproduces_the_eye_bounds(self) -> None:
        """p=0 的帧必须就是眼睛字形本身（否则动画起点会与静态图标不一致）。"""
        codepoints = MORPH.load_codepoints(CODEPOINTS)
        eye_outer = MORPH.read_icon(FONTS_DIR, codepoints, "eye")[0]
        surfaces = MORPH.build_plan(FONTS_DIR, CODEPOINTS)
        contours = FRAMES.frame_contours(surfaces, 0.0)
        xs = [point[0] for contour in contours for point in contour]
        ys = [point[1] for contour in contours for point in contour]
        eye_xs = [point[0] for point in eye_outer]
        eye_ys = [point[1] for point in eye_outer]
        self.assertAlmostEqual(min(xs), min(eye_xs), delta=1.0)
        self.assertAlmostEqual(max(xs), max(eye_xs), delta=1.0)
        self.assertAlmostEqual(min(ys), min(eye_ys), delta=1.0)
        self.assertAlmostEqual(max(ys), max(eye_ys), delta=1.0)

    def test_endpoint_frames_use_the_real_glyphs(self) -> None:
        """首末帧必须是真实字形：几何 morph 的端点带扫掠过填，直接当端点会与静态图标跳变。"""
        codepoints = MORPH.load_codepoints(CODEPOINTS)
        for name in MORPH.ICON_NAMES:
            contours = FRAMES.glyph_frame_contours(FONTS_DIR, codepoints, name)
            sampled = MORPH.read_icon(FONTS_DIR, codepoints, name)
            glyph_xs = [point[0] for contour in contours for point in contour]
            glyph_ys = [point[1] for contour in contours for point in contour]
            sampled_xs = [point[0] for contour in sampled for point in contour]
            sampled_ys = [point[1] for contour in sampled for point in contour]
            self.assertAlmostEqual(min(glyph_xs), min(sampled_xs), delta=1.5)
            self.assertAlmostEqual(max(glyph_xs), max(sampled_xs), delta=1.5)
            self.assertAlmostEqual(min(glyph_ys), min(sampled_ys), delta=1.5)
            self.assertAlmostEqual(max(glyph_ys), max(sampled_ys), delta=1.5)
            # 真字形是展平后的完整轮廓，点数明显多于 64 点采样（且保留了洞的绕向）。
            self.assertGreater(sum(len(contour) for contour in contours), 64 * len(contours))

    def test_temp_font_contains_one_glyph_per_frame(self) -> None:
        from fontTools.ttLib import TTFont

        with tempfile.TemporaryDirectory(prefix="qm-morph-frames-test-") as temp:
            font_path = Path(temp) / "frames.ttf"
            codepoints, progress_values = FRAMES.build_frame_font_from_plan(font_path, FONTS_DIR, CODEPOINTS)
            self.assertEqual(len(codepoints), FRAMES.FRAME_COUNT)
            self.assertEqual(len(progress_values), FRAMES.FRAME_COUNT)
            font = TTFont(str(font_path))
            cmap = font.getBestCmap()
            for codepoint in codepoints:
                self.assertIn(codepoint, cmap)
                glyph = font["glyf"][cmap[codepoint]]
                self.assertGreater(glyph.numberOfContours, 0)
                self.assertGreater(glyph.xMax, glyph.xMin)
                self.assertGreater(glyph.yMax, glyph.yMin)

    def test_font_units_round_trip_matches_the_view_box(self) -> None:
        for point in ((0.0, 0.0), (128.0, 128.0), (256.0, 256.0)):
            font_x, font_y = FRAMES.to_font_units(point)
            self.assertAlmostEqual(font_x / (MORPH.FONT_UNITS_PER_EM / MORPH.VIEWBOX_SIZE), point[0], places=6)
            self.assertAlmostEqual((MORPH.FONT_Y_FLIP - font_y) / (MORPH.FONT_UNITS_PER_EM / MORPH.VIEWBOX_SIZE), point[1], places=6)
            self.assertTrue(math.isfinite(font_x) and math.isfinite(font_y))

    def test_baked_boxes_never_sample_the_untouched_atlas_background(self) -> None:
        """防回归：绘制框内的像素必须全部来自贴进去的位图。

        帧的显示框按进度插值（且取 max 保证包住位图），所以中间帧的框比位图大 1~3px；
        若那几像素采到未清理的图集背景，就会沿框边渲染出一圈实色白边——真 SDF 下
        背景 alpha=1 表示「远在形状内部」，覆盖率恒为 1。修复方式是贴位图前把帧格清成
        (0,0,0,0)，本用例直接盯住这条不变量（静态图标的框等于位图，天然满足）。
        """
        from PIL import Image

        if not (ATLAS_PNG.is_file() and ATLAS_JSON.is_file()):
            self.skipTest("图集数据缺失（用 qm-icon-msdf-atlas 目标重新烘焙）")
        import json

        atlas = Image.open(ATLAS_PNG).convert("RGBA")
        manifest = json.loads(ATLAS_JSON.read_text(encoding="utf-8"))
        entries = list(manifest["icons"].items()) + [(frame["name"], frame) for frame in manifest["morph_frames"]]

        offenders: list[str] = []
        for name, entry in entries:
            x, y, w, h = entry["x"], entry["y"], entry["w"], entry["h"]
            colors = atlas.crop((x, y, x + w, y + h)).getcolors(maxcolors=1 << 16)
            leaked = 0 if colors is None else next((count for count, color in colors if color == UNTOUCHED_BACKGROUND), 0)
            if leaked:
                offenders.append(f"{name}: {leaked} 像素")
        self.assertEqual(offenders, [], f"绘制框采到了未清理的图集背景: {offenders[:8]}")

    def test_baked_boxes_fully_contain_the_frame_shape(self) -> None:
        """形状不能被绘制框裁掉：绘制框是运行时拉伸到屏幕矩形的 UV 矩形。

        框小于形状时会缺角，端点帧也会与静态图标不一致（端点要求框等于位图）。
        """
        from PIL import Image

        if not (ATLAS_PNG.is_file() and ATLAS_JSON.is_file()):
            self.skipTest("图集数据缺失（用 qm-icon-msdf-atlas 目标重新烘焙）")
        import json

        atlas = Image.open(ATLAS_PNG).convert("RGBA")
        manifest = json.loads(ATLAS_JSON.read_text(encoding="utf-8"))
        for frame in manifest["morph_frames"]:
            with self.subTest(frame=frame["name"]):
                x, y, w, h = frame["x"], frame["y"], frame["w"], frame["h"]
                ink = atlas.crop((x, y, x + w, y + h)).getchannel("A").point(lambda value: 255 if value >= 128 else 0)
                bbox = ink.getbbox()
                self.assertIsNotNone(bbox, "帧里没有不透明像素（形状被裁没了）")
                left, top, right, bottom = bbox
                self.assertGreaterEqual(left, 0)
                self.assertGreaterEqual(top, 0)
                self.assertLessEqual(right, w)
                self.assertLessEqual(bottom, h)
                # 形状贴边就意味着被框裁掉：真 SDF 至少留出一个消隐像素
                self.assertGreater(left, 0)
                self.assertGreater(top, 0)
                self.assertLess(right, w)
                self.assertLess(bottom, h)


if __name__ == "__main__":
    unittest.main()
