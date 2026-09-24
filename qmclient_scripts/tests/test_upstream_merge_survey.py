"""`upstream_merge_survey.py` 的排序/格式化：融合对象要先看「双方都动得多」的文件。"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
	sys.path.insert(0, str(REPO_ROOT))

from qmclient_scripts.support.upstream_merge_survey import format_table, sort_rows  # noqa: E402


class MergeSurveySortTest(unittest.TestCase):
	def test_sorts_by_combined_commit_count(self) -> None:
		rows = [
			("a.cpp", 1, 1, 10, 10),
			("b.cpp", 30, 30, 5, 5),
			("c.cpp", 2, 20, 1, 100),
		]
		# (path, 上游提交, 本地提交, 上游改动, 本地改动)
		self.assertEqual([row[0] for row in sort_rows(rows)], ["b.cpp", "c.cpp", "a.cpp"])

	def test_ties_break_on_local_churn(self) -> None:
		rows = [
			("small.cpp", 5, 5, 1, 1),
			("big.cpp", 5, 5, 1, 9999),
		]
		self.assertEqual([row[0] for row in sort_rows(rows)], ["big.cpp", "small.cpp"])

	def test_table_has_header_and_one_line_per_row(self) -> None:
		lines = format_table(sort_rows([("x.cpp", 3, 4, 5, 6)]))
		self.assertEqual(len(lines), 2)
		self.assertIn("文件", lines[0])
		self.assertIn("x.cpp", lines[1])
		self.assertIn("3", lines[1])
		self.assertIn("6", lines[1])


if __name__ == "__main__":
	unittest.main()
