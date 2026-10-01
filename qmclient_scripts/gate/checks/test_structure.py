"""测试层级与注册库存检查，不以静态库存代替行为验证。"""

from __future__ import annotations

import json
from pathlib import Path
import sys

from lib import runner
from lib.report import ResultCollector

REPO_ROOT = Path(__file__).resolve().parents[3]
INVENTORY_SCRIPT = REPO_ROOT / "qmclient_scripts/test_inventory.py"


def run(results: ResultCollector, included: list[str], dry_run: bool = False) -> None:
	if dry_run:
		results.add("INFO", "测试层级与注册库存", "DryRun，扫描完整仓库库存，不运行测试")
		return
	code, output = runner.run(
		[sys.executable, str(INVENTORY_SCRIPT), "--root", str(REPO_ROOT), "--json"],
		title="测试层级与注册库存", check=False,
	)
	if code != 0:
		results.add("FAIL", "测试层级与注册库存", output)
		return
	try:
		inventory = json.loads(output)
		registration = inventory["cmake_registration"]
		if registration is None:
			raise ValueError("缺少 CMake 注册库存")
		problems = {
			"混合声明": inventory["structural_violations"],
			"未注册文件": registration["unregistered_files"],
			"缺失源码": registration["missing_sources"],
			"重复测试 ID": inventory["duplicate_tests"],
		}
		failures = {name: items for name, items in problems.items() if items}
	except (ValueError, KeyError, TypeError) as error:
		results.add("FAIL", "测试层级与注册库存", f"库存读取失败：{error}")
		return
	if failures:
		results.add("FAIL", "测试层级与注册库存", json.dumps(failures, ensure_ascii=False, indent=2))
	else:
		results.add("PASS", "测试层级与注册库存", "混合声明、未注册文件、缺失源码与重复 ID 均为零；不代表行为或性能覆盖")
