"""校验 Google Benchmark 原始重复结果，保留单位和波动含义。"""

from __future__ import annotations

import math
import statistics


class ResultError(ValueError):
	pass


TIME_TO_NS = {"ns": 1.0, "us": 1000.0, "ms": 1_000_000.0, "s": 1_000_000_000.0}


def _number(value: object, field: str) -> float:
	if isinstance(value, bool) or not isinstance(value, (int, float)):
		raise ResultError(f"{field}: expected a number")
	try:
		value = float(value)
	except OverflowError as error:
		raise ResultError(f"{field}: number is too large") from error
	if not math.isfinite(value) or value < 0:
		raise ResultError(f"{field}: expected a finite nonnegative number")
	return float(value)


def _statistics(values: list[float]) -> dict:
	mean = statistics.mean(values)
	deviation = statistics.stdev(values) if len(values) > 1 else None
	return {
		"median_ns": statistics.median(values),
		"mean_ns": mean,
		"min_ns": min(values),
		"max_ns": max(values),
		"cv": deviation / mean if deviation is not None and mean > 0 else None,
	}


def summarize_results(document: object, expected: list[str], repetitions: int, *, smoke: bool = False) -> dict:
	if not expected or len(set(expected)) != len(expected):
		raise ResultError("expected cases must be nonempty and unique")
	if isinstance(repetitions, bool) or not isinstance(repetitions, int) or repetitions < 1:
		raise ResultError("repetitions must be a positive integer")
	if not isinstance(document, dict) or not isinstance(document.get("context"), dict):
		raise ResultError("missing benchmark context")
	rows = document.get("benchmarks")
	if not isinstance(rows, list) or not rows:
		raise ResultError("no benchmark results")
	grouped: dict[str, list[dict]] = {name: [] for name in expected}
	for row in rows:
		if not isinstance(row, dict):
			raise ResultError("invalid benchmark row")
		if row.get("error_occurred") or row.get("skipped"):
			raise ResultError(f"{row.get('name')}: {row.get('error_message') or row.get('skip_message') or 'skipped'}")
		if row.get("run_type") == "aggregate":
			# cv 等 aggregate 是无量纲比例，不能作为重复耗时样本。
			continue
		name = row.get("run_name")
		if row.get("run_type") != "iteration" or not isinstance(name, str) or name not in grouped:
			raise ResultError(f"unexpected benchmark case: {name}")
		grouped[name].append(row)

	cases = []
	for name, samples in grouped.items():
		if len(samples) != repetitions:
			raise ResultError(f"{name}: expected {repetitions} repetitions, got {len(samples)}")
		indices = [sample.get("repetition_index") for sample in samples]
		if any(type(index) is not int for index in indices) or sorted(indices) != list(range(repetitions)):
			raise ResultError(f"{name}: incomplete or duplicate repetition indices")
		cpu, real = [], []
		labels = set()
		for sample in samples:
			if type(sample.get("repetitions")) is not int or sample["repetitions"] != repetitions:
				raise ResultError(f"{name}: repetition count changed")
			iterations = sample.get("iterations")
			if type(iterations) is not int or iterations <= 0:
				raise ResultError(f"{name}: no measured iterations")
			unit = sample.get("time_unit")
			factor = TIME_TO_NS.get(unit) if isinstance(unit, str) else None
			if factor is None:
				raise ResultError(f"{name}: unsupported time unit")
			cpu.append(_number(_number(sample.get("cpu_time"), f"{name}.cpu_time") * factor, f"{name}.cpu_time_ns"))
			real.append(_number(_number(sample.get("real_time"), f"{name}.real_time") * factor, f"{name}.real_time_ns"))
			for field in ("items_per_second", "bytes_per_second"):
				if not smoke and field in sample:
					_number(sample[field], f"{name}.{field}")
			label = sample.get("label", "")
			if not isinstance(label, str):
				raise ResultError(f"{name}: invalid measurement label")
			labels.add(label)
		if len(labels) != 1:
			raise ResultError(f"{name}: measurement label changed between repetitions")
		case = {"name": name, "repetitions": repetitions, "label": labels.pop()}
		if not smoke:
			case.update(cpu=_statistics(cpu), real=_statistics(real))
		cases.append(case)
	return {"context": document["context"], "cases": cases}
