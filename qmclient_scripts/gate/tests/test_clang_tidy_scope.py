"""clang-tidy 增量扫描范围选择的回归测试。"""

from __future__ import annotations

import importlib.util
import json
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[3]
TOOL = REPO_ROOT / "qmclient_scripts" / "gate" / "tools" / "clang_tidy_build.py"

spec = importlib.util.spec_from_file_location("clang_tidy_build", TOOL)
clang_tidy_build = importlib.util.module_from_spec(spec)
sys.modules["clang_tidy_build"] = clang_tidy_build
spec.loader.exec_module(clang_tidy_build)


class ClassifyChangedPathsTest(unittest.TestCase):
	def test_source_only_changes_stay_incremental(self):
		sources, needs_full, reason = clang_tidy_build.classify_changed_paths(
			["src/game/client/gameclient.cpp", "docs/readme.md"]
		)
		self.assertEqual(["src/game/client/gameclient.cpp"], sources)
		self.assertFalse(needs_full)
		self.assertEqual("", reason)

	def test_header_change_forces_full_scan(self):
		for header in ("src/game/client/ui.h", "src/engine/shared/config_variables.h.inl"):
			with self.subTest(header=header):
				sources, needs_full, reason = clang_tidy_build.classify_changed_paths(
					["src/game/client/gameclient.cpp", header]
				)
				self.assertEqual([], sources)
				self.assertTrue(needs_full)
				self.assertIn(header, reason)

	def test_build_configuration_forces_full_scan(self):
		for path in ("CMakeLists.txt", ".clang-tidy", "cmake/BuildFeatures.cmake"):
			with self.subTest(path=path):
				_, needs_full, reason = clang_tidy_build.classify_changed_paths([path])
				self.assertTrue(needs_full)
				self.assertIn(path, reason)

	def test_editing_this_tool_forces_full_scan(self):
		"""改了扫描器本身，增量结论就不可信了。"""
		_, needs_full, reason = clang_tidy_build.classify_changed_paths([clang_tidy_build.THIS_TOOL])
		self.assertTrue(needs_full)
		self.assertIn("clang_tidy_build.py", reason)

	def test_non_code_changes_need_no_scan(self):
		sources, needs_full, reason = clang_tidy_build.classify_changed_paths(
			["docs/wiki/deck/x.md", ".github/workflows/build.yml"]
		)
		self.assertEqual([], sources)
		self.assertFalse(needs_full)
		self.assertEqual("", reason)


class ObjectTargetsTest(unittest.TestCase):
	def write_database(self, root: Path, entries: list[tuple[str, str]]) -> None:
		payload = [
			{"file": str(REPO_ROOT / source), "output": output} for source, output in entries
		]
		(root / "compile_commands.json").write_text(json.dumps(payload), encoding="utf-8")

	def test_maps_sources_to_object_paths(self):
		with TemporaryDirectory() as temporary:
			root = Path(temporary)
			self.write_database(
				root,
				[
					("src/game/client/b.cpp", "CMakeFiles/game-client.dir/src/game/client/b.cpp.o"),
					("src/game/client/a.cpp", "CMakeFiles/game-client.dir/src/game/client/a.cpp.o"),
				],
			)
			original = clang_tidy_build.BUILD_DIR
			clang_tidy_build.BUILD_DIR = root
			try:
				targets = clang_tidy_build.object_targets_for(
					["src/game/client/a.cpp", "src/game/client/b.cpp"]
				)
			finally:
				clang_tidy_build.BUILD_DIR = original
			self.assertEqual(
				[
					"CMakeFiles/game-client.dir/src/game/client/a.cpp.o",
					"CMakeFiles/game-client.dir/src/game/client/b.cpp.o",
				],
				targets,
			)

	def test_sources_outside_the_scanned_target_are_dropped(self):
		with TemporaryDirectory() as temporary:
			root = Path(temporary)
			self.write_database(
				root, [("src/game/client/a.cpp", "CMakeFiles/game-client.dir/src/game/client/a.cpp.o")]
			)
			original = clang_tidy_build.BUILD_DIR
			clang_tidy_build.BUILD_DIR = root
			try:
				targets = clang_tidy_build.object_targets_for(["src/test/only_test.cpp"])
			finally:
				clang_tidy_build.BUILD_DIR = original
			self.assertEqual([], targets)

	def test_missing_database_falls_back_to_full_scan(self):
		with TemporaryDirectory() as temporary:
			original = clang_tidy_build.BUILD_DIR
			clang_tidy_build.BUILD_DIR = Path(temporary)
			try:
				targets = clang_tidy_build.object_targets_for(["src/game/client/a.cpp"])
			finally:
				clang_tidy_build.BUILD_DIR = original
			self.assertIsNone(targets)

	def test_database_without_output_field_falls_back_to_full_scan(self):
		"""老版本 CMake 不写 output 字段：不能把「没映射到」当成「没有 TU」。"""
		with TemporaryDirectory() as temporary:
			root = Path(temporary)
			(root / "compile_commands.json").write_text(
				json.dumps([{"file": str(REPO_ROOT / "src/game/client/a.cpp")}]), encoding="utf-8"
			)
			original = clang_tidy_build.BUILD_DIR
			clang_tidy_build.BUILD_DIR = root
			try:
				targets = clang_tidy_build.object_targets_for(["src/game/client/a.cpp"])
			finally:
				clang_tidy_build.BUILD_DIR = original
			self.assertIsNone(targets)


class MainCommandTest(unittest.TestCase):
	"""确认 main() 把增量结论正确翻译成 ninja 命令行。"""

	def run_main(self, root: Path, changed: list[str]):
		calls = []

		def fake_run(command, cwd=None, log_path=None, log_header=None):
			calls.append(list(command))
			return 0

		with mock.patch.object(clang_tidy_build, "resolve_tool", return_value="tool"), \
			mock.patch.object(clang_tidy_build, "require_llvm_major", return_value=None), \
			mock.patch.object(clang_tidy_build, "run_command", side_effect=fake_run), \
			mock.patch.object(clang_tidy_build, "git_changed_paths", return_value=changed), \
			mock.patch.object(clang_tidy_build, "object_targets_for", side_effect=lambda sources: [
				f"CMakeFiles/game-client.dir/{source}.o" for source in sources
			]), \
			mock.patch.object(clang_tidy_build, "BUILD_DIR", root), \
			mock.patch.object(clang_tidy_build, "CHANGED_ONLY", True):
			code = clang_tidy_build.main()
		self.assertEqual(0, code)
		return calls[-1]

	def test_incremental_build_passes_object_paths(self):
		with TemporaryDirectory() as temporary:
			command = self.run_main(Path(temporary), ["src/game/client/a.cpp"])
		self.assertIn("--", command)
		self.assertNotIn("--target", command)
		self.assertEqual("-k", command[-2])
		self.assertIn("CMakeFiles/game-client.dir/src/game/client/a.cpp.o", command)

	def test_header_change_runs_the_full_target(self):
		with TemporaryDirectory() as temporary:
			command = self.run_main(Path(temporary), ["src/game/client/a.h"])
		self.assertIn("--target", command)
		self.assertIn("game-client", command)


if __name__ == "__main__":
	unittest.main()
