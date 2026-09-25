"""MSVC /analyze「上游 src/base 豁免表」必须精确：只豁免指定文件 + 指定警告码。"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
GATE_DIR = REPO_ROOT / "qmclient_scripts" / "gate"
if str(GATE_DIR) not in sys.path:
	sys.path.insert(0, str(GATE_DIR))

from checks import strict_build  # noqa: E402


def _warning(path: str, line: int, code: str) -> str:
	return f"D:\\repo\\{path}({line}) : warning {code}: 示例告警"


class AnalyzeUpstreamBaseAllowlistTest(unittest.TestCase):
	def test_allowlisted_file_and_code_is_exempt(self) -> None:
		for (allowed_path, allowed_code) in strict_build._ANALYZE_UPSTREAM_BASE_ALLOWLIST:
			with self.subTest(path=allowed_path, code=allowed_code):
				self.assertIsNotNone(
					strict_build._analyze_allowlist_reason(_warning(allowed_path, 1, allowed_code))
				)

	def test_same_code_in_own_code_is_not_exempt(self) -> None:
		# 同一警告码出现在我方自有文件里必须仍然阻断，否则豁免范围就失控了。
		self.assertIsNone(
			strict_build._analyze_allowlist_reason(
				_warning("src/game/client/components/hud.cpp", 10, "C6262")
			)
		)
		self.assertIsNone(
			strict_build._analyze_allowlist_reason(
				_warning("src/base/unicode/confusables_data.cpp", 10, "C6262")
			)
		)

	def test_same_file_with_other_code_is_not_exempt(self) -> None:
		self.assertIsNone(
			strict_build._analyze_allowlist_reason(_warning("src/base/aio.cpp", 78, "C6011"))
		)
		self.assertIsNone(
			strict_build._analyze_allowlist_reason(_warning("src/base/net.cpp", 922, "C6262"))
		)

	def test_non_analyze_lines_are_not_matched(self) -> None:
		self.assertIsNone(strict_build._analyze_allowlist_reason("cl : Command line warning D9002"))
		self.assertIsNone(strict_build._analyze_allowlist_reason(""))

	def test_every_entry_carries_a_reason(self) -> None:
		# 豁免必须留理由：没有理由的条目不允许存在。
		for key, reason in strict_build._ANALYZE_UPSTREAM_BASE_ALLOWLIST.items():
			with self.subTest(key=key):
				self.assertTrue(reason.strip())


if __name__ == "__main__":
	unittest.main()
