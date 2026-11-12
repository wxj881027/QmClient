#!/usr/bin/env python3
"""浮层语义防回潮检查。

audit_overlay_usage.py --check 判定两件事：
  1. 硬错误必须为 0：ShowPopupConfirm(..., false) 的伪气泡确认、
     新路径里的 YesNoButtons()（指代不明）；
  2. 旧 CMenus::PopupMessage/PopupConfirm 调用点与长 tooltip 字面量
     按文件计数只许减少 —— 批 3 迁移期间这条基线就是「不许新增旧写法」。

基线只在债务真实减少时通过 --update-baseline --reason 收紧。
"""

from __future__ import annotations

from pathlib import Path

from lib import runner
from lib.report import ResultCollector


REPO_ROOT = Path(__file__).resolve().parents[3]
AUDIT_SCRIPT = REPO_ROOT / "qmclient_scripts" / "audit_overlay_usage.py"

TRIGGER_PREFIXES = ("src/game/client/",)


def should_run(changed: list[str]) -> bool:
	normalized = [path.replace("\\", "/") for path in changed]
	return any(path.startswith(TRIGGER_PREFIXES) for path in normalized)


def run(results: ResultCollector, changed: list[str], dry_run: bool = False) -> None:
	if not should_run(changed):
		results.add_not_applicable("浮层语义防回潮", "改动范围未触及 src/game/client")
		return
	if dry_run:
		results.add("INFO", "浮层语义防回潮", "DryRun，仅展示命令")
		return
	code, out = runner.run_python(
		str(AUDIT_SCRIPT),
		"--check",
		title="浮层语义防回潮",
		check=False,
	)
	summary = out.strip().splitlines()[-1] if out.strip() else "无输出"
	if code != 0:
		results.add("FAIL", "浮层语义防回潮", out.strip() or "audit 脚本退出码非零")
		return
	results.add("PASS", "浮层语义防回潮", summary)
