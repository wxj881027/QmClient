"""零重叠清单的「独立可摘 vs 需前置」标注：不能把改上游新文件的提交算成可直接摘。"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
	sys.path.insert(0, str(REPO_ROOT))

from qmclient_scripts.support.upstream_status import format_clean_entry  # noqa: E402


class CleanEntryFormatTest(unittest.TestCase):
	def test_pickable_entry_has_no_warning(self) -> None:
		line = format_clean_entry("a23094c69a1", "cmake: Find libpng header", [])
		self.assertIn("a23094c69", line)
		self.assertIn("cmake: Find libpng header", line)
		self.assertNotIn("⚠", line)

	def test_prereq_entry_lists_missing_files(self) -> None:
		line = format_clean_entry(
			"6f941a85f00", "Fix wrong localization context", ["src/engine/client/backend_threaded.cpp"]
		)
		self.assertIn("⚠ 需前置", line)
		self.assertIn("src/engine/client/backend_threaded.cpp", line)

	def test_prereq_entry_truncates_long_file_lists(self) -> None:
		files = [f"src/new/file{i}.cpp" for i in range(5)]
		line = format_clean_entry("deadbeef000", "标题", files)
		self.assertIn("src/new/file0.cpp", line)
		self.assertIn("等", line)
		self.assertNotIn("src/new/file4.cpp", line)


if __name__ == "__main__":
	unittest.main()
