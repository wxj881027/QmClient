from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[3]
GATE_DIR = REPO_ROOT / "qmclient_scripts" / "gate"
sys.path.insert(0, str(GATE_DIR))

from checks import clang_tidy_warn, config_vars, env, identifiers, python_tests, settings_ui  # noqa: E402
import check_gate  # noqa: E402
from lib import scope  # noqa: E402
from lib.report import ResultCollector  # noqa: E402


class GateProcessContractTest(unittest.TestCase):
	def run_gate(self, *args: str) -> subprocess.CompletedProcess[str]:
		return subprocess.run(
			[sys.executable, str(GATE_DIR / "check_gate.py"), *args],
			cwd=REPO_ROOT,
			capture_output=True,
			text=True,
			encoding="utf-8",
			errors="replace",
			check=False,
		)

	def test_report_write_failure_returns_nonzero(self):
		with TemporaryDirectory(dir=REPO_ROOT / "tmp") as temp_dir:
			result = self.run_gate(
				"--mode",
				"quick",
				"--dry-run",
				"--report-json-path",
				temp_dir,
			)
		self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)

	def test_skipped_required_test_degrades_report(self):
		with TemporaryDirectory(dir=REPO_ROOT / "tmp") as temp_dir:
			report = Path(temp_dir) / "gate.json"
			result = self.run_gate(
				"--mode",
				"default",
				"--dry-run",
				"--skip-cxx-tests",
				"--report-json-path",
				str(report),
			)
			payload = json.loads(report.read_text(encoding="utf-8"))

		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		self.assertEqual(payload["Outcome"], "DEGRADED")
		cxx_items = [item for item in payload["Items"] if item["Status"] == "SKIP" and "C++" in item["Title"]]
		self.assertEqual(len(cxx_items), 1)
		self.assertIn("--skip-cxx-tests", cxx_items[0]["Detail"])

	def test_run_all_tests_overrides_cxx_skip_without_degrading_report(self):
		with TemporaryDirectory(dir=REPO_ROOT / "tmp") as temp_dir:
			report = Path(temp_dir) / "gate.json"
			result = self.run_gate(
				"--mode",
				"default",
				"--dry-run",
				"--run-all-tests",
				"--skip-cxx-tests",
				"--report-json-path",
				str(report),
			)
			payload = json.loads(report.read_text(encoding="utf-8"))

		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		self.assertEqual(payload["Outcome"], "DRY_RUN")
		self.assertFalse(any(item["Status"] == "SKIP" and "C++" in item["Title"] for item in payload["Items"]))

	def test_irrelevant_skip_flag_does_not_degrade_report(self):
		with TemporaryDirectory(dir=REPO_ROOT / "tmp") as temp_dir:
			report = Path(temp_dir) / "gate.json"
			result = self.run_gate(
				"--mode",
				"quick",
				"--dry-run",
				"--skip-ci-build",
				"--report-json-path",
				str(report),
			)
			payload = json.loads(report.read_text(encoding="utf-8"))

		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		self.assertEqual(payload["Outcome"], "DRY_RUN")
		self.assertFalse(any(item["Status"] == "SKIP" for item in payload["Items"]))


