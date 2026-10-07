from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("prepare_setup_source", REPO_ROOT / "qmclient_scripts/prepare_setup_source.py")
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class PrepareSetupSourceTest(unittest.TestCase):
	def setUp(self) -> None:
		self.temp = tempfile.TemporaryDirectory(dir=REPO_ROOT / "tmp", prefix="setup-test-")
		self.addCleanup(self.temp.cleanup)
		self.root = Path(self.temp.name)
		self.source = self.root / "build"
		self.data = self.root / "data"
		self.source.mkdir()
		self.data.mkdir()
		(self.source / "DDNet.exe").write_bytes(b"client")
		(self.source / "DDNet-Server.exe").write_bytes(b"server")
		(self.source / "runtime.dll").write_bytes(b"runtime")
		for helper in ("qm-nmt-helper.exe", "qm-soda-helper.exe", "qm-music-helper.exe"):
			(self.source / helper).write_bytes(helper.encode())
		(self.source / "qmclient-setup-runtime.txt").write_text("DDNet.exe\nDDNet-Server.exe\nqm-nmt-helper.exe\nqm-soda-helper.exe\nqm-music-helper.exe\n", encoding="utf-8")
		(self.source / "qmclient-setup-generated.txt").write_text("", encoding="utf-8")
		(self.data / "asset.txt").write_bytes(b"asset")
		self.output = self.source / "setup-source"

	def test_copies_only_release_payload_and_replaces_previous_output(self) -> None:
		(self.source / "testrunner.exe").write_bytes(b"test")
		self.output.mkdir()
		(self.output / "obsolete.txt").write_bytes(b"old")
		MODULE.prepare(self.source, self.data, self.output)
		files = {p.relative_to(self.output).as_posix() for p in self.output.rglob("*") if p.is_file()}
		self.assertEqual(files, {"DDNet.exe", "DDNet-Server.exe", "runtime.dll", "data/asset.txt", "qm-nmt-helper.exe", "qm-soda-helper.exe", "qm-music-helper.exe"})
		self.assertEqual((self.output / "data/asset.txt").read_bytes(), b"asset")
		for helper in ("qm-nmt-helper.exe", "qm-soda-helper.exe", "qm-music-helper.exe"):
			self.assertEqual((self.output / helper).read_bytes(), helper.encode())
		self.assertEqual((self.source / "testrunner.exe").read_bytes(), b"test")
		MODULE.prepare(self.source, self.data, self.output)
		self.assertEqual((self.output / "DDNet.exe").read_bytes(), b"client")

	def test_portable_or_test_build_preserves_existing_setup_payload(self) -> None:
		self.output.mkdir()
		(self.output / "previous.txt").write_bytes(b"old")
		for option in ("QMCLIENT_PORTABLE", "QMCLIENT_TEST_STORAGE"):
			with self.subTest(option=option):
				(self.source / "CMakeCache.txt").write_text(f"{option}:BOOL=ON\n", encoding="utf-8")
				with self.assertRaisesRegex(ValueError, "normal client build"):
					MODULE.prepare(self.source, self.data, self.output)
				self.assertEqual((self.output / "previous.txt").read_bytes(), b"old")

	def test_missing_executable_preserves_previous_payload(self) -> None:
		(self.source / "DDNet-Server.exe").unlink()
		self.output.mkdir()
		(self.output / "previous.txt").write_bytes(b"old")
		with self.assertRaisesRegex(ValueError, "missing required executable"):
			MODULE.prepare(self.source, self.data, self.output)
		self.assertEqual((self.output / "previous.txt").read_bytes(), b"old")

	def test_missing_media_helper_preserves_previous_payload(self) -> None:
		self.output.mkdir()
		(self.output / "previous.txt").write_bytes(b"old")
		for helper in ("qm-nmt-helper.exe", "qm-soda-helper.exe", "qm-music-helper.exe"):
			with self.subTest(helper=helper):
				path = self.source / helper
				content = path.read_bytes()
				path.unlink()
				try:
					with self.assertRaisesRegex(ValueError, "missing required executable"):
						MODULE.prepare(self.source, self.data, self.output)
					self.assertEqual((self.output / "previous.txt").read_bytes(), b"old")
				finally:
					path.write_bytes(content)

	def test_runtime_manifest_includes_new_targets_without_copying_tools(self) -> None:
		(self.source / "qm-client-updater.exe").write_bytes(b"updater")
		(self.source / "map_convert.exe").write_bytes(b"tool")
		with (self.source / "qmclient-setup-runtime.txt").open("a", encoding="utf-8") as manifest:
			manifest.write("qm-client-updater.exe\n")
		MODULE.prepare(self.source, self.data, self.output)
		self.assertEqual((self.output / "qm-client-updater.exe").read_bytes(), b"updater")
		self.assertFalse((self.output / "map_convert.exe").exists())

	def test_disabled_helper_is_not_required_when_absent_from_runtime_manifest(self) -> None:
		helper = "qm-nmt-helper.exe"
		(self.source / helper).unlink()
		manifest = self.source / "qmclient-setup-runtime.txt"
		manifest.write_text(manifest.read_text(encoding="utf-8").replace(f"{helper}\n", ""), encoding="utf-8")
		MODULE.prepare(self.source, self.data, self.output)
		self.assertFalse((self.output / helper).exists())
		self.assertEqual((self.output / "qm-soda-helper.exe").read_bytes(), b"qm-soda-helper.exe")

	def test_missing_manifest_preserves_previous_payload(self) -> None:
		(self.source / "qmclient-setup-runtime.txt").unlink()
		self.output.mkdir()
		(self.output / "previous.txt").write_bytes(b"old")
		with self.assertRaisesRegex(ValueError, "missing CMake Setup runtime manifest"):
			MODULE.prepare(self.source, self.data, self.output)
		self.assertEqual((self.output / "previous.txt").read_bytes(), b"old")

	def test_invalid_manifest_preserves_previous_payload_and_external_file(self) -> None:
		outside = self.root / "outside.exe"
		outside.write_bytes(b"external")
		self.output.mkdir()
		(self.output / "previous.txt").write_bytes(b"old")
		for content in ("", "../outside.exe\n", "..\\outside.exe\n", "C:/outside.exe\n", "data/asset.txt\n", "DDNet.exe\n\n", "DDNet.exe\nddnet.exe\n"):
			with self.subTest(content=content):
				(self.source / "qmclient-setup-runtime.txt").write_text(content, encoding="utf-8")
				with self.assertRaisesRegex(ValueError, "Setup runtime manifest"):
					MODULE.prepare(self.source, self.data, self.output)
				self.assertEqual((self.output / "previous.txt").read_bytes(), b"old")
				self.assertEqual(outside.read_bytes(), b"external")

	def test_missing_manifest_runtime_dll_preserves_previous_payload(self) -> None:
		with (self.source / "qmclient-setup-runtime.txt").open("a", encoding="utf-8") as manifest:
			manifest.write("qm-nmt-hook64.dll\n")
		self.output.mkdir()
		(self.output / "previous.txt").write_bytes(b"old")
		with self.assertRaisesRegex(ValueError, "missing required runtime DLL"):
			MODULE.prepare(self.source, self.data, self.output)
		self.assertEqual((self.output / "previous.txt").read_bytes(), b"old")

	def test_generated_shader_uses_built_binary_instead_of_stale_repository_data(self) -> None:
		name = "data/shader/vulkan/procedural_ring.frag.spv"
		built = self.source / name
		built.parent.mkdir(parents=True)
		built.write_bytes(b"compiled-shader")
		stale = self.data / "shader/vulkan/procedural_ring.frag.spv"
		stale.parent.mkdir(parents=True)
		stale.write_bytes(b"stale-shader")
		(self.source / "qmclient-setup-generated.txt").write_text(name, encoding="utf-8")
		MODULE.prepare(self.source, self.data, self.output)
		self.assertEqual((self.output / name).read_bytes(), b"compiled-shader")
		self.assertEqual(stale.read_bytes(), b"stale-shader")

	def test_missing_generated_shader_preserves_previous_payload(self) -> None:
		(self.source / "qmclient-setup-generated.txt").write_text("data/shader/vulkan/procedural_ring.frag.spv", encoding="utf-8")
		self.output.mkdir()
		(self.output / "previous.txt").write_bytes(b"old")
		with self.assertRaisesRegex(ValueError, "missing required generated asset"):
			MODULE.prepare(self.source, self.data, self.output)
		self.assertEqual((self.output / "previous.txt").read_bytes(), b"old")

	def test_missing_generated_manifest_preserves_previous_payload(self) -> None:
		(self.source / "qmclient-setup-generated.txt").unlink()
		self.output.mkdir()
		(self.output / "previous.txt").write_bytes(b"old")
		with self.assertRaisesRegex(ValueError, "missing CMake Setup generated asset manifest"):
			MODULE.prepare(self.source, self.data, self.output)
		self.assertEqual((self.output / "previous.txt").read_bytes(), b"old")

	def test_invalid_generated_manifest_preserves_previous_payload(self) -> None:
		self.output.mkdir()
		(self.output / "previous.txt").write_bytes(b"old")
		for content in ("../outside.spv", "data/../../outside.spv", "data/./shader.spv", "data\\shader.spv", "/data/shader.spv", "data/C:/shader.spv", "DDNet.exe", "data/shader.spv\ndata/SHADER.spv"):
			with self.subTest(content=content):
				(self.source / "qmclient-setup-generated.txt").write_text(content, encoding="utf-8")
				with self.assertRaisesRegex(ValueError, "Setup generated asset manifest"):
					MODULE.prepare(self.source, self.data, self.output)
				self.assertEqual((self.output / "previous.txt").read_bytes(), b"old")

	def test_missing_runtime_dll_preserves_previous_payload(self) -> None:
		(self.source / "runtime.dll").unlink()
		self.output.mkdir()
		(self.output / "previous.txt").write_bytes(b"old")
		with self.assertRaisesRegex(ValueError, "no runtime DLLs"):
			MODULE.prepare(self.source, self.data, self.output)
		self.assertEqual((self.output / "previous.txt").read_bytes(), b"old")

	def test_temporary_directory_cannot_replace_source(self) -> None:
		source = self.root / ".payload.tmp"
		self.source.rename(source)
		with self.assertRaises(ValueError):
			MODULE.prepare(source, self.data, self.root / "payload")
		self.assertEqual((source / "DDNet.exe").read_bytes(), b"client")

	def test_output_inside_data_is_rejected_before_creating_temporary_directory(self) -> None:
		with self.assertRaises(ValueError):
			MODULE.prepare(self.source, self.data, self.data / "payload")
		self.assertEqual(list(self.data.iterdir()), [self.data / "asset.txt"])

	def test_output_cannot_replace_input_or_ancestor(self) -> None:
		for output in (self.source, self.data, self.root):
			with self.subTest(output=output), self.assertRaises(ValueError):
				MODULE.prepare(self.source, self.data, output)
		self.assertEqual((self.source / "DDNet.exe").read_bytes(), b"client")


if __name__ == "__main__":
	unittest.main()
