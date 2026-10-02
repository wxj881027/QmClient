from __future__ import annotations

import io
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib import runner


class GateRunnerConsoleTest(unittest.TestCase):
	def test_gbk_console_keeps_command_result_with_unencodable_output(self):
		for returncode in (0, 7):
			with self.subTest(returncode=returncode):
				raw = io.BytesIO()
				console = io.TextIOWrapper(raw, encoding="gbk", errors="strict")
				with mock.patch.object(sys, "stdout", console):
					code, output = runner.run([
						sys.executable,
						"-c",
						f"import sys; sys.stdout.buffer.write(b'diagnostic: ' + bytes([255,10])); sys.exit({returncode})",
					])
					console.flush()
				self.assertEqual(code, returncode)
				self.assertIn("diagnostic: \ufffd", output)
				self.assertIn(b"diagnostic: ?", raw.getvalue())

	def test_unicode_string_console_preserves_full_output(self):
		console = io.StringIO()
		with mock.patch.object(sys, "stdout", console):
			code, output = runner.run([
				sys.executable,
				"-c",
				"import sys; sys.stdout.buffer.write(b'diagnostic: ' + bytes([255,10]))",
			])
		self.assertEqual(code, 0)
		self.assertEqual(output, "diagnostic: \ufffd")
		self.assertIn("diagnostic: \ufffd", console.getvalue())


if __name__ == "__main__":
	unittest.main()
