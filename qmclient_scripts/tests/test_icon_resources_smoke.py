from __future__ import annotations

import json
import unittest
import tempfile
from pathlib import Path

from qmclient_scripts.integration.icon_resources_smoke import REPO_ROOT, ttf_draws, icon_summary, perf_snapshot, wait_perf_summary


class IconResourceDiagnosticParsingTest(unittest.TestCase):
	def test_logger_prefixed_json_exposes_actual_draw_counts(self) -> None:
		line = '2026-10-07 12:00:00 I perf/icons: {"event":"icon_summary","ttf_draws":115}'
		self.assertEqual(ttf_draws(line), 115)

	def test_other_event_cannot_prove_icon_drawing(self) -> None:
		line = 'perf/icons: {"event":"text_summary","ttf_draws":115}'
		self.assertIsNone(icon_summary(line))
		self.assertIsNone(ttf_draws(line))

	def test_malformed_and_non_object_payloads_are_ignored(self) -> None:
		for line in ("perf/icons: {broken", 'perf/icons: {"event":"icon_summary"} trailing', "icon_summary", "[]", '{"event":null}'):
			with self.subTest(line=line):
				self.assertIsNone(icon_summary(line))
				self.assertIsNone(ttf_draws(line))

	def test_missing_count_is_unknown_rather_than_zero(self) -> None:
		line = '{"event":"icon_summary"}'
		self.assertIsNone(ttf_draws(line))

	def test_invalid_counts_cannot_prove_drawing(self) -> None:
		for value in (-1, True, False, "12", 1.5, None, [], {}):
			with self.subTest(value=value):
				line = json.dumps({"event": "icon_summary", "ttf_draws": value})
				self.assertIsNone(ttf_draws(line))


class IconResourcePerfFileObservationTest(unittest.TestCase):
	class AliveClient:
		def is_alive(self) -> bool:
			return True

	def setUp(self) -> None:
		self.profile = Path(tempfile.mkdtemp(prefix="icon-diagnostic-test-", dir=REPO_ROOT / "tmp"))
		self.directory = self.profile / "dumps/QmClient_Perf"
		self.directory.mkdir(parents=True)
		self.log = self.directory / "perf.log"

	def test_reader_ignores_success_record_before_command_snapshot(self) -> None:
		self.log.write_text('{"event":"icon_summary","ttf_draws":100}\n', encoding="utf-8")
		offsets = perf_snapshot(self.profile)
		with self.log.open("a", encoding="utf-8") as target:
			target.write('{"event":"icon_summary","ttf_draws":2}\n')
		line = wait_perf_summary(self.AliveClient(), self.profile, offsets, lambda value: (ttf_draws(value) or 0) > 0, "current frame")
		self.assertEqual(ttf_draws(line), 2)

	def test_reader_observes_file_created_after_snapshot(self) -> None:
		offsets = perf_snapshot(self.profile)
		self.log.write_text('{"event":"icon_summary","ttf_draws":3}\n', encoding="utf-8")
		line = wait_perf_summary(self.AliveClient(), self.profile, offsets, lambda value: (ttf_draws(value) or 0) > 0, "new fallback frame")
		self.assertEqual(ttf_draws(line), 3)

	def test_old_success_cannot_satisfy_wait_without_new_frame(self) -> None:
		self.log.write_text('{"event":"icon_summary","ttf_draws":100}\n', encoding="utf-8")
		with self.assertRaises(TimeoutError):
			wait_perf_summary(self.AliveClient(), self.profile, perf_snapshot(self.profile), lambda value: (ttf_draws(value) or 0) > 0, "missing frame", timeout=0)


if __name__ == "__main__":
	unittest.main()
