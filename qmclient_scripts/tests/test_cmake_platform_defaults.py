#!/usr/bin/env python3
"""验证 Apple 图形构建策略及其他平台默认设置。"""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]


class CmakePlatformDefaultsTest(unittest.TestCase):
	def configure_graphics_policy(self, target_os: str, client: bool = True, headless: bool = False) -> str:
		cmake = shutil.which("cmake")
		if cmake is None:
			self.skipTest("CMake is not available")
		temp_root = REPO_ROOT / "tmp" / "tests"
		temp_root.mkdir(parents=True, exist_ok=True)
		with tempfile.TemporaryDirectory(prefix="apple-graphics-", dir=temp_root) as temp_dir:
			source_dir = Path(temp_dir)
			policy = (REPO_ROOT / "cmake/QmAppleGraphics.cmake").as_posix()
			(source_dir / "CMakeLists.txt").write_text(
				"cmake_minimum_required(VERSION 3.16)\n"
				"project(GraphicsPolicy NONE)\n"
				f'include("{policy}")\n'
				'file(WRITE "${CMAKE_BINARY_DIR}/effective.txt" "${VULKAN};${METAL};${VULKAN_SHADER_FILE_LIST}")\n',
				encoding="utf-8",
			)
			build_dir = source_dir / "build"
			arguments = [
				cmake, "-S", str(source_dir), "-B", str(build_dir),
				f"-DTARGET_OS={target_os}", f"-DCLIENT={'ON' if client else 'OFF'}",
				f"-DHEADLESS_CLIENT={'ON' if headless else 'OFF'}",
				"-DVULKAN:BOOL=ON", "-DMETAL:BOOL=OFF",
				"-DVULKAN_SHADER_FILE_LIST:STRING=old.spv",
			]
			result = subprocess.run(arguments, capture_output=True, text=True, encoding="utf-8", errors="replace")
			self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
			cache = (build_dir / "CMakeCache.txt").read_text(encoding="utf-8")
			if target_os in ("mac", "ios"):
				self.assertIn("VULKAN:BOOL=OFF", cache)
				self.assertIn("VULKAN_SHADER_FILE_LIST:STRING=\n", cache)
				if client and not headless:
					self.assertIn("METAL:BOOL=ON", cache)
			return (build_dir / "effective.txt").read_text(encoding="utf-8")

	def test_apple_overrides_old_vulkan_and_metal_cache(self) -> None:
		for target_os in ("mac", "ios"):
			with self.subTest(target_os=target_os):
				self.assertEqual(self.configure_graphics_policy(target_os), "OFF;ON;")

	def test_non_apple_graphics_options_are_preserved(self) -> None:
		for target_os in ("windows", "linux", "android"):
			with self.subTest(target_os=target_os):
				self.assertEqual(self.configure_graphics_policy(target_os), "ON;OFF;old.spv")

	def test_apple_server_and_headless_do_not_force_metal(self) -> None:
		for target_os in ("mac", "ios"):
			with self.subTest(target_os=target_os):
				self.assertEqual(self.configure_graphics_policy(target_os, client=False), "OFF;OFF;")
				self.assertEqual(self.configure_graphics_policy(target_os, headless=True), "OFF;OFF;")

	def test_backend_defaults_use_compiled_platform_configuration(self) -> None:
		compiler = shutil.which("clang") or shutil.which("clang++")
		if compiler is None:
			self.skipTest("Clang preprocessor is not available")
		source = (
			"#define MACRO_CONFIG_INT(...)\n"
			"#define MACRO_CONFIG_COL(...)\n"
			"#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Save, Desc) Name Def\n"
			'#include "engine/shared/config_variables.h"\n'
		)
		cases = [
			(["CONF_PLATFORM_MACOS", "CONF_BACKEND_METAL", "CONF_BACKEND_METAL_READY"], "Metal"),
			(["CONF_PLATFORM_IOS", "CONF_BACKEND_METAL", "CONF_BACKEND_METAL_READY"], "Metal"),
			(["CONF_PLATFORM_MACOS"], "OpenGL"),
			(["CONF_PLATFORM_ANDROID"], "GLES"),
			(["CONF_PLATFORM_WINDOWS"], "Vulkan"),
		]
		for defines, expected in cases:
			with self.subTest(defines=defines):
				result = subprocess.run(
					[compiler, "-E", "-P", "-x", "c++", "-I", str(REPO_ROOT / "src"),
					 *[f"-D{define}" for define in defines], "-"],
					input=source, capture_output=True, text=True, encoding="utf-8", errors="replace",
				)
				self.assertEqual(result.returncode, 0, result.stderr)
				match = re.search(r'\bGfxBackend\s+"([^"]+)"', result.stdout)
				self.assertIsNotNone(match)
				self.assertEqual(match.group(1), expected)

	def test_macos_deployment_target_is_overridable(self) -> None:
		cmake_source = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
		self.assertIn('set(DEFAULT_MACOS_DEPLOYMENT_TARGET "11.0")', cmake_source)
		self.assertIn('CACHE STRING "Minimum macOS deployment version")', cmake_source)
		self.assertNotIn('CACHE STRING "Minimum macOS deployment version" FORCE)', cmake_source)

	def test_windows_defines_update_target(self) -> None:
		cmake_source = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
		self.assertIn('add_executable(qm-client-updater WIN32 src/qm-update/updater_main.cpp)', cmake_source)
		self.assertIn('set_property(TARGET qm-client-updater PROPERTY OUTPUT_NAME QmClient-Updater)', cmake_source)


if __name__ == "__main__":
	unittest.main()
