from __future__ import annotations

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from qmclient_scripts import repair_ninja_msvc_prefix
from qmclient_scripts.cmake_windows import build_directory_lock
from qmclient_scripts.integration.process_harness import Process


@unittest.skipUnless(shutil.which("ninja"), "Ninja is required")
class NinjaMsvcPrefixIntegrationTest(unittest.TestCase):
	def test_repaired_prefix_tracks_header_and_rebuilds_after_header_change(self):
		for prefix in (b"Note: including file: ", "注意: 包含文件:  ".encode("utf-8"), "注意: 包含文件:  ".encode("gbk")):
			for existing in (True, False):
				with self.subTest(prefix=prefix, existing=existing), tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2] / "tmp") as tmp:
					directory = Path(tmp)
					rules = self.create_build(directory, prefix, existing)
					self.assertTrue(repair_ninja_msvc_prefix._repair_rules_file(rules, prefix))
					self.run_ninja(directory)
					deps = self.run_ninja(directory, "-t", "deps")
					self.assertIn(b"#deps 1", deps)
					self.assertIn(b"header.h", deps)
					self.assertFalse(repair_ninja_msvc_prefix._repair_rules_file(rules, prefix))
					self.run_ninja(directory)
					self.assertEqual((directory / "count").read_bytes(), b"1")
					header = directory / "header.h"
					header.write_text("second", encoding="utf-8")
					newer = (directory / "main.obj").stat().st_mtime_ns + 2_000_000_000
					os.utime(header, ns=(newer, newer))
					self.run_ninja(directory)
					self.assertEqual((directory / "count").read_bytes(), b"11")

	def test_prefix_repair_recovers_existing_zero_dependency_object(self):
		prefix = "注意: 包含文件:  ".encode("gbk")
		with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2] / "tmp") as tmp:
			directory = Path(tmp)
			rules = self.create_build(directory, prefix, True)
			self.run_ninja(directory)
			self.assertIn(b"#deps 0", self.run_ninja(directory, "-t", "deps"))
			self.assertTrue(repair_ninja_msvc_prefix._repair_rules_file(rules, prefix))
			self.assertEqual(repair_ninja_msvc_prefix._invalidate_zero_dependency_objects(directory), 1)
			self.run_ninja(directory)
			self.assertIn(b"#deps 1", self.run_ninja(directory, "-t", "deps"))
			self.assertEqual((directory / "count").read_bytes(), b"11")
			self.assertEqual(repair_ninja_msvc_prefix._invalidate_zero_dependency_objects(directory), 0)

	def create_build(self, directory: Path, prefix: bytes, existing: bool) -> Path:
		compiler = directory / "compiler.py"
		compiler.write_text(
			"import pathlib, sys\nheader = pathlib.Path('header.h').resolve()\nsys.stdout.buffer.write(bytes.fromhex(sys.argv[1]) + str(header).encode('utf-8') + b'\\n')\npathlib.Path('main.obj').write_bytes(b'object')\nwith pathlib.Path('count').open('ab') as count: count.write(b'1')\n",
			encoding="utf-8",
		)
		header = directory / "header.h"
		header.write_text("first", encoding="utf-8")
		args = [sys.executable, str(compiler), prefix.hex()]
		command = subprocess.list2cmdline(args) if os.name == "nt" else shlex.join(args)
		rules = directory / "rules.ninja"
		rules.write_bytes((b"msvc_deps_prefix = wrong prefix\n" if existing else b"") + ("rule compile\n  command = " + command.replace("$", "$$") + "\n  deps = msvc\n").encode("utf-8"))
		(directory / "build.ninja").write_text("include rules.ninja\nbuild main.obj: compile main.c\n", encoding="utf-8")
		(directory / "main.c").write_text("int main(void) { return 0; }\n", encoding="utf-8")
		(directory / "CMakeCache.txt").write_text(
			"CMAKE_MAKE_PROGRAM:FILEPATH=" + str(Path(shutil.which("ninja")).resolve()) + "\n",
			encoding="utf-8",
		)
		return rules

	def run_ninja(self, directory: Path, *args: str) -> bytes:
		result = subprocess.run(
			[shutil.which("ninja"), "-C", str(directory), *args],
			capture_output=True,
			timeout=20,
			check=False,
		)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		return result.stdout


