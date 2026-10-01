from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest import mock

GATE_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(GATE_DIR))

from checks import test_structure
from lib.report import ResultCollector


class TestStructureGateTest(unittest.TestCase):
	def check_repository(self, files, cmake, expected_level, expected_detail):
		with TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "CMakeLists.txt").write_text(cmake, encoding="utf-8")
			for name, text in files.items():
				path = root / name
				path.parent.mkdir(parents=True, exist_ok=True)
				path.write_text(text, encoding="utf-8")
			results = ResultCollector()
			with mock.patch.object(test_structure, "REPO_ROOT", root):
				# 运行真实库存脚本，检查生产扫描与 gate 结果的协作。
				test_structure.run(results, [])
		self.assertEqual(len(results.items), 1)
		self.assertEqual(results.items[0].level, expected_level, results.items[0].detail)
		self.assertIn(expected_detail, results.items[0].detail)

	def test_registered_behavior_passes(self):
		self.check_repository({"src/test/unit/domain_test.cpp": "TEST(Domain, Behavior) {}"},
			"src/test/unit/domain_test.cpp", "PASS", "均为零")

	def test_mixed_source_assertion_fails_even_outside_changed_scope(self):
		self.check_repository({"src/test/unit/domain_test.cpp": 'TEST(Domain, Behavior) { ReadRepoFile("src/x.cpp"); }'},
			"src/test/unit/domain_test.cpp", "FAIL", "混合声明")

	def test_unregistered_behavior_fails(self):
		self.check_repository({"src/test/unit/domain_test.cpp": "TEST(Domain, Behavior) {}"},
			"", "FAIL", "未注册文件")

	def test_missing_registered_source_fails(self):
		self.check_repository({}, "src/test/unit/missing_test.cpp", "FAIL", "缺失源码")

	def test_duplicate_id_across_files_fails(self):
		self.check_repository({
			"src/test/unit/a_test.cpp": "TEST(Domain, Behavior) {}",
			"src/test/unit/b_test.cpp": "TEST(Domain, Behavior) {}",
		}, "src/test/unit/a_test.cpp src/test/unit/b_test.cpp", "FAIL", "重复测试 ID")

	def test_separate_contract_and_behavior_pass(self):
		self.check_repository({
			"src/test/unit/a_test.cpp": "TEST(Domain, Behavior) {}",
			"src/test/contract/a_contract_test.cpp": 'TEST(Domain, ResourceManifest) { ReadRepoFile("data/catalog.json"); }',
		}, "src/test/unit/a_test.cpp src/test/contract/a_contract_test.cpp", "PASS", "均为零")

	def test_invalid_inventory_output_fails(self):
		results = ResultCollector()
		with mock.patch.object(test_structure.runner, "run", return_value=(0, "invalid json")):
			test_structure.run(results, [])
		self.assertEqual(results.items[0].level, "FAIL")
		self.assertIn("库存读取失败", results.items[0].detail)

	def test_inventory_process_failure_fails(self):
		results = ResultCollector()
		with mock.patch.object(test_structure.runner, "run", return_value=(1, "scan failed")):
			test_structure.run(results, [])
		self.assertEqual(results.items[0].level, "FAIL")
		self.assertEqual(results.items[0].detail, "scan failed")

	def test_dry_run_does_not_scan_or_claim_pass(self):
		results = ResultCollector()
		with mock.patch.object(test_structure.runner, "run") as run:
			test_structure.run(results, [], dry_run=True)
		run.assert_not_called()
		self.assertEqual(results.items[0].level, "INFO")

	def test_gate_selects_structure_in_source_modes(self):
		import check_gate
		spec = next(item for item in check_gate._CHECK_SPECS if item.name == "test_structure")
		self.assertEqual(spec.default_modes, frozenset({"quick", "default", "full"}))


if __name__ == "__main__":
	unittest.main()
