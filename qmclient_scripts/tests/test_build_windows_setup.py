from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "qmclient_scripts"))
import build_windows_setup as setup


class BuildWindowsSetupTest(unittest.TestCase):
	def setUp(self) -> None:
		self.temp = tempfile.TemporaryDirectory(dir=REPO_ROOT / "tmp", prefix="native-setup-build-")
		self.addCleanup(self.temp.cleanup)
		self.root = Path(self.temp.name)
		self.source = self.root / "中文 载荷"
		self.source.mkdir()
		(self.source / "DDNet.exe").write_bytes(b"client")
		self.output = self.root / "release"
		self.native = self.root / "native build"
		self.identity = "B0AB5D1A-7228-45F7-9DDC-AD453939B6EA"
		self.commands = []

	def fake_compiler(self, command, **kwargs):
		self.commands.append(command)
		if command[0] == "iscc-test":
			(self.native / "engine/QmClient-Setup.exe").write_bytes(b"inno engine")
		elif "--build" in command:
			(self.native / "QmClient-Setup.exe").write_bytes(b"native UI containing engine")
		return subprocess.CompletedProcess(command, 0, b"compiled")

	def build(self):
		return setup.build_setup(self.source, self.output, self.native, "3.3-preview.2", compiler="iscc-test", app_id=self.identity, jobs=2)

	def test_publishes_native_installer_with_matching_engine_identity(self) -> None:
		with patch.object(setup.subprocess, "run", side_effect=self.fake_compiler):
			result = self.build()
		self.assertEqual(result.read_bytes(), b"native UI containing engine")
		self.assertIn("/DSetupAppId={{" + self.identity + "}", self.commands[0])
		self.assertIn("-DQM_SETUP_APP_ID=" + self.identity, self.commands[1])
		self.assertIn("-DQM_SETUP_VERSION=3.3-preview.2", self.commands[1])
		self.assertIn("-DQM_SETUP_NUMERIC_VERSION=3,3,2,0", self.commands[1])
		self.assertIn("/DSourceDir=" + str(self.source), self.commands[0])
		self.assertEqual(self.commands[2][-2:], ["-j", "2"])

	def test_native_build_failure_preserves_previous_installer(self) -> None:
		self.output.mkdir()
		previous = self.output / "QmClient-Setup.exe"
		previous.write_bytes(b"previous installer")
		def fail_native(command, **kwargs):
			if "--build" in command:
				return subprocess.CompletedProcess(command, 1)
			return self.fake_compiler(command, **kwargs)
		with patch.object(setup.subprocess, "run", side_effect=fail_native):
			with self.assertRaisesRegex(RuntimeError, "Native setup compilation failed"):
				self.build()
		self.assertEqual(previous.read_bytes(), b"previous installer")

	def test_inno_warning_stops_before_native_build_and_publication(self) -> None:
		with patch.object(setup.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, b"Warning: missing translation")) as runner:
			with self.assertRaisesRegex(RuntimeError, "emitted warnings"):
				self.build()
		self.assertEqual(runner.call_count, 1)
		self.assertFalse(self.output.exists())

	def test_rejects_invalid_version_and_identity_before_build(self) -> None:
		for version in ('3.3"', "3.3.1", "3.3-preview.-1", "65536.1"):
			with self.subTest(version=version), self.assertRaises(ValueError):
				setup.numeric_version(version)
		with self.assertRaises(ValueError):
			setup.setup_identity("not-a-guid")


if __name__ == "__main__":
	unittest.main()
