"""真实联网 runner 的落盘验证和配置选择行为测试；不访问网络。"""
from pathlib import Path
import hashlib
import tempfile
import unittest

from qmclient_scripts.integration.update_download_live import REPO_ROOT, inspect_download, service_groups


class DownloadIntegrityTest(unittest.TestCase):
    def setUp(self):
        directory = REPO_ROOT / "tmp/tests/update-download-live"
        directory.mkdir(parents=True, exist_ok=True)
        self.root = Path(tempfile.mkdtemp(dir=directory))

    def test_html_with_matching_size_and_hash_is_not_package(self):
        body = b"<html>not an archive</html>"
        path = self.root / "package.zip"
        path.write_bytes(body)
        result = inspect_download(path, len(body), hashlib.sha256(body).hexdigest(), "zip")
        self.assertFalse(result["valid"])
        self.assertFalse(result["magic_valid"])

    def test_matching_magic_does_not_hide_wrong_hash_or_size(self):
        body = b"PK\x03\x04payload"
        path = self.root / "package.zip"
        path.write_bytes(body)
        digest = hashlib.sha256(body).hexdigest()
        self.assertTrue(inspect_download(path, len(body), digest, "zip")["valid"])
        self.assertFalse(inspect_download(path, len(body) + 1, digest, "zip")["valid"])
        self.assertFalse(inspect_download(path, len(body), "00" * 32, "zip")["valid"])

    def test_missing_file_or_invalid_integrity_metadata_is_rejected(self):
        self.assertFalse(inspect_download(self.root / "missing", 1, "00" * 32, "7z")["valid"])
        self.assertFalse(inspect_download(self.root / "missing", 1, "bad", "7z")["valid"])

    def test_groups_use_capability_priority_and_enabled_flag(self):
        sources = [
            dict(prefix="https://disabled/", group="a", priority=0, resources=7, enabled=False),
            dict(prefix="https://a-second/", group="a", priority=20, resources=7, enabled=True),
            dict(prefix="https://a-first/", group="a", priority=10, resources=7, enabled=True),
            dict(prefix="https://download-only/", group="b", priority=30, resources=1, enabled=True),
        ]
        self.assertEqual([source["prefix"] for source in service_groups(sources, 4)], ["https://a-first/"])
        self.assertEqual([source["group"] for source in service_groups(sources, 1)], ["a", "b"])
