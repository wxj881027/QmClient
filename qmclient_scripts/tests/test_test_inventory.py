import tempfile
from pathlib import Path
import unittest

from qmclient_scripts.test_inventory import build_inventory, classify_cpp, count_source_reads


class TestInventoryTest(unittest.TestCase):
	def test_contract_suffix_is_static_contract(self):
		self.assertEqual(classify_cpp(Path("qmclient_monitoring_text_contract_test.cpp"), 1, 1), "static_contract")
		self.assertEqual(classify_cpp(Path("qm_chat_interactions_test.cpp"), 1, 0), "unit_or_behavior")

	def test_direct_source_path_reader_is_counted(self):
		text = 'TEST(Source, One) { std::ifstream File(TestSourcePath("src/game/client/ui.cpp")); }'
		self.assertEqual(count_source_reads(text), 1)

	def test_repository_root_resource_reader_is_counted(self):
		text = 'std::ifstream File(std::string(DDNET_TEST_SOURCE_DIR) + "/qmclient_scripts/catalog.toml");'
		self.assertEqual(count_source_reads(text), 1)

	def test_parameterized_fixture_path_is_not_a_source_contract(self):
		text = 'std::ifstream File(TestSourcePath(pRelativePath));'
		self.assertEqual(count_source_reads(text), 0)

	def test_temp_file_reader_is_not_a_source_contract(self):
		text = 'std::ifstream File(Info.Filename());'
		self.assertEqual(count_source_reads(text), 0)

	def test_inventory_reports_behavior_contract_mixing(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "src/test").mkdir(parents=True)
			(root / "qmclient_scripts/tests").mkdir(parents=True)
			(root / "qmclient_scripts/gate/tests").mkdir(parents=True)
			(root / "qmclient_scripts/integration").mkdir(parents=True)
			(root / "src/test/mixed_test.cpp").write_text('TEST(Mixed, One) {}\nReadRepoFile("x");\n', encoding="utf-8")
			inventory = build_inventory(root)
		self.assertEqual(inventory["structural_violations"][0]["path"], "src/test/mixed_test.cpp")

	def test_inventory_counts_test_macros_and_source_contracts(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "src/test").mkdir(parents=True)
			(root / "qmclient_scripts/tests").mkdir(parents=True)
			(root / "qmclient_scripts/gate/tests").mkdir(parents=True)
			(root / "qmclient_scripts/integration").mkdir(parents=True)
			(root / "qmclient_scripts/integration/test_smoke.py").write_text("def test_smoke_registration():\n    pass\n", encoding="utf-8")
			(root / "src/test/example_contract_test.cpp").write_text('TEST(Example, One) {}\nReadTestSourceFile("x");\n', encoding="utf-8")
			inventory = build_inventory(root)

		self.assertEqual(inventory["cpp_files"][0]["tests"], 1)
		self.assertEqual(inventory["cpp_files"][0]["source_contract_references"], 1)
		self.assertEqual(inventory["layers"]["cpp_static_contract"], 1)
		self.assertEqual(inventory["layers"]["python_unit_files"], 0)
		self.assertEqual(inventory["layers"]["python_unit_cases"], 0)
		self.assertEqual(inventory["layers"]["python_integration_files"], 1)
		self.assertEqual(inventory["layers"]["python_integration_cases"], 1)
		self.assertEqual(inventory["schema"], 7)

	def test_inventory_counts_custom_source_reader(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "src/test").mkdir(parents=True)
			(root / "qmclient_scripts/tests").mkdir(parents=True)
			(root / "qmclient_scripts/gate/tests").mkdir(parents=True)
			(root / "qmclient_scripts/integration").mkdir(parents=True)
			(root / "src/test/mixed_test.cpp").write_text('TEST(Mixed, One) {}\nReadHudNotificationTestFile("x");\n', encoding="utf-8")
			inventory = build_inventory(root)
		self.assertEqual(inventory["cpp_files"][0]["source_contract_references"], 1)

	def test_inventory_counts_local_text_reader(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "src/test").mkdir(parents=True)
			(root / "qmclient_scripts/tests").mkdir(parents=True)
			(root / "qmclient_scripts/gate/tests").mkdir(parents=True)
			(root / "qmclient_scripts/integration").mkdir(parents=True)
			(root / "src/test/mixed_test.cpp").write_text('TEST(Mixed, One) {}\nReadTextFile("x");\n', encoding="utf-8")
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
		self.assertEqual(
			inventory["duplicate_tests"]["Duplicate.Same"],
			[
				"src/test/one_test.cpp",
				"src/test/two_test.cpp",
			],
		)

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
			(root / "qmclient_scripts/integration/e2e_client.py").write_text("def scenario_connect():\n    pass\n\ndef scenario_shutdown():\n    pass\n", encoding="utf-8")
			inventory = build_inventory(root)
		self.assertEqual(inventory["layers"]["e2e_scenarios"], 2)

	def test_inventory_finds_nested_tests_and_duplicates_but_skips_benchmarks(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			for folder in ("unit/qmclient/ui", "contract/qmclient/ui", "benchmark"):
				(root / "src/test" / folder).mkdir(parents=True)
			for name in ("unit/qmclient/ui/one_test.cpp", "contract/qmclient/ui/two_test.cpp", "benchmark/ignored.cpp"):
				(root / "src/test" / name).write_text("TEST(Duplicate, Same) {}\n", encoding="utf-8")
			inventory = build_inventory(root)
		self.assertEqual(len(inventory["cpp_files"]), 2)
		self.assertEqual(len(inventory["duplicate_tests"]["Duplicate.Same"]), 2)

	def test_inventory_counts_typed_declarations_without_counting_instantiations(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "src/test").mkdir(parents=True)
			(root / "src/test/typed_test.cpp").write_text("TYPED_TEST(Writer, Empty) {}\nTYPED_TEST_P(WriterPattern, Value) {}\nTYPED_TEST_SUITE(Writer, Types);\nTEST_P(Parameterized, Value) {}\n", encoding="utf-8")
			inventory = build_inventory(root)
		self.assertEqual(inventory["layers"]["cpp_unit_or_behavior"], 3)
		self.assertEqual(inventory["cpp_files"][0]["suites"], {"Writer": 1, "WriterPattern": 1, "Parameterized": 1})

	def test_inventory_separates_cmake_declarations_from_unregistered_tests(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "src/test/unit").mkdir(parents=True)
			for name in ("listed", "conditional", "dead"):
				(root / f"src/test/unit/{name}_test.cpp").write_text(f"TEST({name}, Works) {{}}\n", encoding="utf-8")
			(root / "CMakeLists.txt").write_text("set_src(TESTS GLOB_RECURSE src/test\n unit/listed_test.cpp\n)\nif(WIN32)\n list(APPEND TESTS src/test/unit/conditional_test.cpp)\nendif()\n# src/test/unit/dead_test.cpp\nadd_executable(extra src/test/unit/missing_test.cpp)\n", encoding="utf-8")
			inventory = build_inventory(root)
		self.assertEqual(inventory["cmake_registration"]["declared_tests"], 2)
		self.assertEqual(inventory["cmake_registration"]["unregistered_files"], ["src/test/unit/dead_test.cpp"])
		self.assertEqual(inventory["cmake_registration"]["missing_sources"], ["src/test/unit/missing_test.cpp"])

	def test_inventory_without_cmake_reports_registration_as_unknown(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "src/test").mkdir(parents=True)
			(root / "src/test/one_test.cpp").write_text("TEST(One, Works) {}\n", encoding="utf-8")
			inventory = build_inventory(root)
		self.assertIsNone(inventory["cmake_registration"])

	def test_summary_keeps_mixed_declarations_separate_from_contracts(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			files = {
				"unit/example_test.cpp": "TEST(Unit, One) {}\nTEST(Unit, Two) {}\n",
				"contract/example_contract_test.cpp": 'TEST(Contract, One) {}\nReadRepoFile("x");\n',
				"mixed/example_test.cpp": 'TEST(Mixed, One) {}\nReadRepoFile("x");\n',
				"integration/example_test.cpp": "TEST(Integration, One) {}\n",
				"support/helpers.cpp": "void Helper() {}\n",
			}
			for name, text in files.items():
				path = root / "src/test" / name
				path.parent.mkdir(parents=True, exist_ok=True)
				path.write_text(text, encoding="utf-8")
			summary = build_inventory(root)["cpp_summary"]
		self.assertEqual(summary["total"], {"files": 4, "tests": 5, "lines": 7})
		self.assertEqual(summary["static_contract"], {"files": 1, "tests": 1, "lines": 2})
		self.assertEqual(summary["mixed"], {"files": 1, "tests": 1, "lines": 2})
		self.assertEqual(summary["support_files"], 1)
		self.assertEqual(summary["static_contract_test_percent"], 20.0)

	def test_summary_empty_inventory_has_zero_ratio(self):
		with tempfile.TemporaryDirectory() as directory:
			summary = build_inventory(Path(directory))["cpp_summary"]
		self.assertEqual(summary["total"], {"files": 0, "tests": 0, "lines": 0})
		self.assertEqual(summary["static_contract_test_percent"], 0.0)


if __name__ == "__main__":
	unittest.main()
