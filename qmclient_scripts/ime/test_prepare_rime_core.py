"""依赖准备接口的离线测试；不执行编译、客户端或网络请求。"""

import hashlib
import io
import os
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import prepare_rime_core as core


class PrepareRimeCore(unittest.TestCase):
    def test_offline_missing_input_does_not_access_network(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(core.urllib.request, "urlopen") as network:
            with self.assertRaisesRegex(ValueError, "离线输入缺失"):
                core.acquire(Path(directory) / "missing", "https://unused.invalid", "0" * 64, False)
            network.assert_not_called()

    def test_corrupt_cache_is_rejected_without_downloading_over_it(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(core.urllib.request, "urlopen") as network:
            cached = Path(directory) / "archive"
            cached.write_bytes(b"changed input")
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                core.acquire(cached, "https://unused.invalid", "0" * 64, True)
            self.assertEqual(cached.read_bytes(), b"changed input")
            network.assert_not_called()

    def test_bad_download_is_not_published_as_a_cache_entry(self):
        with tempfile.TemporaryDirectory() as directory:
            cached = Path(directory) / "archive"
            with patch.object(core.urllib.request, "urlopen", return_value=io.BytesIO(b"bad download")):
                with self.assertRaisesRegex(ValueError, "下载 SHA-256"):
                    core.acquire(cached, "https://unused.invalid", "0" * 64, True)
            self.assertFalse(cached.exists())

    @staticmethod
    def make_archive(path, name="package/source.txt"):
        with tarfile.open(path, "w:gz") as archive:
            entry = tarfile.TarInfo(name)
            entry.size = len(b"original")
            archive.addfile(entry, io.BytesIO(b"original"))

    def test_source_reuse_preserves_local_edits_and_stops(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            self.make_archive(work / "source.tar.gz")
            source = core.unpack(work / "source.tar.gz", work / "sources")
            core.unpack(work / "source.tar.gz", work / "sources")
            (source / "source.txt").write_bytes(b"local edit")
            with self.assertRaisesRegex(ValueError, "固定源码已被修改"):
                core.unpack(work / "source.tar.gz", work / "sources")
            self.assertEqual((source / "source.txt").read_bytes(), b"local edit")

    def test_archive_path_escape_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            self.make_archive(work / "source.tar.gz", "package/../../escape.txt")
            with self.assertRaises(tarfile.FilterError):
                core.unpack(work / "source.tar.gz", work / "sources")
            self.assertFalse((work / "escape.txt").exists())

    def test_runtime_notices_include_bundled_header_licenses(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / "LICENSE").write_text("Full core notice", encoding="utf-8")
            (source / "utf8.h").write_text("// Original author\n/* Full header permission */\n#ifndef UTF8_H\ncode", encoding="utf-8")
            lock = {"sources": [{"name": "librime", "revision": "fixed", "url": "source-url",
                                 "sha256": "a" * 64, "license": "BSD", "notices": ["LICENSE"],
                                 "header_notices": ["utf8.h"]}], "extra_notices": []}
            notices = core.render_notices(lock, {"librime": source}, {})
            self.assertIn("Full core notice", notices)
            self.assertIn("Original author\n/* Full header permission */", notices)
            self.assertNotIn("#ifndef UTF8_H", notices)

    def test_build_clears_inherited_plugins_and_only_builds_library_targets(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            sources = {name: work / "sources" / name for name in ("librime", "boost", "leveldb", "marisa", "yaml-cpp", "opencc")}
            for source in sources.values():
                (source / "src").mkdir(parents=True)
            output = work / "build/opencc/src"
            output.mkdir(parents=True)
            for name in ("opencc.lib", "Opencc_Export.h", "opencc_config.h"):
                (output / name).write_bytes(b"build output")
            (work / "deps-prefix/lib").mkdir(parents=True)
            with patch.object(core.sys, "platform", "win32"), patch.dict(os.environ, {"RIME_PLUGINS": "lotem/librime-octagram"}):
                with patch.object(core, "run_cmake") as runner:
                    core.build(work, sources, 2)
            targets = []
            for args, env in (call.args for call in runner.call_args_list):
                self.assertEqual(env["RIME_PLUGINS"], "")
                if "--target" in args:
                    targets.append(args[args.index("--target") + 1])
            self.assertEqual(targets, ["install", "install", "install", "libopencc", "rime"])
            configuration = runner.call_args_list[-2].args[0]
            self.assertIn("-DENABLE_EXTERNAL_PLUGINS=OFF", configuration)
            self.assertIn("-DBUILD_MERGED_PLUGINS=OFF", configuration)

    def test_receipt_records_copied_runtime_and_notices(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / "build/librime/bin").mkdir(parents=True)
            (work / "build/librime/bin/rime.dll").write_bytes(b"built core")
            source = work / "source"
            (source / "src").mkdir(parents=True)
            (source / "src/rime_api.h").write_bytes(b"fixed API header")
            destination = core.install_runtime(work, {"librime": source}, "Complete notices\n")
            receipt = dict(line.split("=", 1) for line in (destination / "rime-core-build.txt").read_text().splitlines())
            self.assertEqual(receipt["plugins"], "none")
            self.assertEqual(receipt["dll_sha256"], hashlib.sha256(b"built core").hexdigest())
            self.assertEqual(receipt["header_sha256"], hashlib.sha256(b"fixed API header").hexdigest())
            self.assertEqual(receipt["notices_sha256"], hashlib.sha256(b"Complete notices\n").hexdigest())

    def test_failed_install_has_no_ready_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            with self.assertRaises(FileNotFoundError):
                core.install_runtime(work, {"librime": work / "missing"}, "Notices")
            self.assertFalse((work / "install/rime-core-build.txt").exists())


if __name__ == "__main__":
    unittest.main()
