"""Benchmark 编排器与外部测量进程的集成测试；不测客户端。"""

from __future__ import annotations

import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

from qmclient_scripts.benchmark.results import ResultError
from qmclient_scripts.benchmark.run import RunError, execute_benchmarks, main


# fake 仅实现进程协议，用真实子进程验证参数、日志、失败和超时传播。
FAKE_PROGRAM = r"""
import json
from pathlib import Path
import sys
import time

scenario = sys.argv[1]
flags = dict(arg.split("=", 1) for arg in sys.argv[2:] if "=" in arg)
if "--benchmark_list_tests" in flags:
    if scenario != "empty":
        print("fake/16")
    raise SystemExit(0)
if scenario == "exit":
    print("measurement failed", file=sys.stderr)
    raise SystemExit(7)
if scenario == "timeout":
    print("started", flush=True)
    time.sleep(5)
if scenario == "missing":
    raise SystemExit(0)
output = Path(flags["--benchmark_out"])
if scenario == "malformed":
    output.write_text("{", encoding="utf-8")
    raise SystemExit(0)
repetitions = 1 if flags["--benchmark_dry_run"] == "true" else int(flags["--benchmark_repetitions"])
rows = [{"name": "fake/16", "run_name": "fake/16", "run_type": "iteration", "repetitions": repetitions, "repetition_index": i, "iterations": 1, "cpu_time": 100 + i, "real_time": 200 + i, "time_unit": "ns"} for i in range(repetitions)]
if scenario == "error":
    rows[0].update(error_occurred=True, error_message="font unavailable")
if scenario == "partial":
    rows.pop()
build_type = "debug" if scenario == "debug" else "release"
payload = {"context": {"library_build_type": build_type}, "benchmarks": rows}
if scenario == "ansi":
    payload["context"]["host_name"] = bytes([0x80]).decode("mbcs")
    output.write_bytes(json.dumps(payload, ensure_ascii=False).encode("mbcs"))
else:
    output.write_text(json.dumps(payload), encoding="utf-8")
"""


class BenchmarkRunnerIntegrationTest(unittest.TestCase):
	def setUp(self) -> None:
		self.directory = tempfile.TemporaryDirectory(prefix="qm-benchmark-runner-")
		self.addCleanup(self.directory.cleanup)
		self.root = Path(self.directory.name)
		self.program = self.root / "fake measurement.py"
		self.program.write_text(FAKE_PROGRAM, encoding="utf-8")
		self.output = self.root / "results with spaces"
		self.output.mkdir()

	def execute(self, scenario: str, **options: object) -> dict:
		return execute_benchmarks([sys.executable, str(self.program), scenario], self.root, self.output, pattern="^fake/", **options)

	def test_valid_process_results_are_summarized_and_original_json_is_preserved(self) -> None:
		summary = self.execute("valid", repetitions=3)
		self.assertEqual(summary["status"], "MEASURED")
		self.assertEqual(summary["cases"][0]["cpu"]["median_ns"], 101)
		self.assertEqual(len(json.loads((self.output / "results.json").read_text())["benchmarks"]), 3)
		command = json.loads((self.output / "command.json").read_text())["command"]
		self.assertIn("--benchmark_report_aggregates_only=false", command)
		self.assertIn("--benchmark_enable_random_interleaving=true", command)

	@unittest.skipUnless(os.name == "nt", "Windows ANSI output")
	def test_windows_native_ansi_context_records_fallback_and_preserves_raw_bytes(self) -> None:
		summary = self.execute("ansi", repetitions=3)
		self.assertEqual(summary["json_encoding"], "mbcs")
		raw = (self.output / "results.json").read_bytes()
		self.assertIn(bytes([0x80]), raw)
		self.assertEqual(summary["cases"][0]["cpu"]["median_ns"], 101)

	def test_empty_filter_stops_before_measurement(self) -> None:
		with self.assertRaisesRegex(RunError, "matched no cases"):
			self.execute("empty")
		self.assertFalse((self.output / "benchmark.log").exists())

	def test_nonzero_process_exit_preserves_failure_log(self) -> None:
		with self.assertRaisesRegex(RunError, "exited with 7"):
			self.execute("exit")
		self.assertIn("measurement failed", (self.output / "benchmark.log").read_text())

	def test_timeout_terminates_own_measurement_process_and_keeps_partial_log(self) -> None:
		with self.assertRaisesRegex(RunError, "timed out"):
			self.execute("timeout", timeout=0.4)
		self.assertIn("started", (self.output / "benchmark.log").read_text())

	def test_zero_exit_without_json_is_not_success(self) -> None:
		with self.assertRaises(FileNotFoundError):
			self.execute("missing")

	def test_malformed_json_is_not_success(self) -> None:
		with self.assertRaises(json.JSONDecodeError):
			self.execute("malformed")

	def test_zero_exit_with_native_error_is_not_success(self) -> None:
		with self.assertRaisesRegex(ResultError, "font unavailable"):
			self.execute("error")

	def test_partial_repetitions_are_not_success(self) -> None:
		with self.assertRaisesRegex(ResultError, "expected 5 repetitions, got 4"):
			self.execute("partial")

	def test_smoke_is_distinct_from_performance_measurement(self) -> None:
		summary = self.execute("valid", smoke=True)
		self.assertEqual(summary["status"], "SMOKE")
		self.assertEqual(summary["cases"][0]["repetitions"], 1)
		self.assertNotIn("cpu", summary["cases"][0])

	def test_debug_library_can_smoke_but_cannot_claim_release_measurement(self) -> None:
		with self.assertRaisesRegex(RunError, "not built in release"):
			self.execute("debug")
		summary = self.execute("debug", smoke=True)
		self.assertEqual(summary["status"], "SMOKE")

	def test_existing_output_is_preserved_instead_of_overwriting_baseline(self) -> None:
		marker = self.output / "results.json"
		marker.write_text("baseline", encoding="utf-8")
		self.assertEqual(main(["--filter", ".*", "--output-dir", str(self.output)]), 1)
		self.assertEqual(marker.read_text(), "baseline")


if __name__ == "__main__":
	unittest.main()
