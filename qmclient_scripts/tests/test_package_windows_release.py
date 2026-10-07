from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "qmclient_scripts"))
SPEC = importlib.util.spec_from_file_location("package_windows_release", REPO_ROOT / "qmclient_scripts/package_windows_release.py")
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class PublishWindowsArtifactsTest(unittest.TestCase):
	def setUp(self) -> None:
		self.temp = tempfile.TemporaryDirectory(dir=REPO_ROOT / "tmp", prefix="release-publish-")
		self.addCleanup(self.temp.cleanup)
		self.root = Path(self.temp.name)
		self.inputs = self.root / "build"
		self.inputs.mkdir()
		self.output = self.root / "QmClient_Release"

	def test_publishes_all_artifacts_with_matching_hashes_and_keeps_unrelated_files(self) -> None:
		artifacts = []
		for name, data in (("完整包.7z", b"normal"), ("Setup.exe", b"installer"), ("portable.zip", b"portable")):
			path = self.inputs / name
			path.write_bytes(data)
			artifacts.append(path)
		self.output.mkdir()
		(self.output / "user-owned.txt").write_bytes(b"keep")
		MODULE.publish_artifacts(artifacts, self.output)
		hashes = (self.output / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines()
		for artifact, line in zip(artifacts, hashes, strict=True):
			self.assertEqual((self.output / artifact.name).read_bytes(), artifact.read_bytes())
			self.assertEqual(line, f"{hashlib.sha256(artifact.read_bytes()).hexdigest()}  {artifact.name}")
		self.assertEqual((self.output / "user-owned.txt").read_bytes(), b"keep")

	def test_missing_input_preserves_previous_release_before_any_copy(self) -> None:
		valid = self.inputs / "normal.7z"
		valid.write_bytes(b"new")
		self.output.mkdir()
		(self.output / valid.name).write_bytes(b"old")
		with self.assertRaisesRegex(ValueError, "missing or empty"):
			MODULE.publish_artifacts([valid, self.inputs / "missing.zip"], self.output)
		self.assertEqual((self.output / valid.name).read_bytes(), b"old")
		self.assertEqual(len(list(self.output.iterdir())), 1)

	def test_duplicate_output_names_are_rejected_before_output_is_created(self) -> None:
		path = self.inputs / "normal.7z"
		path.write_bytes(b"normal")
		with self.assertRaisesRegex(ValueError, "duplicate"):
			MODULE.publish_artifacts([path, path], self.output)
		self.assertFalse(self.output.exists())


if __name__ == "__main__":
	unittest.main()
