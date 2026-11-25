"""clang-tidy 防回潮判据的回归测试。"""

from __future__ import annotations

import importlib.util
import json
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[3]
TOOLS = REPO_ROOT / "qmclient_scripts" / "gate" / "tools"


def load_tool(name):
	spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
	module = importlib.util.module_from_spec(spec)
	sys.modules[name] = module
	spec.loader.exec_module(module)
	return module


ratchet = load_tool("clang_tidy_ratchet")
build = load_tool("clang_tidy_build")


def log_line(path, line, column, check, message="something"):
	return f"{path}:{line}:{column}: warning: {message} [{check}]\n"


class ParseDiagnosticsTest(unittest.TestCase):
	def test_ci_absolute_path_is_relative_to_the_anchor(self):
		text = log_line(
			"/home/runner/work/QmClient/QmClient/src/base/io.cpp",
			12,
			3,
			"bugprone-bool-pointer-implicit-conversion",
		)
		diagnostics = ratchet.parse_diagnostics(text, Path("/home/runner/work/QmClient/QmClient"))
		self.assertEqual("src/base/io.cpp", diagnostics[0]["path"])

	def test_same_position_reported_by_different_tus_counts_once(self):
		line = log_line("src/engine/graphics.h", 1105, 102, "performance-unnecessary-value-param")
		diagnostics = ratchet.parse_diagnostics(line * 5, REPO_ROOT)
		self.assertEqual(1, len(diagnostics))

	def test_different_check_on_same_line_stays_separate(self):
		text = log_line("src/a.cpp", 7, 1, "bugprone-branch-clone") + log_line(
			"src/a.cpp", 7, 1, "readability-else-after-return"
		)
		self.assertEqual(2, len(ratchet.parse_diagnostics(text, REPO_ROOT)))

	def test_non_diagnostic_lines_are_ignored(self):
		text = "[12/648] Building CXX object x.o\n/usr/bin/ld: error: undefined reference\n"
		self.assertEqual([], ratchet.parse_diagnostics(text, REPO_ROOT))


class AddedLinesTest(unittest.TestCase):
	def test_multiple_hunks_and_pure_deletion(self):
		diff = (
			"diff --git a/src/a.cpp b/src/a.cpp\n"
			"--- a/src/a.cpp\n"
			"+++ b/src/a.cpp\n"
			"@@ -3,0 +4,2 @@\n"
			"@@ -20 +22,0 @@\n"
			"@@ -40,2 +42,3 @@\n"
			"diff --git a/src/b.h b/src/b.h\n"
			"--- a/src/b.h\n"
			"+++ b/src/b.h\n"
			"@@ -0,0 +1,5 @@\n"
		)
		added = ratchet.parse_added_lines(diff, REPO_ROOT)
		self.assertEqual([(4, 5), (42, 44)], added["src/a.cpp"])
		self.assertEqual([(1, 5)], added["src/b.h"])

	def test_deleted_file_is_dropped(self):
		diff = "--- a/src/gone.cpp\n+++ /dev/null\n@@ -1,4 +0,0 @@\n"
		self.assertEqual({}, ratchet.parse_added_lines(diff, REPO_ROOT))


class FilterAddedLinesTest(unittest.TestCase):
	def setUp(self):
		self.added = {"src/a.cpp": [(10, 12)]}
		self.inside = {"path": "src/a.cpp", "line": 11, "column": 1, "check": "x", "message": ""}
		self.outside = {"path": "src/a.cpp", "line": 13, "column": 1, "check": "x", "message": ""}
		self.other_file = {"path": "src/b.cpp", "line": 11, "column": 1, "check": "x", "message": ""}

	def test_only_diagnostics_on_added_lines_survive(self):
		hits = ratchet.filter_added_lines([self.inside, self.outside, self.other_file], self.added)
		self.assertEqual([self.inside], hits)

	def test_empty_added_lines_reports_nothing(self):
		self.assertEqual([], ratchet.filter_added_lines([self.inside], {}))


