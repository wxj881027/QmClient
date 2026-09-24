# 请抬头享受阳光｜日子很好 我很我---------致咩子
"""地图工具功能冒烟：dummy_map 产出的地图必须可被 map_convert_07 接受。

覆盖 S11 落地的工具修复（`dummy_map` 两层各自持有 tile 数据、`map_convert_07` use-after-free 等）：
这些工具此前只有编译级证据，缺少「真的跑一遍」的验证。

说明：
- 工具的存储根在 Windows 上是 `%APPDATA%\\DDNet`（`$USERDIR`），所以产物固定落在
  `%APPDATA%\\DDNet\\maps\\dummy3.map`；本测试会**先备份、后恢复/删除**该文件，不污染用户目录。
- 工具未构建时自动跳过；默认在仓库的 `cmake-build-release` / `cmake-build-debug` 里找工具，
  可用 `QM_MAP_TOOLS_BUILD_DIR=<dir>` 指向别的构建目录（例如同步工作的 worktree 构建）。
- 二进制若比源码旧，测到的就不是当前改动；断言信息里会带上实际使用的可执行文件路径。

运行：
	python -m unittest discover -s qmclient_scripts/tests -p "test_map_tools_smoke.py" -v
"""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
BUILD_DIRS = (REPO_ROOT / "cmake-build-release", REPO_ROOT / "cmake-build-debug")
# 允许显式指定构建目录（例如同步工作在 worktree 里构建时）：
#   QM_MAP_TOOLS_BUILD_DIR=<repo>/tmp/sync-slice-1/cmake-build-release
BUILD_DIR_ENV = "QM_MAP_TOOLS_BUILD_DIR"
CRC_RE = re.compile(r"CRC32 ([0-9A-Fa-f]{8}), SHA256 ([0-9a-f]{64})")


def build_dirs() -> tuple[Path, ...]:
    override = os.environ.get(BUILD_DIR_ENV)
    if override:
        return (Path(override),)
    return BUILD_DIRS


def find_tool(name: str) -> Path | None:
    for build_dir in build_dirs():
        for candidate in (build_dir / f"{name}.exe", build_dir / name):
            if candidate.is_file():
                return candidate
    return None


def run_tool(exe: Path, args: list[str], cwd: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(exe), *args],
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


@unittest.skipUnless(sys.platform == "win32", "工具的 $USERDIR 落在 %APPDATA%\\DDNet（Windows 专用）")
class MapToolsSmokeTest(unittest.TestCase):
    def test_dummy_map_output_is_a_convertible_map(self) -> None:
        dummy_map = find_tool("dummy_map")
        map_convert_07 = find_tool("map_convert_07")
        if dummy_map is None or map_convert_07 is None:
            self.skipTest("地图工具未构建（需要 cmake --target dummy_map map_convert_07）")

        appdata = os.environ.get("APPDATA")
        if not appdata:
            self.skipTest("APPDATA 未设置")
        userdir_map = Path(appdata) / "DDNet" / "maps" / "dummy3.map"

        # 产物必须来自当前源码的构建：旧的二进制会静默通过编译级检查却跑不出新行为，
        # 所以把实际使用的可执行文件写进断言信息，便于排查「测的不是这个改动」。
        used = f"（dummy_map={dummy_map}, map_convert_07={map_convert_07}）"

        backup = userdir_map.read_bytes() if userdir_map.is_file() else None
        temp_dir = Path(tempfile.mkdtemp(prefix="qm-map-tools-"))
        try:
            produced = run_tool(dummy_map, [], temp_dir)
            self.assertEqual(produced.returncode, 0, f"dummy_map 退出码非 0{used}：\n{produced.stdout[-2000:]}")
            self.assertIn("Dummy map written", produced.stdout, f"dummy_map 未报告写出地图{used}：\n{produced.stdout[-2000:]}")

            match = CRC_RE.search(produced.stdout)
            self.assertIsNotNone(match, f"未能从工具输出解析 CRC32/SHA256：\n{produced.stdout}")
            assert match is not None
            expected_crc = int(match.group(1), 16)
            expected_sha = match.group(2)

            self.assertTrue(userdir_map.is_file(), f"工具未产出 {userdir_map}")
            data = userdir_map.read_bytes()
            self.assertEqual(data[:4], b"DATA", "产物不是 DDNet 地图（缺少 DATA magic）")
            self.assertGreater(len(data), 256, "产物过小，两层地图结构不完整")

            # 工具自报的 CRC32/SHA256 必须与实际文件一致（写-读回一致）
            self.assertEqual(zlib.crc32(data) & 0xFFFFFFFF, expected_crc, "CRC32 与工具自报不一致")
            self.assertEqual(hashlib.sha256(data).hexdigest(), expected_sha, "SHA256 与工具自报不一致")

            # 产出的 0.6 地图必须能被 map_convert_07 接受（覆盖其 use-after-free 修复路径）
            converted = temp_dir / "dummy07.map"
            result = run_tool(map_convert_07, [str(userdir_map), str(converted)], temp_dir)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertTrue(converted.is_file(), "map_convert_07 未产出文件")
            self.assertEqual(converted.read_bytes()[:4], b"DATA", "转换结果不是合法地图")
        finally:
            if backup is None:
                userdir_map.unlink(missing_ok=True)
            else:
                userdir_map.write_bytes(backup)
            shutil.rmtree(temp_dir, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
