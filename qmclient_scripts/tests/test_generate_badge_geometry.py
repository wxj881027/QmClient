import importlib.util
import pathlib
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location("badge_geometry", pathlib.Path(__file__).resolve().parents[1] / "generate_badge_geometry.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def area(quads):
    return sum(((q[2] - q[0]) + (q[6] - q[4])) * (q[5] - q[1]) / 2 for q in quads)


def polygon_area(quads):
    total = 0
    for q in quads:
        points = [(q[0], q[1]), (q[2], q[3]), (q[6], q[7]), (q[4], q[5])]
        total += abs(sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(points, points[1:] + points[:1]))) / 2
    return total


def developer_fixture(content):
    source = f'''<svg xmlns="http://www.w3.org/2000/svg">
      <path d="M0 0H16V16H0Z" fill="#19343B"/>
      <g id="developer-content" clip-path="url(#capsule)">
        <g id="developer-logo"><path d="M6 6H10V10H6Z" fill="#8BDFCF"/></g>
        <path d="{content}" fill="#D8FFF5"/>
      </g>
      <path d="M0 0H16V16H0Z" fill="#4B7D84" mask="url(#border)"/>
      <defs><clipPath id="unrelated"><path d="M0 0H16V16H0Z"/></clipPath>
      <clipPath id="capsule"><path d="M0 8L8 0L16 8L8 16Z"/></clipPath></defs>
    </svg>'''
    with tempfile.TemporaryDirectory() as directory:
        path = pathlib.Path(directory) / "developer.svg"
        path.write_text(source, encoding="utf-8")
        return MODULE.build_mesh(path, (0, 0, 4, 4), developer=True)


class BadgeGeometryGenerationTest(unittest.TestCase):
    def test_developer_content_excludes_background_border_and_shared_logo(self):
        quads = developer_fixture("M4 4H12V12H4Z")
        self.assertTrue(quads)
        self.assertEqual({color for _, color, _ in quads}, {"D8FFF5"})
        self.assertAlmostEqual(polygon_area([q for q, _, _ in quads]), 4)

    def test_developer_content_respects_the_svg_capsule_mask(self):
        quads = developer_fixture("M0 0H16V16H0Z")
        self.assertAlmostEqual(polygon_area([q for q, _, _ in quads]), 8)
        for q, _, _ in quads:
            for x, y in zip(q[::2], q[1::2]):
                self.assertLessEqual(abs(x - 2) + abs(y - 2), 2 + 1e-7)

    def test_rectangle_preserves_its_area(self):
        self.assertAlmostEqual(area(MODULE.trapezoids([[(0, 0), (4, 0), (4, 3), (0, 3)]], (0, 0, 4, 3))), 12)

    def test_opposite_winding_retains_the_letter_hole(self):
        outer = [(0, 0), (4, 0), (4, 4), (0, 4)]
        hole = [(1, 1), (1, 3), (3, 3), (3, 1)]
        self.assertAlmostEqual(area(MODULE.trapezoids([outer, hole], (0, 0, 4, 4))), 12)

    def test_same_winding_nested_fill_remains_solid(self):
        outer = [(0, 0), (4, 0), (4, 4), (0, 4)]
        inner = [(1, 1), (3, 1), (3, 3), (1, 3)]
        self.assertAlmostEqual(area(MODULE.trapezoids([outer, inner], (0, 0, 4, 4))), 16)

    def test_slice_clips_diagonal_edges_at_the_actual_intersection(self):
        triangle = [[(0, 0), (4, 0), (0, 4)]]
        quads = list(MODULE.trapezoids(triangle, (1, 0, 3, 4)))
        self.assertAlmostEqual(area(quads), 4)
        self.assertTrue(all(0 <= x <= 2 for q in quads for x in (q[0], q[2], q[4], q[6])))

    def test_cubic_flattening_keeps_endpoints_and_curvature(self):
        paths = MODULE.flatten("M0 0C0 4 4 4 4 0", 0.05)
        points, closed = paths[0]
        self.assertFalse(closed)
        self.assertEqual(points[0], (0, 0))
        self.assertEqual(points[-1], (4, 0))
        self.assertTrue(any(y > 2.5 for _, y in points))

    def test_round_stroke_caps_extend_beyond_the_line_endpoints(self):
        outlines = MODULE.stroke_outline([(2, 2), (6, 2)], False, 2)
        self.assertAlmostEqual(min(x for x, _ in outlines[0]), 1)
        self.assertAlmostEqual(max(x for x, _ in outlines[0]), 7)

    def test_unsupported_path_commands_fail_instead_of_silently_changing_the_icon(self):
        with self.assertRaises(ValueError):
            MODULE.flatten("M0 0Q1 1 2 2", 0.05)


if __name__ == "__main__":
    unittest.main()