class GateLibraryContractTest(unittest.TestCase):
	def test_python_gate_discovers_owned_test_roots(self):
		source = (GATE_DIR / "checks" / "python_tests.py").read_text(encoding="utf-8")
		for root in ("qmclient_scripts/tests", "qmclient_scripts/gate/tests", "qmclient_scripts/integration"):
			self.assertIn(root, source)

	def test_mode_contract_keeps_default_tests_and_full_superset(self):
		default = check_gate._MODE_SPECS["default"]
		default_checks = {spec.name for spec in check_gate._CHECK_SPECS if "default" in spec.default_modes}
		full_checks = {spec.name for spec in check_gate._CHECK_SPECS if "full" in spec.default_modes}

		self.assertTrue(default["tests"]["cxx"])
		self.assertTrue(default["tests"]["rust"])
		self.assertGreaterEqual(full_checks, default_checks)

	def test_check_registry_describes_special_inputs(self):
		specs = {spec.name: spec for spec in check_gate._CHECK_SPECS}

		self.assertEqual(specs["settings_ui"].scope_kind, "changed")
		self.assertTrue(specs["strict_build"].needs_base_ref)
		self.assertEqual(specs["env"].skip_attr, "skip_preflight")

	def test_config_failure_is_blocking(self):
		results = ResultCollector()
		with mock.patch.object(
			config_vars.runner,
			"run_python",
			return_value=(1, "new config violation"),
		):
			config_vars.run(results, [])

		self.assertTrue(results.has_failures())
		self.assertEqual(results.items[0].original_level, "FAIL")

	def test_scope_keeps_all_paths_and_cpp_subset(self):
		result = scope.collect_scope(
			explicit_files=[
				"AGENTS.md",
				"qmclient_scripts/gate/check_gate.py",
				"src/game/client/gameclient.cpp",
			]
		)

		self.assertEqual(
			result.changed,
			[
				"AGENTS.md",
				"qmclient_scripts/gate/check_gate.py",
				"src/game/client/gameclient.cpp",
			],
		)
		self.assertEqual(result.included, ["src/game/client/gameclient.cpp"])

	def test_settings_ui_check_is_scope_aware(self):
		self.assertTrue(settings_ui.should_run(["src/game/client/components/menus_settings.cpp"]))
		self.assertTrue(settings_ui.should_run(["src/game/client/components/menus_settings7.cpp"]))
		self.assertTrue(settings_ui.should_run(["src/game/client/QmUi/SettingsCardDeck.cpp"]))
		self.assertTrue(settings_ui.should_run(["src/game/client/QmUi/UiForms.cpp"]))
		self.assertTrue(settings_ui.should_run(["src/game/client/QmUi/QmScroll.cpp"]))
		self.assertTrue(settings_ui.should_run(["src/game/client/QmUi/SettingsPageLayout.h"]))
		self.assertTrue(settings_ui.should_run(["src/game/client/QmUi/cards/QmCardCatalogSkin.cpp"]))
		self.assertTrue(settings_ui.should_run(["src/game/client/components/menus.cpp"]))
		self.assertTrue(settings_ui.should_run(["src/game/client/components/tclient/menus_tclient.cpp"]))
		self.assertFalse(settings_ui.should_run(["src/game/client/gameclient.cpp"]))

	def test_settings_ui_irrelevant_scope_is_not_applicable(self):
		results = ResultCollector()
		settings_ui.run(results, ["src/game/client/gameclient.cpp"])

		self.assertEqual(results.items[0].status, "NOT_APPLICABLE")
		self.assertEqual(results.outcome(), "PASS")

	def test_uninitialized_submodule_parser(self):
		output = """-abc ddnet-libs\n def docs/QmClient_docs (heads/main)\n"""
		self.assertEqual(env.uninitialized_submodules(output), ["ddnet-libs"])

	def test_uninitialized_submodule_is_blocking(self):
		results = ResultCollector()
		process = subprocess.CompletedProcess(args=["git"], returncode=0, stdout="-abc ddnet-libs\n", stderr="")
		with mock.patch.object(env.subprocess, "run", return_value=process):
			env.run(results, [])

		self.assertTrue(results.has_failures())
		self.assertTrue(any(item.title == "Git 子模块前置检查" for item in results.items))

	def test_analysis_checks_ignore_deleted_translation_units(self):
		included = [
			"src/test/definitely_deleted.cpp",
			"src/test/qm_axiom_scores_test.cpp",
			"src/game/client/gameclient.cpp",
			"src/game/client/gameclient.h",
		]

		self.assertEqual(
			clang_tidy_warn._existing_source_files(included),
			["src/test/qm_axiom_scores_test.cpp", "src/game/client/gameclient.cpp"],
		)
		self.assertEqual(
			identifiers._existing_source_files(included),
			["src/game/client/gameclient.cpp"],
		)

	def test_identifier_rows_are_limited_to_changed_lines(self):
		rows = [
			{"file": r"src\game\client\gameclient.cpp", "line": "10"},
			{"file": r"src\game\client\gameclient.cpp", "line": "20"},
			{"file": r"src\game\client\gameclient.h", "line": "30"},
		]
		ranges = {
			"src/game/client/gameclient.cpp": [(8, 12)],
			"src/game/client/gameclient.h": [(30, 30)],
		}

		self.assertEqual(identifiers._filter_changed_rows(rows, ranges), [rows[0], rows[2]])


if __name__ == "__main__":
	unittest.main()
