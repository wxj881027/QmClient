from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
CMAKE = shutil.which("cmake")


@unittest.skipUnless(CMAKE, "CMake is required for the test dependency contract")
class GTestDependenciesContractTest(unittest.TestCase):

	# 依赖目标只在配置时存在，直接调用生产 CMake 模块，不用源码字样推断接线。
	def configure_targets(self, targets: tuple[str, ...], found: bool = True) -> tuple[list[str], str, str]:
		tmp_root = REPO_ROOT / "tmp"
		tmp_root.mkdir(exist_ok=True)
		root = Path(tempfile.mkdtemp(prefix="gtest-dependencies-contract-", dir=tmp_root))
		lines = [
			"cmake_minimum_required(VERSION 3.16)",
			"project(GTestDependenciesContract NONE)",
			"set(SERVER ON)",
			f"set(GTEST_FOUND {'TRUE' if found else 'FALSE'})",
		]
		lines.extend(f"add_library({target} INTERFACE IMPORTED)" for target in targets)
		lines.extend([
			f'include("{(REPO_ROOT / "cmake/QmGTest.cmake").as_posix()}")',
			"qm_resolve_gtest_targets(libraries main)",
			'file(WRITE "${CMAKE_BINARY_DIR}/selection.txt" "${libraries}\\n${main}\\n")',
		])
		(root / "CMakeLists.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
		build_dir = root / "build"
		if os.name == "nt":
			command = ["cmd.exe", "/c", str(REPO_ROOT / "qmclient_scripts/cmake-windows.cmd"), "-G", "NMake Makefiles"]
		else:
			command = [CMAKE, "-G", "Unix Makefiles"]
		command.extend(["-S", str(root), "-B", str(build_dir)])
		result = subprocess.run(command, cwd=REPO_ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60, check=False)
		output = result.stdout + result.stderr
		self.assertEqual(result.returncode, 0, output)
		libraries, main = (build_dir / "selection.txt").read_text(encoding="utf-8").splitlines()
		return libraries.split(";") if libraries else [], main, " ".join(output.split())

	def test_complete_external_package_links_google_mock(self) -> None:
		libraries, main, output = self.configure_targets(("GTest::gtest", "GTest::gmock", "GTest::gtest_main"))
		self.assertEqual(libraries, ["GTest::gtest", "GTest::gmock"])
		self.assertEqual(main, "GTest::gtest_main")
		self.assertNotIn("C++ test targets are disabled", output)

	def test_gtest_only_package_reports_how_to_select_a_complete_pair(self) -> None:
		libraries, main, output = self.configure_targets(("GTest::gtest", "GTest::gtest_main"))
		self.assertEqual(libraries, [])
		self.assertEqual(main, "")
		self.assertIn("C++ test targets are disabled", output)
		self.assertIn("-DCMAKE_DISABLE_FIND_PACKAGE_GTest=ON -DDOWNLOAD_GTEST=ON", output)

	def test_downloaded_targets_remain_a_matching_pair(self) -> None:
		libraries, main, _ = self.configure_targets(("gtest", "gmock", "gtest_main"), found=False)
		self.assertEqual(libraries, ["gtest", "gmock"])
		self.assertEqual(main, "gtest_main")

	def test_missing_dependencies_do_not_publish_test_targets(self) -> None:
		libraries, main, output = self.configure_targets((), found=False)
		self.assertEqual(libraries, [])
		self.assertEqual(main, "")
		self.assertNotIn("Google Test was found", output)

	def test_custom_main_tests_remain_available_without_gtest_main(self) -> None:
		libraries, main, _ = self.configure_targets(("GTest::gtest", "GTest::gmock"))
		self.assertEqual(libraries, ["GTest::gtest", "GTest::gmock"])
		self.assertEqual(main, "")

	def test_incomplete_targets_from_different_sources_are_not_combined(self) -> None:
		libraries, main, output = self.configure_targets(("GTest::gtest", "gmock"))
		self.assertEqual(libraries, [])
		self.assertEqual(main, "")
		self.assertIn("C++ test targets are disabled", output)


if __name__ == "__main__":
	unittest.main()