class BaselineCompareTest(unittest.TestCase):
	def test_current_above_baseline_is_a_violation(self):
		violations = ratchet.compare_with_baseline(
			{"src/a.cpp|x": 3}, {"per_check_counts": {"src/a.cpp|x": 2}}
		)
		self.assertEqual([{"key": "src/a.cpp|x", "current": 3, "baseline": 2}], violations)

	def test_equal_or_lower_is_allowed(self):
		baseline = {"per_check_counts": {"src/a.cpp|x": 2, "src/b.cpp|y": 4}}
		self.assertEqual(
			[], ratchet.compare_with_baseline({"src/a.cpp|x": 2, "src/b.cpp|y": 1}, baseline)
		)

	def test_unregistered_key_is_a_violation(self):
		violations = ratchet.compare_with_baseline({"src/new.cpp|z": 1}, {"per_check_counts": {}})
		self.assertEqual(1, len(violations))

	def test_write_and_load_round_trip(self):
		with TemporaryDirectory() as directory:
			path = Path(directory) / "baseline.json"
			ratchet.write_baseline({"src/a.cpp|x": 2}, path, "test", 2)
			baseline = ratchet.load_baseline(path)
			self.assertEqual({"src/a.cpp|x": 2}, baseline["per_check_counts"])
			self.assertEqual(2, baseline["total_hits"])
			self.assertEqual(2, json.loads(path.read_text(encoding="utf-8"))["total_hits"])

	def test_missing_baseline_treats_everything_as_new(self):
		with TemporaryDirectory() as directory:
			baseline = ratchet.load_baseline(Path(directory) / "absent.json")
		self.assertEqual({}, baseline["per_check_counts"])


class ApplyRatchetTest(unittest.TestCase):
	def _write_log(self, directory, text):
		path = Path(directory) / "clang-tidy.log"
		path.write_text(text, encoding="utf-8")
		return path

	def test_incremental_reports_diagnostics_on_changed_lines_only(self):
		with TemporaryDirectory() as directory:
			log = self._write_log(
				directory,
				log_line("src/a.cpp", 5, 1, "bugprone-x") + log_line("src/a.cpp", 90, 1, "bugprone-y"),
			)
			with mock.patch.object(build, "DIFF_BASE", "origin/master"), mock.patch.object(
				ratchet, "changed_line_ranges", return_value={"src/a.cpp": [(5, 5)]}
			):
				code = build.apply_ratchet(0, log, incremental=True)
		self.assertEqual(1, code)

	def test_incremental_without_hits_passes(self):
		with TemporaryDirectory() as directory:
			log = self._write_log(directory, log_line("src/a.cpp", 90, 1, "bugprone-x"))
			with mock.patch.object(build, "DIFF_BASE", "origin/master"), mock.patch.object(
				ratchet, "changed_line_ranges", return_value={"src/a.cpp": [(5, 5)]}
			):
				code = build.apply_ratchet(0, log, incremental=True)
		self.assertEqual(0, code)

	def test_full_scan_over_baseline_fails(self):
		with TemporaryDirectory() as directory:
			log = self._write_log(
				directory,
				"".join(log_line("src/a.cpp", line, 1, "bugprone-x") for line in (5, 9, 13)),
			)
			baseline = Path(directory) / "baseline.json"
			ratchet.write_baseline({"src/a.cpp|bugprone-x": 1}, baseline, "test", 1)
			with mock.patch.object(build, "BASELINE_PATH", baseline), mock.patch.object(
				build, "UPDATE_BASELINE", False
			):
				code = build.apply_ratchet(0, log, incremental=False)
		self.assertEqual(1, code)

	def test_update_mode_rewrites_baseline_and_passes(self):
		with TemporaryDirectory() as directory:
			log = self._write_log(directory, log_line("src/a.cpp", 5, 1, "bugprone-x"))
			baseline = Path(directory) / "baseline.json"
			with mock.patch.object(build, "BASELINE_PATH", baseline), mock.patch.object(
				build, "UPDATE_BASELINE", True
			):
				code = build.apply_ratchet(0, log, incremental=False)
			self.assertEqual(0, code)
			self.assertEqual(1, ratchet.load_baseline(baseline)["total_hits"])

	def test_build_failure_short_circuits(self):
		with TemporaryDirectory() as directory:
			log = self._write_log(directory, log_line("src/a.cpp", 5, 1, "bugprone-x"))
			self.assertEqual(7, build.apply_ratchet(7, log, incremental=True))

	def test_ratchet_can_be_disabled(self):
		with TemporaryDirectory() as directory:
			log = self._write_log(directory, log_line("src/a.cpp", 5, 1, "bugprone-x"))
			with mock.patch.object(build, "RATCHET_ENABLED", False):
				self.assertEqual(0, build.apply_ratchet(0, log, incremental=True))


if __name__ == "__main__":
	unittest.main()
