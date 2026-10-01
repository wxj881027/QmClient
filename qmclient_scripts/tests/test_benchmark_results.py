"""Benchmark 重复样本与摘要的单元行为测试。"""

from __future__ import annotations

import unittest

from qmclient_scripts.benchmark.results import ResultError, summarize_results


def sample(index: int, value: float, unit: str = "ns", name: str = "work/16", repetitions: int = 3) -> dict:
	return {"name": name, "run_name": name, "run_type": "iteration", "repetitions": repetitions, "repetition_index": index, "iterations": 100, "cpu_time": value, "real_time": value * 2, "time_unit": unit}


def document(rows: list[dict]) -> dict:
	return {"context": {"build_type": "release"}, "benchmarks": rows}


class BenchmarkResultsTest(unittest.TestCase):
	def setUp(self) -> None:
		self.rows = [sample(0, 100), sample(1, 300), sample(2, 200)]

	def summarize(self, rows: list[dict]) -> dict:
		return summarize_results(document(rows), ["work/16"], 3)

	def test_unordered_repetitions_preserve_median_and_variation(self) -> None:
		case = self.summarize(self.rows)["cases"][0]
		self.assertEqual(case["cpu"]["median_ns"], 200)
		self.assertEqual(case["real"]["median_ns"], 400)
		self.assertEqual(case["cpu"]["cv"], 0.5)

	def test_units_normalize_each_sample_before_aggregation(self) -> None:
		rows = [sample(0, 1000), sample(1, 2, "us"), sample(2, 0.003, "ms")]
		self.assertEqual(self.summarize(rows)["cases"][0]["cpu"]["median_ns"], 2000)

	def test_percentage_aggregate_is_not_a_timing_sample(self) -> None:
		aggregate = {"run_type": "aggregate", "aggregate_name": "cv", "aggregate_unit": "percentage", "cpu_time": 0.99, "real_time": 0.99}
		self.assertEqual(self.summarize(self.rows + [aggregate])["cases"][0]["cpu"]["median_ns"], 200)

	def test_error_row_fails_instead_of_producing_success(self) -> None:
		self.rows[1]["error_occurred"] = True
		self.rows[1]["error_message"] = "missing font"
		with self.assertRaisesRegex(ResultError, "missing font"):
			self.summarize(self.rows)

	def test_skipped_row_is_not_a_successful_repetition(self) -> None:
		self.rows[1]["skipped"] = True
		with self.assertRaisesRegex(ResultError, "skipped"):
			self.summarize(self.rows)

	def test_missing_case_fails_even_when_another_case_has_all_samples(self) -> None:
		with self.assertRaisesRegex(ResultError, "other.*got 0"):
			summarize_results(document(self.rows), ["work/16", "other"], 3)

	def test_duplicate_repetition_does_not_replace_missing_repetition(self) -> None:
		self.rows[2]["repetition_index"] = 1
		with self.assertRaisesRegex(ResultError, "duplicate repetition"):
			self.summarize(self.rows)

	def test_aggregates_without_raw_samples_cannot_claim_measurement(self) -> None:
		with self.assertRaisesRegex(ResultError, "got 0"):
			self.summarize([{"run_type": "aggregate", "aggregate_name": "median"}])

	def test_invalid_timing_values_are_rejected(self) -> None:
		for value in [None, True, -1, float("nan"), float("inf"), "100"]:
			with self.subTest(value=value):
				self.rows[0]["cpu_time"] = value
				with self.assertRaises(ResultError):
					self.summarize(self.rows)

	def test_unit_scaling_overflow_is_not_reported_as_a_finite_time(self) -> None:
		self.rows[0].update(cpu_time=1e308, time_unit="s")
		with self.assertRaises(ResultError):
			self.summarize(self.rows)

	def test_smoke_ignores_unmeasured_rate_and_emits_no_timing_metrics(self) -> None:
		row = sample(0, 0, repetitions=1)
		row["items_per_second"] = float("inf")
		summary = summarize_results(document([row]), ["work/16"], 1, smoke=True)
		self.assertNotIn("cpu", summary["cases"][0])
		self.assertNotIn("real", summary["cases"][0])

	def test_zero_iterations_cannot_count_as_a_measured_sample(self) -> None:
		self.rows[0]["iterations"] = 0
		with self.assertRaisesRegex(ResultError, "no measured iterations"):
			self.summarize(self.rows)

	def test_malformed_unit_is_reported_as_result_error(self) -> None:
		self.rows[0]["time_unit"] = []
		with self.assertRaisesRegex(ResultError, "time unit"):
			self.summarize(self.rows)

	def test_malformed_label_is_reported_as_result_error(self) -> None:
		self.rows[0]["label"] = []
		with self.assertRaisesRegex(ResultError, "label"):
			self.summarize(self.rows)

	def test_changed_measurement_label_fails_between_repetitions(self) -> None:
		self.rows[0]["label"] = "cache-hit"
		with self.assertRaisesRegex(ResultError, "label changed"):
			self.summarize(self.rows)

	def test_single_smoke_sample_does_not_invent_variation(self) -> None:
		case = summarize_results(document([sample(0, 100, repetitions=1)]), ["work/16"], 1)["cases"][0]
		self.assertIsNone(case["cpu"]["cv"])

	def test_unexpected_raw_case_fails(self) -> None:
		with self.assertRaisesRegex(ResultError, "unexpected benchmark"):
			self.summarize(self.rows + [sample(0, 100, name="other")])

	def test_no_results_fails(self) -> None:
		with self.assertRaisesRegex(ResultError, "no benchmark results"):
			self.summarize([])


if __name__ == "__main__":
	unittest.main()
