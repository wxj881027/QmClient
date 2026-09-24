# 请抬头享受阳光｜日子很好 我很我---------致咩子
from __future__ import annotations

import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "qmclient_scripts/support/upstream_commit_check.py"
SPEC = importlib.util.spec_from_file_location("upstream_commit_check", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


def run(repo: Path, *args: str) -> None:
    subprocess.run(["git", "-C", str(repo), *args], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


class RevertMapTest(unittest.TestCase):
    def _make_repo(self, temp_dir: str) -> Path:
        repo = Path(temp_dir)
        run(repo, "init", "-q")
        run(repo, "config", "user.email", "t@example.com")
        run(repo, "config", "user.name", "t")
        (repo / "a.txt").write_text("1\n", encoding="utf-8")
        run(repo, "add", "a.txt")
        run(repo, "commit", "-q", "-m", "Fix something")
        (repo / "a.txt").write_text("2\n", encoding="utf-8")
        run(repo, "commit", "-qam", 'Revert "Fix something"')
        return repo

    def test_revert_map_detects_reverted_subject(self) -> None:
        with tempfile.TemporaryDirectory(prefix="qm-revert-audit-") as temp_dir:
            repo = self._make_repo(temp_dir)
            CHECK._REVERT_CACHE.clear()

            reverts = CHECK.revert_map(repo)

            self.assertIn("Fix something", reverts)

    def test_revert_warning_reports_revert_and_reapply(self) -> None:
        with tempfile.TemporaryDirectory(prefix="qm-revert-audit-") as temp_dir:
            repo = self._make_repo(temp_dir)
            (repo / "a.txt").write_text("3\n", encoding="utf-8")
            run(repo, "commit", "-qam", 'Revert "Revert "Fix something""')
            CHECK._REVERT_CACHE.clear()

            warning = CHECK.revert_warning(repo, "Fix something")
            plain = CHECK.revert_warning(repo, "Untouched subject")

            self.assertIsNotNone(warning)
            self.assertIn("净效果是保留", warning or "")
            self.assertIsNone(plain)

    def test_revert_warning_reports_plain_revert(self) -> None:
        with tempfile.TemporaryDirectory(prefix="qm-revert-audit-") as temp_dir:
            repo = self._make_repo(temp_dir)
            CHECK._REVERT_CACHE.clear()

            warning = CHECK.revert_warning(repo, "Fix something")

            self.assertIsNotNone(warning)
            self.assertIn("已被上游回退", warning or "")


if __name__ == "__main__":
    unittest.main()
