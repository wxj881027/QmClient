import tempfile
from pathlib import Path
import unittest

from qmclient_scripts.test_inventory import build_inventory, classify_cpp


class TestInventoryTest(unittest.TestCase):
	def test_contract_suffix_is_static_contract(self):
		self.assertEqual(classify_cpp(Path("qmclient_monitoring_text_contract_test.cpp"), 1, 1), "static_contract")
		self.assertEqual(classify_cpp(Path("qm_chat_interactions_test.cpp"), 1, 0), "unit_or_behavior")

	def test_inventory_reports_behavior_contract_mixing(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "src/test").mkdir(parents=True)
			(root / "qmclient_scripts/tests").mkdir(parents=True)
			(root / "qmclient_scripts/gate/tests").mkdir(parents=True)
			(root / "qmclient_scripts/integration").mkdir(parents=True)
			(root / "src/test/mixed_test.cpp").write_text(
				'TEST(Mixed, One) {}\nReadRepoFile("x");\n', encoding="utf-8"
			)
			inventory = build_inventory(root)
		self.assertEqual(inventory["structural_violations"][0]["path"], "src/test/mixed_test.cpp")

	def test_inventory_counts_test_macros_and_source_contracts(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "src/test").mkdir(parents=True)
			(root / "qmclient_scripts/tests").mkdir(parents=True)
			(root / "qmclient_scripts/gate/tests").mkdir(parents=True)
			(root / "qmclient_scripts/integration").mkdir(parents=True)
			(root / "qmclient_scripts/integration/test_smoke.py").write_text(
				"def test_smoke_registration():\n    pass\n", encoding="utf-8"
			)
			(root / "src/test/example_contract_test.cpp").write_text(
				'TEST(Example, One) {}\nReadTestSourceFile("x");\n', encoding="utf-8"
			)
			inventory = build_inventory(root)

		self.assertEqual(inventory["cpp_files"][0]["tests"], 1)
		self.assertEqual(inventory["cpp_files"][0]["source_contract_references"], 1)
		self.assertEqual(inventory["layers"]["cpp_static_contract"], 1)
		self.assertEqual(inventory["layers"]["python_unit_files"], 0)
		self.assertEqual(inventory["layers"]["python_unit_cases"], 0)
		self.assertEqual(inventory["layers"]["python_integration_files"], 1)
		self.assertEqual(inventory["layers"]["python_integration_cases"], 1)
		self.assertEqual(inventory["schema"], 4)

	def test_inventory_counts_custom_source_reader(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "src/test").mkdir(parents=True)
			(root / "qmclient_scripts/tests").mkdir(parents=True)
			(root / "qmclient_scripts/gate/tests").mkdir(parents=True)
			(root / "qmclient_scripts/integration").mkdir(parents=True)
			(root / "src/test/mixed_test.cpp").write_text(
				'TEST(Mixed, One) {}\nReadHudNotificationTestFile("x");\n', encoding="utf-8"
			)
			inventory = build_inventory(root)
		self.assertEqual(inventory["cpp_files"][0]["source_contract_references"], 1)

	def test_inventory_counts_local_text_reader(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "src/test").mkdir(parents=True)
			(root / "qmclient_scripts/tests").mkdir(parents=True)
			(root / "qmclient_scripts/gate/tests").mkdir(parents=True)
			(root / "qmclient_scripts/integration").mkdir(parents=True)
			(root / "src/test/mixed_test.cpp").write_text(
				'TEST(Mixed, One) {}\nReadTextFile("x");\n', encoding="utf-8"
			)
			inventory = build_inventory(root)
		self.assertEqual(inventory["cpp_files"][0]["source_contract_references"], 1)

	def test_inventory_reports_duplicate_test_ids_across_files(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "src/test").mkdir(parents=True)
			for name in ("one_test.cpp", "two_test.cpp"):
				(root / "src/test" / name).write_text("TEST(Duplicate, Same) {}\n", encoding="utf-8")
			(root / "qmclient_scripts/tests").mkdir(parents=True)
			(root / "qmclient_scripts/gate/tests").mkdir(parents=True)
			(root / "qmclient_scripts/integration").mkdir(parents=True)
			inventory = build_inventory(root)
		self.assertEqual(inventory["duplicate_tests"]["Duplicate.Same"], [
			"src/test/one_test.cpp",
			"src/test/two_test.cpp",
		])

	def test_inventory_reports_oversized_suite(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "src/test").mkdir(parents=True)
			body = "".join(f"TEST(LargeSuite, Case{i}) {{}}\n" for i in range(51))
			(root / "src/test/large_test.cpp").write_text(body, encoding="utf-8")
			(root / "qmclient_scripts/tests").mkdir(parents=True)
			(root / "qmclient_scripts/gate/tests").mkdir(parents=True)
			(root / "qmclient_scripts/integration").mkdir(parents=True)
			inventory = build_inventory(root)
		self.assertEqual(inventory["suite_violations"][0]["suite"], "LargeSuite")

	def test_inventory_reports_files_at_documented_split_threshold(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "src/test").mkdir(parents=True)
			body = "TEST(LargeFile, Case) {}\n" + "// padding\n" * 1499
			(root / "src/test/large_file.cpp").write_text(body, encoding="utf-8")
			for path in (root / "qmclient_scripts/tests", root / "qmclient_scripts/gate/tests", root / "qmclient_scripts/integration"):
				path.mkdir(parents=True)
			inventory = build_inventory(root)
		self.assertEqual(inventory["high_debt_cpp_files"][0]["path"], "src/test/large_file.cpp")

	def test_inventory_counts_independent_e2e_scenarios(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			for path in (root / "src/test", root / "qmclient_scripts/tests", root / "qmclient_scripts/gate/tests", root / "qmclient_scripts/integration"):
				path.mkdir(parents=True)
			(root / "qmclient_scripts/integration/e2e_client.py").write_text(
				"def scenario_connect():\n    pass\n\ndef scenario_shutdown():\n    pass\n", encoding="utf-8"
			)
			inventory = build_inventory(root)
		self.assertEqual(inventory["layers"]["e2e_scenarios"], 2)


if __name__ == "__main__":
	unittest.main()
