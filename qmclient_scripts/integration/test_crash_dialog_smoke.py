import unittest

try:
	from qmclient_scripts.integration.crash_dialog_smoke import (
		CLOSE_LABEL,
		PREVIEW_TYPES,
		REQUIRED_BUTTONS,
		REQUIRED_REPORT_SECTIONS,
		SMOKE_TESTS,
		WINDOW_CLASS,
		WINDOW_TITLE,
		WindowCapture,
	)
except ModuleNotFoundError:
	from crash_dialog_smoke import (  # type: ignore[no-redef]
		CLOSE_LABEL,
		PREVIEW_TYPES,
		REQUIRED_BUTTONS,
		REQUIRED_REPORT_SECTIONS,
		SMOKE_TESTS,
		WINDOW_CLASS,
		WINDOW_TITLE,
		WindowCapture,
	)


def make_capture(value: bytes, changed_rows: tuple[int, ...] = (), height: int = 4, width: int = 4) -> WindowCapture:
	pixels = bytearray()
	for row in range(height):
		fill = b"\xff\x00\x00\xff" if row in changed_rows else value
		pixels += fill * width
	return WindowCapture(width, height, bytes(pixels))


class CrashDialogSmokeRunnerTest(unittest.TestCase):
	def test_all_preview_types_are_registered(self):
		self.assertEqual(set(SMOKE_TESTS), {f"crash_dialog_{name}" for name in PREVIEW_TYPES})
		self.assertEqual(PREVIEW_TYPES, ("graphics", "assertion", "fatal", "hang"))

	def test_window_identity_constants_match_the_festive_dialog(self):
		self.assertEqual(WINDOW_CLASS, "QmClientFestiveCrashWindow")
		self.assertEqual(WINDOW_TITLE, "QmClient · 好运还在")

	def test_required_buttons_keep_report_close_action(self):
		self.assertIn(CLOSE_LABEL, REQUIRED_BUTTONS)

	def test_required_report_sections_cover_the_chinese_localization(self):
		self.assertEqual(REQUIRED_REPORT_SECTIONS, ("【问题判断】", "【建议操作】", "【中文化报告】"))

	def test_identical_captures_report_no_changed_pixels(self):
		capture = make_capture(b"\x01\x02\x03\x04")
		self.assertEqual(capture.changed_pixels(capture), 0)
		self.assertEqual(capture.signature(), make_capture(b"\x01\x02\x03\x04").signature())

	def test_changed_pixels_are_counted_only_where_frames_differ(self):
		before = make_capture(b"\x01\x02\x03\x04")
		after = make_capture(b"\x01\x02\x03\x04", changed_rows=(0, 1))
		self.assertEqual(before.changed_pixels(after), 2 * 4)

	def test_changed_pixels_detect_particle_animation_anywhere_in_window(self):
		before = make_capture(b"\x01\x02\x03\x04")
		top_change = make_capture(b"\x01\x02\x03\x04", changed_rows=(0,))
		self.assertGreater(before.changed_pixels(top_change), 0)

	def test_blank_capture_is_detected(self):
		self.assertTrue(make_capture(b"\x00\x00\x00\x00").is_uniform())
		self.assertFalse(make_capture(b"\x00\x00\x00\x00", changed_rows=(0,)).is_uniform())


if __name__ == "__main__":
	unittest.main()
