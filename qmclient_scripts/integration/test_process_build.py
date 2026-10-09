"""通过缓存输入、构建命令边界和产物验证进程测试构建准备行为。"""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from qmclient_scripts.integration import process_build


class ProcessBuildTest(unittest.TestCase):
	def setUp(self):
		self.temp = tempfile.TemporaryDirectory()
		self.addCleanup(self.temp.cleanup)
		self.root = Path(self.temp.name).resolve()
		self.requested = self.root / "cmake-build-release"
		self.expected = self.root / "cmake-build-release-process-tests"
		self.calls = []
		self.failure = None
		self.missing_binary = False
		self.change_storage_mode = False
		self.write_cache(self.requested)
		patcher = mock.patch.object(process_build, "REPO_ROOT", self.root)
		patcher.start()
		self.addCleanup(patcher.stop)

	def write_cache(self, directory, **changes):
		values = {"CMAKE_HOME_DIRECTORY": str(self.root), "CMAKE_BUILD_TYPE": "Release", "CMAKE_GENERATOR": "Ninja", "DEV": "OFF", "QMCLIENT_TEST_STORAGE": "OFF", "QMCLIENT_PORTABLE": "OFF", "SERVER": "ON", "QM_MSVC_OPTIMIZE_GLOBAL_DATA": "ON"}
		values.update(changes)
		directory.mkdir(parents=True, exist_ok=True)
		(directory / "CMakeCache.txt").write_text("\n".join(f"{key}:STRING={value}" for key, value in values.items()) + "\n", encoding="utf-8")

	def fake_build(self, command, source):
		self.assertEqual(source, self.root)
		self.calls.append(list(command))
		configure = "--build" not in command
		if self.failure == ("configure" if configure else "build"):
			raise process_build.ProcessBuildError("injected build failure")
		if configure:
			directory = Path(command[command.index("-B") + 1])
			values = {argument[2:].split("=", 1)[0]: argument.split("=", 1)[1] for argument in command if argument.startswith("-D")}
			existing = process_build.read_cmake_cache(directory) if (directory / "CMakeCache.txt").is_file() else {}
			existing.update(values)
			self.write_cache(directory, **existing)
		else:
			directory = Path(command[command.index("--build") + 1])
			if self.change_storage_mode:
				self.write_cache(directory)
			if not self.missing_binary:
				for target, binary in (("game-client", "DDNet.exe"), ("game-server", "DDNet-Server.exe")):
					if target in command:
						(directory / binary).write_bytes(b"fresh-build")

	def prepare(self, **options):
		return process_build.prepare_process_build(self.requested, windows=True, run_command=self.fake_build, **options)

	def test_normal_build_creates_isolated_cache_without_changing_requested_cache(self):
		original = (self.requested / "CMakeCache.txt").read_bytes()
		self.assertEqual(self.prepare(), self.expected)
		self.assertEqual((self.requested / "CMakeCache.txt").read_bytes(), original)
		self.assertEqual((self.expected / "DDNet.exe").read_bytes(), b"fresh-build")
		cache = process_build.read_cmake_cache(self.expected)
		self.assertEqual(cache["QMCLIENT_TEST_STORAGE"], "ON")
		self.assertEqual(cache["DEV"], "ON")
		self.assertEqual(cache["QMCLIENT_PORTABLE"], "OFF")
		self.assertEqual(cache["QM_MSVC_OPTIMIZE_GLOBAL_DATA"], "ON")

	def test_existing_matching_isolated_cache_is_rebuilt_and_reused(self):
		isolated = self.root / "cmake-build-existing-test"
		self.write_cache(isolated, DEV="ON", QMCLIENT_TEST_STORAGE="ON")
		(isolated / "DDNet.exe").write_bytes(b"old-build")
		self.assertEqual(self.prepare(), isolated)
		self.assertEqual((isolated / "DDNet.exe").read_bytes(), b"fresh-build")
		self.assertFalse(self.expected.exists())

	def test_explicit_isolated_build_still_builds_existing_executable(self):
		self.write_cache(self.requested, DEV="ON", QMCLIENT_TEST_STORAGE="ON")
		(self.requested / "DDNet.exe").write_bytes(b"old-build")
		self.assertEqual(self.prepare(), self.requested)
		self.assertEqual(len(self.calls), 1)
		self.assertEqual((self.requested / "DDNet.exe").read_bytes(), b"fresh-build")

	def test_portable_build_uses_isolated_copy_instead_of_its_profile(self):
		self.write_cache(self.requested, QMCLIENT_PORTABLE="ON")
		self.assertEqual(self.prepare(), self.expected)
		self.assertEqual(process_build.read_cmake_cache(self.requested)["QMCLIENT_PORTABLE"], "ON")

	def test_isolated_cache_with_different_configuration_is_not_reused(self):
		other = self.root / "cmake-build-debug-test"
		self.write_cache(other, DEV="ON", QMCLIENT_TEST_STORAGE="ON", CMAKE_BUILD_TYPE="Debug")
		self.assertEqual(self.prepare(), self.expected)
		self.assertEqual(process_build.read_cmake_cache(other)["CMAKE_BUILD_TYPE"], "Debug")

	def test_isolated_cache_with_different_compiler_is_not_reused(self):
		self.write_cache(self.requested, CMAKE_CXX_COMPILER="toolchain-a")
		other = self.root / "cmake-build-other-compiler"
		self.write_cache(other, DEV="ON", QMCLIENT_TEST_STORAGE="ON", CMAKE_CXX_COMPILER="toolchain-b")
		self.assertEqual(self.prepare(), self.expected)
		self.assertEqual(process_build.read_cmake_cache(other)["CMAKE_CXX_COMPILER"], "toolchain-b")

	def test_foreign_requested_source_fails_before_building(self):
		self.write_cache(self.requested, CMAKE_HOME_DIRECTORY=str(self.root / "other-source"))
		with self.assertRaisesRegex(process_build.ProcessBuildError, "another workspace"):
			self.prepare()
		self.assertEqual(self.calls, [])

	def test_foreign_preferred_cache_is_not_reconfigured(self):
		self.write_cache(self.expected, CMAKE_HOME_DIRECTORY=str(self.root / "other-source"))
		original = (self.expected / "CMakeCache.txt").read_bytes()
		with self.assertRaisesRegex(process_build.ProcessBuildError, "already configured"):
			self.prepare()
		self.assertEqual(self.calls, [])
		self.assertEqual((self.expected / "CMakeCache.txt").read_bytes(), original)

	def test_preferred_production_cache_is_not_converted(self):
		self.write_cache(self.expected)
		with self.assertRaisesRegex(process_build.ProcessBuildError, "already configured"):
			self.prepare()
		self.assertEqual(self.calls, [])

	def test_configure_failure_does_not_start_build_or_return_old_binary(self):
		(self.requested / "DDNet.exe").write_bytes(b"old-build")
		self.failure = "configure"
		with self.assertRaisesRegex(process_build.ProcessBuildError, "injected"):
			self.prepare()
		self.assertEqual(len(self.calls), 1)

	def test_build_failure_is_not_hidden_by_old_executable(self):
		self.write_cache(self.requested, DEV="ON", QMCLIENT_TEST_STORAGE="ON")
		(self.requested / "DDNet.exe").write_bytes(b"old-build")
		self.failure = "build"
		with self.assertRaisesRegex(process_build.ProcessBuildError, "injected"):
			self.prepare()

	def test_missing_executable_is_rejected_after_successful_build_command(self):
		self.missing_binary = True
		with self.assertRaisesRegex(process_build.ProcessBuildError, "did not produce"):
			self.prepare()

	def test_storage_mode_change_during_build_is_rejected(self):
		self.change_storage_mode = True
		with self.assertRaisesRegex(process_build.ProcessBuildError, "changed during build"):
			self.prepare()

	def test_server_is_enabled_and_built_when_connection_scenario_needs_it(self):
		self.write_cache(self.requested, DEV="ON", QMCLIENT_TEST_STORAGE="ON", SERVER="OFF")
		self.assertEqual(self.prepare(targets=("game-client", "game-server")), self.requested)
		self.assertEqual(process_build.read_cmake_cache(self.requested)["SERVER"], "ON")
		self.assertTrue((self.requested / "DDNet-Server.exe").is_file())

	def test_profile_parallel_job_budget_is_used(self):
		profile = self.root / ".agents" / "machine.local.json"
		profile.parent.mkdir()
		profile.write_text(json.dumps({"build_jobs_per_slot_parallel": 2, "build_jobs_single": 6, "total_build_job_budget": 6}), encoding="utf-8")
		self.prepare()
		self.assertEqual(self.calls[-1][-2:], ["-j", "2"])

	def test_invalid_job_budget_fails_before_any_build_command(self):
		profile = self.root / ".agents" / "machine.local.json"
		profile.parent.mkdir()
		profile.write_text(json.dumps({"build_jobs_per_slot_parallel": True, "build_jobs_single": 6, "total_build_job_budget": 6}), encoding="utf-8")
		with self.assertRaisesRegex(process_build.ProcessBuildError, "invalid build resource"):
			self.prepare()
		self.assertEqual(self.calls, [])

	def test_non_windows_build_does_not_require_test_storage_or_cmake_cache(self):
		missing = self.root / "unconfigured"
		self.assertEqual(process_build.prepare_process_build(missing, windows=False, run_command=self.fake_build), missing)
		self.assertEqual(self.calls, [])

	def test_external_command_failure_is_reported(self):
		with mock.patch.object(process_build.subprocess, "run", side_effect=subprocess.CalledProcessError(1, ["cmake"])):
			with self.assertRaisesRegex(process_build.ProcessBuildError, "build failed"):
				process_build._run(["cmake"], self.root)


if __name__ == "__main__":
	unittest.main()
