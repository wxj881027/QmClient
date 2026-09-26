from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "coverage"))
from collect_llvm_coverage import resolve_testrunner, validate_build_dir  # noqa: E402


class CoverageRunnerTest(unittest.TestCase):
	def test_rejects_shared_release_build_directories(self):
		for name in ("cmake-build-release", "cmake-build-release-msvc"):
			with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, "dedicated build directory"):
				validate_build_dir(Path(name))
	def test_windows_explicit_runner_without_suffix_prefers_exe(self):
		with tempfile.TemporaryDirectory() as directory:
			build = Path(directory)
			(binary := build / "testrunner.exe").write_bytes(b"runner")
			with mock.patch("collect_llvm_coverage.os.name", "nt"):
				self.assertEqual(resolve_testrunner(build, build / "testrunner"), binary.resolve())

	def test_windows_rejects_extensionless_non_windows_runner(self):
		with tempfile.TemporaryDirectory() as directory:
			build = Path(directory)
			(binary := build / "testrunner").write_bytes(b"elf")
			with mock.patch("collect_llvm_coverage.os.name", "nt"):
				with self.assertRaisesRegex(RuntimeError, "Linux/WSL"):
					resolve_testrunner(build, binary)


if __name__ == "__main__":
	unittest.main()
