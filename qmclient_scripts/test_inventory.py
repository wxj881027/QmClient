#!/usr/bin/env python3
"""Summarize QmClient test layers and high-debt C++ test files."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re


TEST_MACRO = re.compile(r"^\s*TEST(?:_F|_P)?\s*\(", re.MULTILINE)
SOURCE_READ = re.compile(r"Read(?:TestSourceFile|RepoFile|HudNotificationTestFile|TextFile)|ExtractSource(?:FunctionBody|Block)")
PYTHON_CASE = re.compile(r"^\s*def\s+test\w*\s*\(", re.MULTILINE)
SMOKE_SCENARIO = re.compile(r"^def\s+smoke_[a-z0-9_]+\s*\(", re.MULTILINE)
E2E_SCENARIO = re.compile(r"^\s*def\s+scenario_[a-z0-9_]+\s*\(", re.MULTILINE)
SUITE_TEST = re.compile(r"^\s*TEST(?:_F|_P)?\s*\(\s*([^,]+),", re.MULTILINE)
TEST_ID = re.compile(r"^\s*TEST(?:_F|_P)?\s*\(\s*([^,]+),\s*([^\)]+)\)", re.MULTILINE)


def classify_cpp(path: Path, tests: int, source_reads: int) -> str:
	"""按文件内容判定测试层级。

	仅凭文件名后缀不足以判定：`*_contract_test.cpp` 里也可能有调用生产接口的行为断言，
	而普通 `*_test.cpp` 也可能混入少量源码合同。这里按“源码读取密度”判定：

	- 显式命名为 `*_contract_test.cpp` 且确有源码读取 → 静态合同；
	- 源码读取数达到 20（高债务阈值）且占总测试数一半以上 → 静态合同；
	- 其余归入行为/单元测试。

	`structural_violations` 用于提示“行为文件里混入了源码读取”，因此这里只在
	文件主体确实是源码合同时才归为 static_contract，避免把仅含个别合同的
	行为文件整体错分，掩盖真实的混入问题。
	"""
	if source_reads == 0:
		return "unit_or_behavior"
	if path.name.endswith("_contract_test.cpp"):
		return "static_contract"
	if source_reads >= 20 and tests > 0 and source_reads * 2 >= tests:
		return "static_contract"
	return "unit_or_behavior"


# 源码合同辅助函数自身的定义站（如 test.cpp 定义 ReadTestSourceFile）不算引用。
SOURCE_DEFINITION = re.compile(
	r"^(?:std::string|const\s+std::string|static\s+std::string)\s+"
	r"(?:ReadTestSourceFile|ReadRepoFile|ReadHudNotificationTestFile|ReadTextFile|"
	r"ExtractSourceFunctionBody|ExtractSourceBlock)\s*\(",
	re.MULTILINE,
)


def count_source_reads(text: str) -> int:
	return len(SOURCE_READ.findall(text)) - len(SOURCE_DEFINITION.findall(text))


def cpp_inventory(root: Path) -> list[dict[str, object]]:
	items = []
	for path in sorted((root / "src" / "test").glob("*.cpp")):
		text = path.read_text(encoding="utf-8", errors="replace")
		suites = {}
		for match in SUITE_TEST.finditer(text):
			suites[match.group(1).strip()] = suites.get(match.group(1).strip(), 0) + 1
		tests = len(TEST_MACRO.findall(text))
		source_reads = count_source_reads(text)
		items.append(
			{
				"path": path.relative_to(root).as_posix(),
				"type": classify_cpp(path, tests, source_reads),
				"lines": len(text.splitlines()),
				"tests": tests,
				"source_contract_references": source_reads,
				"max_suite_tests": max(suites.values(), default=0),
				"suites": suites,
			}
		)
	return items


def build_inventory(root: Path) -> dict[str, object]:
	cpp = cpp_inventory(root)
	test_locations: dict[str, list[str]] = {}
	for path in sorted((root / "src" / "test").glob("*.cpp")):
		text = path.read_text(encoding="utf-8", errors="replace")
		for match in TEST_ID.finditer(text):
			test_id = f"{match.group(1).strip()}.{match.group(2).strip()}"
			test_locations.setdefault(test_id, []).append(path.relative_to(root).as_posix())
	duplicate_tests = {
		test_id: locations
		for test_id, locations in test_locations.items()
		if len(locations) > 1
	}
	python_tests = list((root / "qmclient_scripts" / "tests").glob("test*.py"))
	integration_tests = list((root / "qmclient_scripts" / "integration").glob("test*.py"))
	smoke_runner = root / "qmclient_scripts" / "integration" / "qmclient_smoke.py"
	e2e_scenarios = sum(
		len(E2E_SCENARIO.findall(path.read_text(encoding="utf-8", errors="replace")))
		for path in (root / "qmclient_scripts" / "integration").glob("e2e_*.py")
	)
	gate_tests = list((root / "qmclient_scripts" / "gate" / "tests").glob("test*.py"))
	python_unit_files = python_tests + gate_tests
	python_integration_files = integration_tests
	python_unit_cases = sum(
		len(PYTHON_CASE.findall(path.read_text(encoding="utf-8", errors="replace")))
		for path in python_unit_files
	)
	return {
		"schema": 4,
		"layers": {
			"cpp_unit_or_behavior": sum(item["tests"] for item in cpp if item["type"] == "unit_or_behavior"),
			"cpp_static_contract": sum(item["tests"] for item in cpp if item["type"] == "static_contract"),
			"python_unit_files": len(python_unit_files),
			"python_unit_cases": python_unit_cases,
			"python_integration_files": len(python_integration_files),
			"python_integration_cases": sum(
				len(PYTHON_CASE.findall(path.read_text(encoding="utf-8", errors="replace")))
				for path in python_integration_files
			),
			"process_smoke_runners": 1 if (root / "qmclient_scripts" / "integration" / "qmclient_smoke.py").is_file() else 0,
			"process_smoke_scenarios": len(SMOKE_SCENARIO.findall(smoke_runner.read_text(encoding="utf-8", errors="replace"))) if smoke_runner.is_file() else 0,
			"official_process_integration": 1 if (root / "scripts" / "integration_test.py").is_file() else 0,
			"e2e_scenarios": e2e_scenarios,
		},
		"cpp_files": cpp,
		"duplicate_tests": duplicate_tests,
		"structural_violations": [
			{
				"path": item["path"],
				"reason": "behavior file contains source-contract reads",
			}
			for item in cpp
			if item["type"] == "unit_or_behavior" and item["source_contract_references"]
		],
		"suite_violations": [
			{
				"path": item["path"],
				"suite": suite,
				"tests": count,
				"reason": "suite exceeds 50 tests; split by behavior or lifecycle",
			}
			for item in cpp
			for suite, count in item["suites"].items()
			if count > 50
		],
		"high_debt_cpp_files": [
			item
			for item in cpp
			if item["lines"] >= 1500 or item["source_contract_references"] >= 20
		],
	}


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--root", type=Path, default=Path("."))
	parser.add_argument("--json", action="store_true", help="输出 JSON")
	args = parser.parse_args()
	inventory = build_inventory(args.root.resolve())
	if args.json:
		print(json.dumps(inventory, ensure_ascii=False, indent=2))
		return 0
	print("测试层级:")
	for name, count in inventory["layers"].items():
		print(f"  {name}: {count}")
	print("高债务 C++ 测试:")
	for item in inventory["high_debt_cpp_files"]:
		print(
			f"  {item['path']}: lines={item['lines']} tests={item['tests']} "
			f"source_contract_references={item['source_contract_references']}"
		)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
