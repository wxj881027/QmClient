import unittest

try:
	from qmclient_scripts.integration.qmclient_smoke import SMOKE_TESTS, _format_exit_code, log_message
except ModuleNotFoundError:
	from qmclient_smoke import SMOKE_TESTS, _format_exit_code, log_message


class QmClientSmokeRunnerTest(unittest.TestCase):
	def test_log_message_strips_ddnet_prefix(self):
		self.assertEqual(
			log_message("2026-09-09 22:00:00 I server: version 19.9"),
			"server: version 19.9",
		)

	def test_log_message_keeps_non_ddnet_lines(self):
		self.assertEqual(log_message("plain output"), "plain output")

	def test_required_smoke_scenarios_are_registered(self):
		self.assertEqual(
			set(SMOKE_TESTS),
			{"plain_connection", "focus_configuration", "gores_configuration", "connection_shutdown"},
		)

	def test_exit_code_formatter_describes_missing_and_normal_exit(self):
		self.assertEqual(_format_exit_code(None), "<running>")
		self.assertIn("5", _format_exit_code(5))

	def test_exit_code_formatter_preserves_windows_style_hex_diagnostics(self):
		formatted = _format_exit_code(0xC0000005)
		if __import__("os").name == "nt":
			self.assertIn("0xC0000005", formatted)


if __name__ == "__main__":
	unittest.main()
