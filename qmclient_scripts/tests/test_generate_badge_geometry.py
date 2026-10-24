import importlib.util
import pathlib
import unittest


SPEC = importlib.util.spec_from_file_location("badge_geometry", pathlib.Path(__file__).resolve().parents[1] / "generate_badge_geometry.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def area(quads):
    return sum(((q[2] - q[0]) + (q[6] - q[4])) * (q[5] - q[1]) / 2 for q in quads)


class BadgeGeometryGenerationTest(unittest.TestCase):
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
