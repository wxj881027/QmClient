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
		# 每个基线包都带一套匹配捆绑版本的 FFmpeg 运行时，否则校验会先因缺 DLL 失败。
		self.ffmpeg_names = {"avcodec-63.dll", "avformat-63.dll", "avutil-61.dll", "swresample-7.dll", "swscale-10.dll"}

	def write_package(self, extra: tuple[str, ...] = (), missing: str = "", portable: bool = False, steam_appid: bool = True) -> None:
		names = self.runtime_names | (set() if portable else {"map_extract.exe", "twping.exe"})
		with zipfile.ZipFile(self.package, "w") as archive:
			for name in sorted((names - {missing}) | self.ffmpeg_names):
				archive.writestr(f"QmClient/{name}", b"binary")
			archive.writestr("QmClient/data/fonts/中文字体.ttf", b"font")
			archive.writestr("QmClient/freetype.dll", b"runtime")
			if steam_appid:
				archive.writestr("QmClient/steam_appid.txt", b"412220")
			for name in extra:
				archive.writestr(name, b"extra")

	def test_obsolete_sdl2_runtime_is_rejected(self) -> None:
		self.write_package(extra=("QmClient/SDL2.dll",))
		with self.assertRaisesRegex(ValueError, "removed bundled resources"):
			MODULE.verify(self.package, self.runtime, self.tools, False)

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

	def test_missing_root_steam_appid_is_rejected(self) -> None:
		self.write_package(steam_appid=False)
		with self.assertRaisesRegex(ValueError, "steam_appid"):
			MODULE.verify(self.package, self.runtime, self.tools, False)
		# data 子目录里的同名文件不能替代包根位置。
		self.write_package(steam_appid=False, extra=("QmClient/data/steam_appid.txt",))
		with self.assertRaisesRegex(ValueError, "steam_appid"):
			MODULE.verify(self.package, self.runtime, self.tools, False)

	@unittest.skipUnless(shutil.which("7z"), "7z CLI is not installed")
	def test_real_sevenzip_archive_inventory_rejects_unexpected_program(self) -> None:
		directory = self.root / "QmClient"
		directory.mkdir()
		for name in self.runtime_names | {"map_extract.exe", "twping.exe"} | self.ffmpeg_names:
			(directory / name).write_bytes(b"program")
		(directory / "steam_appid.txt").write_bytes(b"412220")
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

	def test_theme_thumbnails_are_bundled_and_not_treated_as_obsolete(self) -> None:
		# 主题缩略图在精简时丢失、随后回填；校验清单若仍把它们列为废弃资源，
		# 打包会在校验阶段拒绝刚回填的资产（Windows/Portable 发布步骤失败）。
		themes = {
			"data/themes/auto.png",
			"data/themes/autumn.png",
			"data/themes/heavens.png",
			"data/themes/jungle.png",
			"data/themes/newyear.png",
			"data/themes/none.png",
			"data/themes/rand.png",
			"data/themes/winter.png",
		}
		self.assertFalse(themes & {name.casefold() for name in MODULE.REMOVED_BUNDLED_RESOURCES})
		self.write_package(extra=tuple(f"QmClient/{name}" for name in sorted(themes)))
		MODULE.verify(self.package, self.runtime, self.tools, False)

	def test_ffmpeg_runtime_requires_one_dll_per_family(self) -> None:
		self.write_package()
		MODULE.verify(self.package, self.runtime, self.tools, False)
		# 旧版残留：同一族出现两份会让客户端加载到不匹配的 ABI。
		self.write_package(extra=("QmClient/avcodec-61.dll",))
		with self.assertRaisesRegex(ValueError, "multiple FFmpeg runtime DLLs"):
			MODULE.verify(self.package, self.runtime, self.tools, False)

	def test_ffmpeg_runtime_requires_every_family(self) -> None:
		for missing in ("avcodec-63.dll", "swscale-10.dll"):
			with self.subTest(missing=missing):
				# 从基线包里删掉指定族，模拟打包遗漏；工具程序也要带上，
				# 否则会先触发可执行文件清单校验。
				with zipfile.ZipFile(self.package, "w") as archive:
					for name in sorted((self.runtime_names | self.ffmpeg_names | {"map_extract.exe", "twping.exe"}) - {missing}):
						archive.writestr(f"QmClient/{name}", b"binary")
					archive.writestr("QmClient/steam_appid.txt", b"412220")
				with self.assertRaisesRegex(ValueError, "missing FFmpeg runtime DLLs"):
					MODULE.verify(self.package, self.runtime, self.tools, False)

	def test_ffmpeg_runtime_must_match_bundled_library_versions(self) -> None:
		libdir = self.root / "ffmpeg-lib64"
		libdir.mkdir()
		for name in ("avcodec-63.dll", "avformat-63.dll", "avutil-61.dll", "swresample-7.dll", "swscale-10.dll"):
			(libdir / name).write_bytes(b"dll")
		# 归档里是 FFmpeg 7 的一套（61/59/5/8），与捆绑的 FFmpeg 9 不匹配。
		old = ("avcodec-61.dll", "avformat-61.dll", "avutil-59.dll", "swresample-5.dll", "swscale-8.dll")
		with zipfile.ZipFile(self.package, "w") as archive:
			for name in sorted(self.runtime_names | {"map_extract.exe", "twping.exe"} | set(old)):
				archive.writestr(f"QmClient/{name}", b"binary")
			archive.writestr("QmClient/steam_appid.txt", b"412220")
		with self.assertRaisesRegex(ValueError, "does not match bundled libraries"):
			MODULE.verify(self.package, self.runtime, self.tools, False, libdir)
		self.write_package()
		MODULE.verify(self.package, self.runtime, self.tools, False, libdir)

	def test_lossy_resource_name_rejects_package(self) -> None:
		self.write_package(extra=("QmClient/data/fonts/??.ttf",))
		with self.assertRaisesRegex(ValueError, "lossy file names"):
			MODULE.verify(self.package, self.runtime, self.tools, False)


if __name__ == "__main__":
	unittest.main()
