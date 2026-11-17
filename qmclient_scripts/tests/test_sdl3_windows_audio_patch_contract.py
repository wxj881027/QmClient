from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
CMAKE = shutil.which("cmake")
PATCH_SCRIPT = REPO_ROOT / "cmake/PatchSDL3WindowsAudio.cmake"
IMPLEMENTATION = REPO_ROOT / "qmclient_scripts/sdl3/SDL_qm_immdevice_info.h"
FIXTURE = Path(__file__).parent / "fixtures/sdl3/SDL_immdevice.c"
INCLUDE = b'#include "SDL_qm_immdevice_info.h"'


@unittest.skipUnless(CMAKE, "CMake is required for the dependency patch contract")
class Sdl3WindowsAudioPatchContractTest(unittest.TestCase):
	# 仅检查构建补丁应用、重复应用和源码漂移；设备行为由 C++ 测试验证。
	def setUp(self) -> None:
		tmp_root = REPO_ROOT / "tmp"
		tmp_root.mkdir(exist_ok=True)
		self.source_root = Path(tempfile.mkdtemp(prefix="sdl3-audio-patch-", dir=tmp_root))
		self.source_file = self.source_root / "src/core/windows/SDL_immdevice.c"
		self.source_file.parent.mkdir(parents=True)
		self.source_file.write_bytes(FIXTURE.read_bytes())
		self.header = self.source_file.with_name("SDL_qm_immdevice_info.h")

	def apply_patch(self) -> subprocess.CompletedProcess[str]:
		return subprocess.run(
			[CMAKE, f"-DSDL3_SOURCE_DIR={self.source_root}", "-P", str(PATCH_SCRIPT)],
			capture_output=True, text=True, encoding="utf-8", timeout=30, check=False,
		)

	def test_patch_installs_shared_implementation_and_preserves_surrounding_source(self) -> None:
		result = self.apply_patch()
		self.assertEqual(result.returncode, 0, result.stderr)
		patched = self.source_file.read_bytes()
		self.assertEqual(patched.count(INCLUDE), 1)
		self.assertIn(b"/* fixture prefix */", patched)
		self.assertTrue(patched.endswith(b"/* fixture suffix */\n"))
		self.assertEqual(self.header.read_bytes(), IMPLEMENTATION.read_bytes())

	def test_reapplying_patch_refreshes_cached_implementation(self) -> None:
		first = self.apply_patch()
		self.assertEqual(first.returncode, 0, first.stderr)
		patched = self.source_file.read_bytes()
		self.header.write_bytes(b"stale implementation\n")
		second = self.apply_patch()
		self.assertEqual(second.returncode, 0, second.stderr)
		self.assertEqual(self.source_file.read_bytes(), patched)
		self.assertEqual(self.header.read_bytes(), IMPLEMENTATION.read_bytes())

	def test_crlf_source_keeps_its_line_endings(self) -> None:
		self.source_file.write_bytes(FIXTURE.read_bytes().replace(b"\n", b"\r\n"))
		result = self.apply_patch()
		self.assertEqual(result.returncode, 0, result.stderr)
		patched = self.source_file.read_bytes()
		self.assertIn(INCLUDE + b"\r\n", patched)
		self.assertNotIn(b"\n", patched.replace(b"\r\n", b""))

	def test_unrecognized_source_fails_without_changing_it(self) -> None:
		original = b"upstream implementation changed\n"
		self.source_file.write_bytes(original)
		result = self.apply_patch()
		self.assertNotEqual(result.returncode, 0)
		self.assertIn("unexpected SDL3 Windows audio source", result.stderr)
		self.assertEqual(self.source_file.read_bytes(), original)
		self.assertFalse(self.header.exists())


if __name__ == "__main__":
	unittest.main()
