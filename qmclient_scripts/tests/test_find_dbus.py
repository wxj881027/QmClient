from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO_ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which("pkg-config"), "pkg-config is not installed")
class FindDBusIntegrationTest(unittest.TestCase):
	def test_nonstandard_prefix_preserves_imported_library_options_and_missing_dependency_is_optional(self) -> None:
		# 运行实际 FindDBus 模块与 pkg-config；目标属性是构建可观察结果，不读生产源码断言。
		with tempfile.TemporaryDirectory(dir=REPO_ROOT / "tmp", prefix="dbus-configure-") as temporary:
			workspace = Path(temporary)
			project = workspace / "project"
			project.mkdir()
			prefix = workspace / "external-prefix"
			libraries = prefix / "lib"
			includes = prefix / "include"
			metadata = prefix / "pkgconfig"
			for directory in (libraries, includes, metadata):
				directory.mkdir(parents=True)
			library = libraries / "libdbus-1.a"
			library.write_bytes(b"configure-only library fixture")
			(metadata / "dbus-1.pc").write_text(
				f"prefix={prefix.as_posix()}\nlibdir=${{prefix}}/lib\nincludedir=${{prefix}}/include\nName: isolated dbus\nDescription: actual pkg-config imported-target fixture\nVersion: 1.0\nLibs: -L${{libdir}} -ldbus-1 -Wl,--as-needed\nCflags: -I${{includedir}} -DQM_DBUS_FIXTURE=1\n",
				encoding="utf-8",
			)
			(project / "CMakeLists.txt").write_text(
				"cmake_minimum_required(VERSION 3.16)\nproject(DBusConfigure NONE)\n"
				f'list(PREPEND CMAKE_MODULE_PATH "{(REPO_ROOT / "cmake").as_posix()}")\n'
				'set(CMAKE_FIND_LIBRARY_PREFIXES "lib")\nset(CMAKE_FIND_LIBRARY_SUFFIXES ".a")\n'
				"find_package(DBus QUIET)\n"
				"if(DBus_FOUND)\n"
				"  foreach(property INTERFACE_LINK_LIBRARIES INTERFACE_INCLUDE_DIRECTORIES INTERFACE_COMPILE_OPTIONS INTERFACE_LINK_OPTIONS)\n"
				"    get_target_property(value PkgConfig::DBUS ${property})\n"
				'    file(APPEND "${CMAKE_BINARY_DIR}/result.txt" "${property}=${value}\\n")\n'
				"  endforeach()\n"
				'  file(APPEND "${CMAKE_BINARY_DIR}/result.txt" "found=1\\n")\n'
				"else()\n"
				'  file(WRITE "${CMAKE_BINARY_DIR}/result.txt" "found=0\\n")\n'
				"endif()\n",
				encoding="utf-8",
			)
			environment = {**os.environ, "PKG_CONFIG_PATH": str(metadata), "PKG_CONFIG_LIBDIR": str(metadata), "PKG_CONFIG_SYSROOT_DIR": ""}
			for mode in ("available", "missing", "missing-pkg-config"):
				with self.subTest(mode=mode):
					available = mode == "available"
					if mode == "missing":
						(metadata / "dbus-1.pc").unlink()
					build = workspace / ("available" if mode == "missing-pkg-config" else mode)
					if os.name == "nt":
						command = ["cmd", "/c", str(REPO_ROOT / "qmclient_scripts/cmake-windows.cmd")]
					else:
						command = ["cmake"]
					arguments = [*command, "-S", str(project), "-B", str(build)]
					if (build / "result.txt").exists():
						(build / "result.txt").write_text("", encoding="utf-8")
					if mode == "missing-pkg-config":
						arguments.append(f"-DPKG_CONFIG_EXECUTABLE={workspace / 'absent-pkg-config'}")
					completed = subprocess.run(arguments, cwd=REPO_ROOT, env=environment, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
					(workspace / (build.name + ".log")).write_text(completed.stdout + completed.stderr, encoding="utf-8")
					self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
					properties = dict(line.split("=", 1) for line in (build / "result.txt").read_text(encoding="utf-8").splitlines())
					if available:
						self.assertEqual(properties["found"], "1")
						self.assertEqual(Path(properties["INTERFACE_LINK_LIBRARIES"]).resolve(), library.resolve())
						self.assertIn(includes.as_posix(), properties["INTERFACE_INCLUDE_DIRECTORIES"])
						self.assertIn("-DQM_DBUS_FIXTURE=1", properties["INTERFACE_COMPILE_OPTIONS"])
						self.assertIn("-Wl,--as-needed", properties["INTERFACE_LINK_OPTIONS"])
					else:
						self.assertEqual(properties, {"found": "0"})


if __name__ == "__main__":
	unittest.main()
