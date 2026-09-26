from __future__ import annotations

import importlib.util
import math
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "qmclient_scripts/qm_build_icon_morph.py"
FONTS_DIR = REPO_ROOT / "data/qmclient/fonts/Phosphor"
CODEPOINTS = REPO_ROOT / "datasrc/qm_icons/phosphor.codepoints"
sys.path.insert(0, str(SCRIPT_PATH.parent))
SPEC = importlib.util.spec_from_file_location("qm_build_icon_morph", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
MORPH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MORPH)


def resolve(path, index: int, progress: float):
    """与 renderer 的 ResolveQmIconMorphPoint 等价。"""
    progress = max(-0.25, min(1.25, progress))
    clamped = max(0, min(MORPH.SAMPLE_COUNT - 1, index))
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


def surface_band_area(surface, progress: float) -> float:
    """表面在给定进度下的条带总面积（鞋带公式），也是「是否塌缩成碎片」的判据。"""
    total = 0.0
    for index in range(MORPH.SAMPLE_COUNT):
        nxt = (index + 1) % MORPH.SAMPLE_COUNT
        corners = [
            resolve(surface.outer, index, progress),
            resolve(surface.outer, nxt, progress),
            resolve(surface.inner, nxt, progress),
            resolve(surface.inner, index, progress),
        ]
        total += abs(
            sum(
                corners[i][0] * corners[(i + 1) % 4][1] - corners[(i + 1) % 4][0] * corners[i][1]
                for i in range(4)
            )
        ) * 0.5
    return total