@unittest.skipUnless(os.name == "nt" and shutil.which("cmake") and (Path(os.environ.get("ProgramFiles(x86)", "")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe").is_file(), "Windows MSVC tools are required")
class WindowsMsvcPrefixBuildIntegrationTest(unittest.TestCase):
	def test_wrapper_recovers_cmake_regeneration_and_header_change(self):
		root = Path(__file__).resolve().parents[2]
		with tempfile.TemporaryDirectory(dir=root / "tmp") as tmp:
			directory = Path(tmp)
			source = directory / "source"
			source.mkdir()
			build = directory / "build space"
			cmakelists = source / "CMakeLists.txt"
			project = "cmake_minimum_required(VERSION 3.24)\nproject(PrefixProbe C)\nadd_executable(probe main.c)\n"
			cmakelists.write_text(project, encoding="utf-8")
			(source / "main.c").write_text('#include "header.h"\nint main(void) { return HEADER_VALUE; }\n', encoding="utf-8")
			header = source / "header.h"
			header.write_text("#define HEADER_VALUE 0\n", encoding="utf-8")
			self.run_wrapper(root, "-G", "Ninja", "-S", str(source), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release")
			self.run_wrapper(root, "--build", str(build))
			self.assertEqual(subprocess.run([str(build / "probe.exe")], timeout=10).returncode, 0)
			cmakelists.write_text(project + "add_compile_definitions(QM_PREFIX_REGENERATED=1)\n", encoding="utf-8")
			self.run_wrapper(root, "--build", str(build))
			deps = subprocess.run([shutil.which("ninja"), "-C", str(build), "-t", "deps"], capture_output=True, timeout=20, check=True).stdout
			self.assertIn(b"header.h", deps)
			self.assertNotIn(b"#deps 0", deps)
			header.write_text("#define HEADER_VALUE 5\n", encoding="utf-8")
			self.run_wrapper(root, "--build", str(build))
			self.assertEqual(subprocess.run([str(build / "probe.exe")], timeout=10).returncode, 5)

	def test_wrapper_waits_before_configuring_an_occupied_directory(self):
		root = Path(__file__).resolve().parents[2]
		with tempfile.TemporaryDirectory(dir=root / "tmp") as tmp:
			directory = Path(tmp)
			source = directory / "source"
			source.mkdir()
			(source / "CMakeLists.txt").write_text("cmake_minimum_required(VERSION 3.24)\nproject(DirectoryLock NONE)\n", encoding="utf-8")
			build = directory / "build space"
			process = None
			try:
				with build_directory_lock(build):
					process = Process("configure", ["cmd.exe", "/c", str(root / "qmclient_scripts" / "cmake-windows.cmd"), "-G", "Ninja", "-S", str(source), "-B", str(build)], root)
					process.wait_for(lambda line: "Waiting for build directory:" in line, "directory lock wait", 45)
					self.assertFalse((build / "CMakeCache.txt").exists())
				self.assertEqual(process.wait_for_exit(90), 0)
				self.assertTrue((build / "CMakeCache.txt").is_file())
			finally:
				if process is not None:
					process.stop()
					process._process.stdout.close()

	def test_failed_compile_releases_lock_and_next_build_succeeds(self):
		root = Path(__file__).resolve().parents[2]
		with tempfile.TemporaryDirectory(dir=root / "tmp") as tmp:
			directory = Path(tmp)
			source = directory / "source"
			source.mkdir()
			(source / "CMakeLists.txt").write_text("cmake_minimum_required(VERSION 3.24)\nproject(BuildRecovery C)\nadd_executable(probe main.c)\n", encoding="utf-8")
			main = source / "main.c"
			main.write_text("#error expected_compile_failure\n", encoding="utf-8")
			build = directory / "build space"
			self.run_wrapper(root, "-G", "Ninja", "-S", str(source), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release")
			result = subprocess.run(["cmd.exe", "/c", str(root / "qmclient_scripts" / "cmake-windows.cmd"), "--build", str(build)], cwd=root, capture_output=True, timeout=60, check=False)
			self.assertNotEqual(result.returncode, 0)
			self.assertIn(b"expected_compile_failure", result.stdout)
			main.write_text("int main(void) { return 0; }\n", encoding="utf-8")
			self.run_wrapper(root, "--build", str(build))
			self.assertEqual(subprocess.run([str(build / "probe.exe")], timeout=10).returncode, 0)

	def run_wrapper(self, root: Path, *args: str) -> None:
		result = subprocess.run(["cmd.exe", "/c", str(root / "qmclient_scripts" / "cmake-windows.cmd"), *args], cwd=root, capture_output=True, timeout=90, check=False)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
	unittest.main()
