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
		(self.data / "asset.txt").write_bytes(b"asset")
		self.output = self.source / "setup-source"

	def test_copies_only_release_payload_and_replaces_previous_output(self) -> None:
		(self.source / "testrunner.exe").write_bytes(b"test")
		self.output.mkdir()
		(self.output / "obsolete.txt").write_bytes(b"old")
		MODULE.prepare(self.source, self.data, self.output)
		files = {p.relative_to(self.output).as_posix() for p in self.output.rglob("*") if p.is_file()}
		self.assertEqual(files, {"DDNet.exe", "DDNet-Server.exe", "runtime.dll", "data/asset.txt"})
		self.assertEqual((self.output / "data/asset.txt").read_bytes(), b"asset")
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