class QmBuildIconMorphTest(unittest.TestCase):
    def test_contours_match_the_documented_topology(self) -> None:
        codepoints = MORPH.load_codepoints(CODEPOINTS)
        for name in MORPH.ICON_NAMES:
            with self.subTest(name=name):
                contours = MORPH.read_icon(FONTS_DIR, codepoints, name)
                self.assertEqual(len(contours), MORPH.EXPECTED_CONTOUR_COUNTS[name])
                for contour in contours:
                    self.assertEqual(len(contour), MORPH.SAMPLE_COUNT)
                    for x, y in contour:
                        self.assertGreaterEqual(x, -1.0)
                        self.assertLessEqual(x, MORPH.VIEWBOX_SIZE + 1.0)
                        self.assertGreaterEqual(y, -1.0)
                        self.assertLessEqual(y, MORPH.VIEWBOX_SIZE + 1.0)

    def test_eye_slash_orientation_is_not_mirrored(self) -> None:
        """防回归：字体是 y 向上、视图框是 y 向下，漏掉翻转会让斜线方向相反。

        eye 上下对称无法区分；用 eye-slash 的不对称性判定——斜线的远端在右下、
        斜线另一端伸到左上角之外。
        """
        codepoints = MORPH.load_codepoints(CODEPOINTS)
        contours = MORPH.read_icon(FONTS_DIR, codepoints, "eye-slash")
        union_outline = contours[0]
        topmost = min(union_outline, key=lambda point: point[1])
        rightmost = max(union_outline, key=lambda point: point[0])
        self.assertLess(topmost[0], MORPH.VIEWBOX_CENTER, "斜线上端应偏左（否则数据被上下镜像了）")
        self.assertGreater(rightmost[1], MORPH.VIEWBOX_CENTER, "斜线远端应偏下（否则数据被上下镜像了）")

    def test_surface_layout_indexes_are_within_the_contour_count(self) -> None:
        for name in MORPH.ICON_NAMES:
            count = MORPH.EXPECTED_CONTOUR_COUNTS[name]
            for role, outer, inner in MORPH.SURFACE_LAYOUT[name]:
                with self.subTest(name=name, role=role):
                    self.assertIn(role, ("ring", "band"))
                    self.assertLess(outer, count)
                    if inner is not None:
                        self.assertLess(inner, count)

    def test_band_contour_splits_into_two_arcs_sharing_their_tips(self) -> None:
        codepoints = MORPH.load_codepoints(CODEPOINTS)
        contours = MORPH.read_icon(FONTS_DIR, codepoints, "eye-slash")
        crescent = contours[MORPH.SURFACE_LAYOUT["eye-slash"][1][1]]
        outer, inner = MORPH.split_band_contour(crescent)
        self.assertEqual(len(outer), MORPH.SAMPLE_COUNT)
        self.assertEqual(len(inner), MORPH.SAMPLE_COUNT)
        # 两条弧在两端尖点处重合（否则条带会开口）
        self.assertLess(math.dist(outer[0], inner[0]), 0.5)
        self.assertLess(math.dist(outer[-1], inner[-1]), 0.5)
        # 外弧离质心更远
        center = MORPH.centroid(crescent)
        mean_outer = sum(math.dist(point, center) for point in outer) / len(outer)
        mean_inner = sum(math.dist(point, center) for point in inner) / len(inner)
        self.assertGreater(mean_outer, mean_inner)

    def test_band_surface_reproduces_the_crescent_area(self) -> None:
        """端点保真：条带在 p=1 的面积必须等于新月形的实际面积。

        若把新月当成实心形状用退化内轮廓填充，月牙凹侧会被填上，面积明显偏大。
        """
        codepoints = MORPH.load_codepoints(CODEPOINTS)
        contours = MORPH.read_icon(FONTS_DIR, codepoints, "eye-slash")
        crescent = contours[MORPH.SURFACE_LAYOUT["eye-slash"][1][1]]
        outer, inner = MORPH.split_band_contour(crescent)
        band_area = 0.0
        for index in range(MORPH.SAMPLE_COUNT):
            nxt = (index + 1) % MORPH.SAMPLE_COUNT
            corners = [outer[index], outer[nxt], inner[nxt], inner[index]]
            band_area += abs(
                sum(
                    corners[i][0] * corners[(i + 1) % 4][1] - corners[(i + 1) % 4][0] * corners[i][1]
                    for i in range(4)
                )
            ) * 0.5
        crescent_area = abs(
            sum(
                crescent[i][0] * crescent[(i + 1) % MORPH.SAMPLE_COUNT][1]
                - crescent[(i + 1) % MORPH.SAMPLE_COUNT][0] * crescent[i][1]
                for i in range(MORPH.SAMPLE_COUNT)
            )
        ) * 0.5
        self.assertLess(abs(band_area - crescent_area) / crescent_area, 0.05)

    def test_every_surface_shares_one_rigid_transform(self) -> None:
        """防回归：内外轮廓各持一套 θ/σ 会差速扭转，把连接带拧成碎片。"""
        for role, index, surface in MORPH.build_plan(FONTS_DIR, CODEPOINTS):
            with self.subTest(role=role, index=index):
                self.assertAlmostEqual(surface.outer.theta, surface.inner.theta, places=6)
                self.assertAlmostEqual(surface.outer.log_scale, surface.inner.log_scale, places=6)

    def test_no_surface_collapses_into_subpixel_fragments(self) -> None:
        """防回归：整个表面同时塌缩时，64 条带退化成亚像素碎片，实机上渲染为散点。

        上一版 plan 的虹膜环就是整体塌缩（外/内轮廓同时收到一点），判据是任一进度下
        条带面积不得接近零。
        """
        progress_values = [step / 50.0 for step in range(51)]
        for role, index, surface in MORPH.build_plan(FONTS_DIR, CODEPOINTS):
            with self.subTest(role=role, index=index):
                areas = [surface_band_area(surface, progress) for progress in progress_values]
                self.assertGreater(min(areas), 100.0, f"band collapses at progress {progress_values[areas.index(min(areas))]:.2f}")

    def test_surface_pairing_is_by_index(self) -> None:
        source_layout = MORPH.SURFACE_LAYOUT["eye"]
        target_layout = MORPH.SURFACE_LAYOUT["eye-slash"]
        self.assertEqual(len(source_layout), len(target_layout))
        plan = MORPH.build_plan(FONTS_DIR, CODEPOINTS)
        self.assertEqual(len(plan), len(source_layout))
        for index, (role, plan_index, _) in enumerate(plan):
            self.assertEqual(plan_index, index)
            self.assertEqual(role, source_layout[index][0])

    def test_progress_zero_reconstructs_the_source_contour(self) -> None:
        codepoints = MORPH.load_codepoints(CODEPOINTS)
        source = MORPH.read_icon(FONTS_DIR, codepoints, "eye")
        surfaces = MORPH.build_plan(FONTS_DIR, CODEPOINTS)
        outer = surfaces[0][2].outer
        for index, expected in enumerate(source[MORPH.SURFACE_LAYOUT["eye"][0][1]]):
            actual = resolve(outer, index, 0.0)
            self.assertAlmostEqual(actual[0], expected[0], places=4)
            self.assertAlmostEqual(actual[1], expected[1], places=4)

    def test_progress_one_reconstructs_the_target_contour(self) -> None:
        """p=1 必须重现目标轮廓；对应关系允许循环移位（形状等价）。"""
        codepoints = MORPH.load_codepoints(CODEPOINTS)
        target = MORPH.read_icon(FONTS_DIR, codepoints, "eye-slash")
        contour = target[MORPH.SURFACE_LAYOUT["eye-slash"][0][1]]
        surfaces = MORPH.build_plan(FONTS_DIR, CODEPOINTS)
        outer = surfaces[0][2].outer

        resolved = [resolve(outer, index, 1.0) for index in range(MORPH.SAMPLE_COUNT)]
        for point in resolved:
            self.assertLess(min(math.dist(point, expected) for expected in contour), 1.0)
        for expected in contour:
            self.assertLess(min(math.dist(point, expected) for point in resolved), 1.0)

    def test_midpoint_contains_polar_transform_not_raw_coordinate_lerp(self) -> None:
        surfaces = MORPH.build_plan(FONTS_DIR, CODEPOINTS)
        path = surfaces[0][2].outer
        index = 17
        raw_lerp = (
            (path.source[index][0] + path.target[index][0]) * 0.5 + path.source_center[0],
            (path.source[index][1] + path.target[index][1]) * 0.5 + path.source_center[1],
        )
        actual = resolve(path, index, 0.5)
        self.assertGreater(math.dist(actual, raw_lerp), 0.01)

    def test_generator_emits_only_the_bold_plan(self) -> None:
        generated = MORPH.generate(FONTS_DIR, CODEPOINTS)
        self.assertIn("s_EyeMorphPlan_bold", generated)
        self.assertIn("sizeof(s_aEyeMorph_bold_surfaces)", generated)
        for weight in ("thin", "light", "regular", "fill", "duotone"):
            self.assertNotIn(f"s_EyeMorphPlan_{weight}", generated)
        self.assertIn(f"static constexpr float s_aEyeMorph_bold_ring_0_outer_source[{MORPH.SAMPLE_COUNT * 2}]", generated)

    def test_unknown_icon_name_is_rejected(self) -> None:
        codepoints = MORPH.load_codepoints(CODEPOINTS)
        with self.assertRaises(ValueError):
            MORPH.read_icon(FONTS_DIR, codepoints, "definitely-not-an-icon")

    def test_input_field_keeps_morph_before_the_existing_icon_fallback(self) -> None:
        source = (REPO_ROOT / "src/game/client/QmUi/UiForms.cpp").read_text(encoding="utf-8")
        morph = source.index("RenderQmEyeMorph")
        fallback = source.index("Ctx.m_pIconManager->RenderIcon", morph)
        self.assertIn("QmIcon == static_cast<int>(EQmIcon::EYE)", source)
        self.assertIn("QmIcon == static_cast<int>(EQmIcon::EYE_OFF)", source)
        self.assertIn("g_Config.m_QmUiMotionLevel > 0", source)
        self.assertIn("ui_token::motion::TOGGLE", source)
        self.assertIn("ResolveUiAnimSpringValue", source)
        self.assertIn("HasActiveAnimation(MorphNodeKey", source)
        self.assertLess(morph, fallback)


if __name__ == "__main__":
    unittest.main()
