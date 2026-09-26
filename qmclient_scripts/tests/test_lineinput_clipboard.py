"""编译并运行真实行输入逻辑，不访问系统剪贴板或启动客户端。"""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO_ROOT = Path(__file__).resolve().parents[2]


class LineInputClipboardTest(unittest.TestCase):
	def test_multiline_paste_limits_each_callback(self) -> None:
		generated = REPO_ROOT / "cmake-build-release/src"
		self.assertTrue(
			(generated / "generated/protocol.h").is_file(),
			"先配置并构建 cmake-build-release 的 generate_protocol 目标",
		)
		sources = [
			REPO_ROOT / "qmclient_scripts/tests/fixtures/lineinput_clipboard_test.cpp",
			REPO_ROOT / "src/game/client/lineinput.cpp",
			REPO_ROOT / "src/base/str.cpp",
			REPO_ROOT / "src/base/mem.cpp",
			REPO_ROOT / "src/base/unicode/tolower.cpp",
			REPO_ROOT / "src/base/unicode/tolower_data.cpp",
		]
		temporary_root = REPO_ROOT / "tmp"
		temporary_root.mkdir(exist_ok=True)
		with tempfile.TemporaryDirectory(prefix="lineinput-clipboard-", dir=temporary_root) as directory:
			executable = Path(directory) / ("lineinput_clipboard.exe" if os.name == "nt" else "lineinput_clipboard")
			if os.name == "nt":
				command = [
					"cmd.exe",
					"/c",
					str(REPO_ROOT / "qmclient_scripts/cmake-windows.cmd"),
					"-E",
					"chdir",
					directory,
					"cl",
					"/nologo",
					"/std:c++20",
					"/EHsc",
					"/O2",
					"/utf-8",
					f"/I{REPO_ROOT / 'src'}",
					f"/I{REPO_ROOT / 'src/rust-bridge'}",
					f"/I{generated}",
					*map(str, sources),
					f"/Fe:{executable}",
				]
			else:
				compiler = os.environ.get("CXX", "c++")
				self.assertIsNotNone(shutil.which(compiler), f"缺少编译器：{compiler}")
				command = [
					compiler,
					"-std=c++20",
					"-O2",
					f"-I{REPO_ROOT / 'src'}",
					f"-I{REPO_ROOT / 'src/rust-bridge'}",
					f"-I{generated}",
					*map(str, sources),
					"-o",
					str(executable),
				]
			build = subprocess.run(command, cwd=directory, capture_output=True, timeout=120)
			self.assertEqual(build.returncode, 0, (build.stdout + build.stderr).decode(errors="replace"))
			run = subprocess.run([str(executable)], cwd=directory, capture_output=True, timeout=30)
			self.assertEqual(run.returncode, 0, (run.stdout + run.stderr).decode(errors="replace"))


if __name__ == "__main__":
	unittest.main()
