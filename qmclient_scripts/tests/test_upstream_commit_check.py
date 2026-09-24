# 请抬头享受阳光｜日子很好 我很我---------致咩子
from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "qmclient_scripts/support/upstream_commit_check.py"
SPEC = importlib.util.spec_from_file_location("upstream_commit_check", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


SYNTHETIC_DIFF = """diff --git a/src/base/aio.cpp b/src/base/aio.cpp
--- a/src/base/aio.cpp
+++ b/src/base/aio.cpp
@@ -10,0 +11,3 @@
+enum class EAsyncIoFinishState : unsigned char
+{
+	RUNNING,
diff --git a/src/base/os.cpp b/src/base/os.cpp
--- a/src/base/os.cpp
+++ b/src/base/os.cpp
@@ -1,0 +2 @@
+#include <base/mem.h>
"""


class UpstreamCommitCheckTest(unittest.TestCase):
    def test_added_lines_groups_by_file_and_skips_trivial(self) -> None:
        additions = CHECK.nontrivial_added_lines(SYNTHETIC_DIFF)

        # os.cpp 只新增了一行 #include，属于被跳过的类别，因此整个文件被过滤掉
        self.assertEqual(set(additions), {"src/base/aio.cpp"})
        # `{` 被 `SKIP_LINES` 过滤；枚举名与枚举成员行是有效指纹
        self.assertEqual(
            additions["src/base/aio.cpp"],
            ["enum class EAsyncIoFinishState : unsigned char", "RUNNING,"],
        )

    def test_coverage_counts_stripped_matches(self) -> None:
        additions = {"src/a.cpp": ["int value = 1;", "return value;"]}
        files = {"src/a.cpp": "void f()\n{\n    int value = 1;\n}\n"}

        per_file, present, total = CHECK.coverage(additions, files.get)

        self.assertEqual(per_file["src/a.cpp"], (1, 2))
        self.assertEqual((present, total), (1, 2))

    def test_coverage_treats_missing_file_as_absent(self) -> None:
        additions = {"src/missing.cpp": ["int value = 1;"]}

        per_file, present, total = CHECK.coverage(additions, lambda _path: None)

        self.assertEqual(per_file["src/missing.cpp"], (0, 1))
        self.assertEqual((present, total), (0, 1))

    def test_verdict_thresholds(self) -> None:
        self.assertEqual(CHECK.verdict(0, 0), "NO-CODE-CHANGES")
        self.assertEqual(CHECK.verdict(90, 100), "ALREADY-PRESENT")
        self.assertEqual(CHECK.verdict(9, 10), "ALREADY-PRESENT")
        self.assertEqual(CHECK.verdict(50, 100), "PARTIAL")
        self.assertEqual(CHECK.verdict(15, 100), "PARTIAL")
        self.assertEqual(CHECK.verdict(14, 100), "ABSENT")
        self.assertEqual(CHECK.verdict(0, 10), "ABSENT")


if __name__ == "__main__":
    unittest.main()
