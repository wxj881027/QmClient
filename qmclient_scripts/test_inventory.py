#!/usr/bin/env python3
"""Summarize QmClient test layers and high-debt C++ test files."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re


TEST_MACRO = re.compile(r"^\s*(?:TEST(?:_F|_P)?|TYPED_TEST(?:_P)?)\s*\(", re.MULTILINE)
SOURCE_READ = re.compile(r"Read(?:TestSourceFile|RepoFile|HudNotificationTestFile|TextFile)|ExtractSource(?:FunctionBody|Block)")
PYTHON_CASE = re.compile(r"^\s*def\s+test\w*\s*\(", re.MULTILINE)
SMOKE_SCENARIO = re.compile(r"^def\s+smoke_[a-z0-9_]+\s*\(", re.MULTILINE)
E2E_SCENARIO = re.compile(r"^\s*def\s+scenario_[a-z0-9_]+\s*\(", re.MULTILINE)
SUITE_TEST = re.compile(r"^\s*(?:TEST(?:_F|_P)?|TYPED_TEST(?:_P)?)\s*\(\s*([^,]+),", re.MULTILINE)
TEST_ID = re.compile(r"^\s*(?:TEST(?:_F|_P)?|TYPED_TEST(?:_P)?)\s*\(\s*([^,]+),\s*([^\)]+)\)", re.MULTILINE)


def classify_cpp(path: Path, tests: int, source_reads: int) -> str:
	"""统计声明所属文件的层级，不把混合文件的所有声明当成静态合同。

	合同后缀是文件职责声明，不证明每个用例有效；普通文件含源码读取时
	单列 mixed，方便后续按行为拆分。无源码读取的合同后缀仍按行为统计。
	"""
	if source_reads:
		return "static_contract" if path.name.endswith("_contract_test.cpp") else "mixed"
	if "integration" in path.parts:
		return "integration"
	return "unit_or_behavior"


# 源码合同辅助函数自身的定义站（如 test.cpp 定义 ReadTestSourceFile）不算引用。
SOURCE_DEFINITION = re.compile(
	r"^(?:std::string|const\s+std::string|static\s+std::string)\s+"
	r"(?:ReadTestSourceFile|ReadRepoFile|ReadHudNotificationTestFile|ReadTextFile|"
	r"ExtractSourceFunctionBody|ExtractSourceBlock)\s*\(",
	re.MULTILINE,
)


# 直接打开仓库源码/清单同样是静态读取；参数化字体等测试输入不凭路径 helper 判为合同。
DIRECT_REPO_READ = re.compile(
	r'TestSourcePath\s*\(\s*"(?:src/|qmclient_scripts/|data/|datasrc/|other/|scripts/|CMakeLists\.txt)'
	r'|std::ifstream\s+\w+\s*\([^;\n]*DDNET_TEST_SOURCE_DIR'
)


def count_source_reads(text: str) -> int:
	return len(SOURCE_READ.findall(text)) - len(SOURCE_DEFINITION.findall(text)) + len(DIRECT_REPO_READ.findall(text))


def cpp_test_files(root: Path) -> list[Path]:
	# benchmark 的声明不属于 Google Test 库存；共享辅助 cpp 会保留为零声明文件。
	return sorted(path for path in (root / "src/test").rglob("*.cpp") if "benchmark" not in path.relative_to(root / "src/test").parts)


def summarize_cpp(items: list[dict[str, object]]) -> dict[str, object]:
	"""按文件职责汇总声明和行数；辅助编译单元单列，不伪装成测试文件。"""
	tests = [item for item in items if item["tests"] > 0]

	def totals(selected: list[dict[str, object]]) -> dict[str, int]:
		return {"files": len(selected), "tests": sum(item["tests"] for item in selected), "lines": sum(item["lines"] for item in selected)}

	summary = {layer: totals([item for item in tests if item["type"] == layer]) for layer in ("unit_or_behavior", "integration", "static_contract", "mixed")}
	summary["total"] = totals(tests)
	summary["support_files"] = len(items) - len(tests)
	count = summary["total"]["tests"]
	summary["static_contract_test_percent"] = round(summary["static_contract"]["tests"] * 100.0 / count, 2) if count else 0.0
	return summary


def cmake_registration(root: Path, cpp: list[dict[str, object]]) -> dict[str, object] | None:
	cmake = root / "CMakeLists.txt"
	if not cmake.is_file():
		return None
	# 这里只报告源码清单声明，包含条件分支和独立目标；不推断当前配置实际执行数。
	text = re.sub(r"#[^\n]*", "", cmake.read_text(encoding="utf-8-sig"))
	declared = set(re.findall(r"src/test/[A-Za-z0-9_./-]+\.cpp", text))
	for block in re.finditer(r"set_src\(\w+\s+GLOB(?:_RECURSE)?\s+src/test\s+([^)]*)\)", text):
		declared.update("src/test/" + name for name in re.findall(r"[A-Za-z0-9_./-]+\.cpp", block[1]))
	declared = {name for name in declared if "benchmark" not in Path(name).parts}
	listed = [item for item in cpp if item["path"] in declared]
	return {
		"basis": "CMake source declarations, including conditional and separate targets; not executed test instances",
		"declared_files": len(listed),
		"summary": summarize_cpp(listed),
		"declared_tests": sum(item["tests"] for item in listed),
		"declared_static_contract_tests": sum(item["tests"] for item in listed if item["type"] == "static_contract"),
		"unregistered_files": [item["path"] for item in cpp if item["tests"] and item["path"] not in declared],
		"missing_sources": sorted(name for name in declared if not (root / name).is_file()),
	}


def cpp_inventory(root: Path) -> list[dict[str, object]]:
	items = []
	for path in cpp_test_files(root):
		text = path.read_text(encoding="utf-8", errors="replace")
		suites = {}
		for match in SUITE_TEST.finditer(text):
			suites[match.group(1).strip()] = suites.get(match.group(1).strip(), 0) + 1
		tests = len(TEST_MACRO.findall(text))
		source_reads = count_source_reads(text)
		items.append({
			"path": path.relative_to(root).as_posix(),
			"type": classify_cpp(path, tests, source_reads),
			"lines": len(text.splitlines()),
			"tests": tests,
			"source_contract_references": source_reads,
			"max_suite_tests": max(suites.values(), default=0),
			"suites": suites,
		})
	return items


def build_inventory(root: Path) -> dict[str, object]:
	cpp = cpp_inventory(root)
	test_locations: dict[str, list[str]] = {}
	for path in cpp_test_files(root):
		text = path.read_text(encoding="utf-8", errors="replace")
		for match in TEST_ID.finditer(text):
			test_id = f"{match.group(1).strip()}.{match.group(2).strip()}"
			test_locations.setdefault(test_id, []).append(path.relative_to(root).as_posix())
	duplicate_tests = {test_id: locations for test_id, locations in test_locations.items() if len(locations) > 1}
	python_tests = list((root / "qmclient_scripts" / "tests").glob("test*.py"))
	integration_tests = list((root / "qmclient_scripts" / "integration").glob("test*.py"))
	smoke_runners = [path for path in (root / "qmclient_scripts" / "integration").glob("*smoke.py") if SMOKE_SCENARIO.search(path.read_text(encoding="utf-8", errors="replace"))]
	e2e_scenarios = sum(len(E2E_SCENARIO.findall(path.read_text(encoding="utf-8", errors="replace"))) for path in (root / "qmclient_scripts" / "integration").glob("e2e_*.py"))
	gate_tests = list((root / "qmclient_scripts" / "gate" / "tests").glob("test*.py"))
	python_unit_files = python_tests + gate_tests
	python_integration_files = integration_tests
	python_unit_cases = sum(len(PYTHON_CASE.findall(path.read_text(encoding="utf-8", errors="replace"))) for path in python_unit_files)
	return {
		"schema": 7,
		"count_basis": "test macro declarations; parameter and type instantiations are not expanded",
		"cmake_registration": cmake_registration(root, cpp),
		"layers": {
			"cpp_unit_or_behavior": sum(item["tests"] for item in cpp if item["type"] == "unit_or_behavior"),
			"cpp_static_contract": sum(item["tests"] for item in cpp if item["type"] == "static_contract"),
			"cpp_mixed": sum(item["tests"] for item in cpp if item["type"] == "mixed"),
			"cpp_integration": sum(item["tests"] for item in cpp if item["type"] == "integration"),
			"python_unit_files": len(python_unit_files),
			"python_unit_cases": python_unit_cases,
			"python_integration_files": len(python_integration_files),
			"python_integration_cases": sum(len(PYTHON_CASE.findall(path.read_text(encoding="utf-8", errors="replace"))) for path in python_integration_files),
			"process_smoke_runners": len(smoke_runners),
			"process_smoke_scenarios": sum(len(SMOKE_SCENARIO.findall(path.read_text(encoding="utf-8", errors="replace"))) for path in smoke_runners),
			"official_process_integration": 1 if (root / "scripts" / "integration_test.py").is_file() else 0,
			"e2e_scenarios": e2e_scenarios,
		},
		"cpp_files": cpp,
		"cpp_summary": summarize_cpp(cpp),
		"duplicate_tests": duplicate_tests,
		"structural_violations": [
			{
				"path": item["path"],
				"reason": "behavior file contains source-contract reads",
			}
			for item in cpp
			if item["type"] == "mixed"
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
		"high_debt_cpp_files": [item for item in cpp if item["lines"] >= 1500 or item["source_contract_references"] >= 20],
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
	registration = inventory["cmake_registration"]
	if registration is not None:
		print(f"CMake 声明测试（含条件分支和独立目标，非执行实例）: {registration['declared_tests']}")
		print(f"未注册测试文件: {registration['unregistered_files']}")
		print(f"注册但缺失的源码: {registration['missing_sources']}")
	print("C++ 汇总（含测试声明的文件；混合文件单列）:")
	for layer, counts in inventory["cpp_summary"].items():
		print(f"  {layer}: {counts}")
	print("高债务 C++ 测试:")
	for item in inventory["high_debt_cpp_files"]:
		print(f"  {item['path']}: lines={item['lines']} tests={item['tests']} source_contract_references={item['source_contract_references']}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
