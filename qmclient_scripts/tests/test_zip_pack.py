# 请抬头享受阳光｜日子很好 我很我---------致咩子
from __future__ import annotations

import importlib.util
import os
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "qmclient_scripts/zip_pack.py"
SPEC = importlib.util.spec_from_file_location("zip_pack", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
ZIP_PACK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ZIP_PACK)


class ZipPackTest(unittest.TestCase):
    def _write_tree(self, root: Path) -> None:
        (root / "data/qmclient/fonts/霞鹜文楷").mkdir(parents=True)
        (root / "DDNet.exe").write_bytes(b"client")
        (root / "data/qmclient/fonts/霞鹜新致宋.ttf").write_bytes(b"zhi-song")
        (root / "data/qmclient/fonts/霞鹜新晰黑.ttf").write_bytes(b"xi-hei")
        (root / "data/qmclient/fonts/方正屏显雅宋.TTF").write_bytes(b"ya-song")
        (
            root / "data/qmclient/fonts/霞鹜文楷/LXGWWenKai-Regular.ttf"
        ).write_bytes(b"wen-kai")

    def test_keeps_distinct_non_ascii_names_with_utf8_flag(self) -> None:
        with tempfile.TemporaryDirectory(prefix="qm-zip-pack-") as temp_dir:
            workspace = Path(temp_dir)
            source = workspace / "QmClient-3.0-win64"
            self._write_tree(source)
            package = workspace / "QmClient-windows.zip"

            count = ZIP_PACK.pack(source, package)

            with zipfile.ZipFile(package) as archive:
                entries = {info.filename: info for info in archive.infolist()}
                expected = {
                    "QmClient-3.0-win64/",
                    "QmClient-3.0-win64/DDNet.exe",
                    "QmClient-3.0-win64/data/",
                    "QmClient-3.0-win64/data/qmclient/",
                    "QmClient-3.0-win64/data/qmclient/fonts/",
                    "QmClient-3.0-win64/data/qmclient/fonts/霞鹜新致宋.ttf",
                    "QmClient-3.0-win64/data/qmclient/fonts/霞鹜新晰黑.ttf",
                    "QmClient-3.0-win64/data/qmclient/fonts/方正屏显雅宋.TTF",
                    "QmClient-3.0-win64/data/qmclient/fonts/霞鹜文楷/",
                    "QmClient-3.0-win64/data/qmclient/fonts/霞鹜文楷/LXGWWenKai-Regular.ttf",
                }
                self.assertEqual(set(entries), expected)
                self.assertEqual(count, len(expected))
                for name in expected:
                    if name.endswith("/") or name.isascii():
                        continue
                    self.assertTrue(
                        entries[name].flag_bits & 0x800,
                        f"缺少 UTF-8 标志: {name}",
                    )
                self.assertEqual(
                    archive.read("QmClient-3.0-win64/data/qmclient/fonts/霞鹜新致宋.ttf"),
                    b"zhi-song",
                )

    def test_names_are_never_replaced_by_conversion_placeholders(self) -> None:
        with tempfile.TemporaryDirectory(prefix="qm-zip-pack-") as temp_dir:
            workspace = Path(temp_dir)
            source = workspace / "QmClient-3.0-win64"
            self._write_tree(source)
            package = workspace / "QmClient-windows.zip"

            ZIP_PACK.pack(source, package)

            with zipfile.ZipFile(package) as archive:
                names = archive.namelist()
            self.assertEqual(len(names), len(set(names)))
            for name in names:
                self.assertNotIn("?", name)
                self.assertNotIn("\ufffd", name)

    def test_rejects_lossy_archive_names(self) -> None:
        for name in (
            "QmClient-3.0-win64/data/qmclient/fonts/?????.ttf",
            "QmClient-3.0-win64/data\\file.txt",
            "QmClient-3.0-win64/../DDNet.exe",
            "QmClient-3.0-win64/data/file.txt ",
        ):
            with self.subTest(name=name):
                with self.assertRaises(ValueError):
                    ZIP_PACK._validate_archive_name(name)

    def test_rejects_output_inside_source_directory(self) -> None:
        with tempfile.TemporaryDirectory(prefix="qm-zip-pack-") as temp_dir:
            source = Path(temp_dir) / "QmClient-3.0-win64"
            self._write_tree(source)

            with self.assertRaisesRegex(ValueError, "输出文件不能落在"):
                ZIP_PACK.pack(source, source / "QmClient-windows.zip")

    def test_rejects_source_without_files(self) -> None:
        with tempfile.TemporaryDirectory(prefix="qm-zip-pack-") as temp_dir:
            workspace = Path(temp_dir)
            source = workspace / "QmClient-3.0-win64"
            source.mkdir()
            (source / "empty").mkdir()

            with self.assertRaisesRegex(ValueError, "便携目录为空"):
                ZIP_PACK.pack(source, workspace / "QmClient-windows.zip")

    def test_console_that_cannot_encode_chinese_does_not_fail_the_pack(self) -> None:
        # CI 的 Windows 构建把 stdout 接到 cp1252 上，中文提示曾让打包命令
        # 在写完 ZIP 之后以 UnicodeEncodeError 失败。
        with tempfile.TemporaryDirectory(prefix="qm-zip-pack-") as temp_dir:
            workspace = Path(temp_dir)
            source = workspace / "QmClient-3.0-win64"
            self._write_tree(source)
            package = workspace / "QmClient-windows.zip"
            environment = dict(os.environ, PYTHONIOENCODING="cp1252")

            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT_PATH),
                    "--source",
                    str(source),
                    "--output",
                    str(package),
                ],
                capture_output=True,
                env=environment,
                check=False,
            )

            self.assertEqual(
                result.returncode, 0, result.stderr.decode("utf-8", "replace")
            )
            self.assertTrue(package.is_file())
            with zipfile.ZipFile(package) as archive:
                self.assertIn(
                    "QmClient-3.0-win64/data/qmclient/fonts/霞鹜新致宋.ttf",
                    archive.namelist(),
                )


if __name__ == "__main__":
    unittest.main()
