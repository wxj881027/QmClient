"""`upstream_status.py` 的基线新鲜度检查：落后 origin/master 时必须给出可执行的警告。"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
	sys.path.insert(0, str(REPO_ROOT))

from qmclient_scripts.support.upstream_status import format_baseline_freshness  # noqa: E402


class BaselineFreshnessFormatTest(unittest.TestCase):
	def test_up_to_date_has_no_warning(self) -> None:
		line = format_baseline_freshness(
			{"origin_ref": "origin/master", "exists": True, "behind": 0, "tip": "abc1234 2026-09-24 标题"}
		)
		self.assertIn("已跟上 origin/master", line)
		self.assertNotIn("⚠", line)

	def test_behind_reports_count_and_action(self) -> None:
		line = format_baseline_freshness(
			{"origin_ref": "origin/master", "exists": True, "behind": 5, "tip": "abc1234 2026-09-24 标题"}
		)
		self.assertIn("⚠", line)
		self.assertIn("落后 origin/master 5 个提交", line)
		# 警告必须指向「先前移基线」的动作与教训出处，否则没人会照做。
		self.assertIn("先前移基线", line)
		self.assertIn("S37", line)

	def test_missing_ref_tells_you_to_fetch(self) -> None:
		line = format_baseline_freshness({"origin_ref": "origin/master", "exists": False, "behind": 0, "tip": ""})
		self.assertIn("未找到 origin/master", line)
		self.assertIn("git fetch origin", line)

	def test_custom_origin_ref_is_echoed(self) -> None:
		line = format_baseline_freshness(
			{"origin_ref": "upstream/main", "exists": True, "behind": 1, "tip": "deadbee 2026-09-24 标题"}
		)
		self.assertIn("upstream/main", line)


if __name__ == "__main__":
	unittest.main()
