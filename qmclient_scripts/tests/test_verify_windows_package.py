from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path
import zipfile
import shutil
import subprocess

REPO_ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("verify_windows_package", REPO_ROOT / "qmclient_scripts/verify_windows_package.py")
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class VerifyWindowsPackageTest(unittest.TestCase):
	def setUp(self) -> None:
		self.temp = tempfile.TemporaryDirectory(dir=REPO_ROOT / "tmp", prefix="package-inventory-")
		self.addCleanup(self.temp.cleanup)
		self.root = Path(self.temp.name)
		self.runtime = self.root / "runtime.txt"
		self.tools = self.root / "tools.txt"
		self.package = self.root / "package.zip"
		self.runtime.write_text("DDNet.exe\nDDNet-Server.exe\nQmClient-Updater.exe\nqm-nmt-helper.exe\nqm-nmt-hook64.dll\n", encoding="utf-8")
		self.tools.write_text("map_extract.exe\ntwping.exe\n", encoding="utf-8")
		self.runtime_names = {"DDNet.exe", "DDNet-Server.exe", "QmClient-Updater.exe", "qm-nmt-helper.exe"}

	def write_package(self, extra: tuple[str, ...] = (), missing: str = "", portable: bool = False) -> None:
		names = self.runtime_names | (set() if portable else {"map_extract.exe", "twping.exe"})
		with zipfile.ZipFile(self.package, "w") as archive:
			for name in sorted(names - {missing}):
				archive.writestr(f"QmClient/{name}", b"binary")
			archive.writestr("QmClient/data/fonts/中文字体.ttf", b"font")
			archive.writestr("QmClient/SDL2.dll", b"runtime")
			for name in extra:
				archive.writestr(name, b"extra")

	def test_normal_archive_contains_declared_runtime_and_complete_tool_selection(self) -> None:
		self.write_package()
		MODULE.verify(self.package, self.runtime, self.tools, False)

	def test_portable_accepts_runtime_only_and_rejects_tool_contamination(self) -> None:
		self.tools.write_text("", encoding="utf-8")
		self.write_package(portable=True)
		MODULE.verify(self.package, self.runtime, self.tools, True)
		self.write_package(extra=("QmClient/map_extract.exe",), portable=True)
		with self.assertRaisesRegex(ValueError, "extra"):
			MODULE.verify(self.package, self.runtime, self.tools, True)

	def test_missing_updater_in_build_manifest_is_rejected(self) -> None:
		self.runtime.write_text("DDNet.exe\nDDNet-Server.exe\n", encoding="utf-8")
		self.write_package()
		with self.assertRaisesRegex(ValueError, "missing required"):
			MODULE.verify(self.package, self.runtime, self.tools, False)

	def test_portable_rejects_build_manifest_that_declares_tools(self) -> None:
		self.write_package()
		with self.assertRaisesRegex(ValueError, "must not declare tools"):
			MODULE.verify(self.package, self.runtime, self.tools, True)

	def test_missing_helper_or_selected_tool_rejects_archive(self) -> None:
		for missing in ("qm-nmt-helper.exe", "map_extract.exe"):
			with self.subTest(missing=missing):
				self.write_package(missing=missing)
				with self.assertRaisesRegex(ValueError, "missing/duplicate"):
					MODULE.verify(self.package, self.runtime, self.tools, False)

	def test_test_and_developer_programs_are_rejected(self) -> None:
		for name in ("testrunner.exe", "qm-nmt-tests.exe", "qm-benchmarks.exe", "dummy_map.exe", "crapnet.exe", "packetgen.exe"):
			with self.subTest(name=name):
				self.write_package(extra=(f"QmClient/{name}",))
				with self.assertRaisesRegex(ValueError, "extra"):
					MODULE.verify(self.package, self.runtime, self.tools, False)

	def test_nested_or_case_duplicate_executable_is_rejected(self) -> None:
		for name in ("QmClient/tools/map_extract.exe", "Other/MAP_EXTRACT.EXE"):
			with self.subTest(name=name):
				self.write_package(extra=(name,))
				with self.assertRaisesRegex(ValueError, "missing/duplicate"):
					MODULE.verify(self.package, self.runtime, self.tools, False)

	@unittest.skipUnless(shutil.which("7z"), "7z CLI is not installed")
	def test_real_sevenzip_archive_inventory_rejects_unexpected_program(self) -> None:
		directory = self.root / "QmClient"
		directory.mkdir()
		for name in self.runtime_names | {"map_extract.exe", "twping.exe"}:
			(directory / name).write_bytes(b"program")
		package = self.root / "package.7z"
		subprocess.run(["7z", "a", "-t7z", str(package), directory.name], cwd=self.root, check=True, capture_output=True, timeout=30)
		MODULE.verify(package, self.runtime, self.tools, False)
		(directory / "testrunner.exe").write_bytes(b"test program")
		subprocess.run(["7z", "a", "-t7z", str(package), directory.name], cwd=self.root, check=True, capture_output=True, timeout=30)
		with self.assertRaisesRegex(ValueError, "extra"):
			MODULE.verify(package, self.runtime, self.tools, False)

	def test_removed_bundled_resources_are_rejected(self) -> None:
		for relative in MODULE.REMOVED_BUNDLED_RESOURCES:
			with self.subTest(relative=relative):
				self.write_package(extra=(f"QmClient/{relative}",))
				with self.assertRaisesRegex(ValueError, "removed bundled resources"):
					MODULE.verify(self.package, self.runtime, self.tools, False)

	def test_lossy_resource_name_rejects_package(self) -> None:
		self.write_package(extra=("QmClient/data/fonts/??.ttf",))
		with self.assertRaisesRegex(ValueError, "lossy file names"):
			MODULE.verify(self.package, self.runtime, self.tools, False)


if __name__ == "__main__":
	unittest.main()
