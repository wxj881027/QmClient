#!/usr/bin/env python3
"""UI 裸颜色字面量防回潮检查。

QmUi 与 qmclient 组件层的绘制代码必须引用 UiTokens.h 的语义 token；
audit_ui_color_tokens.py --check 按文件计数基线判定回潮（新增裸值即失败）。
基线只在债务真实减少时通过 --update-baseline 收紧。
"""

from __future__ import annotations

from pathlib import Path

from lib import runner
from lib.report import ResultCollector


REPO_ROOT = Path(__file__).resolve().parents[3]
AUDIT_SCRIPT = REPO_ROOT / "qmclient_scripts" / "audit_ui_color_tokens.py"

TRIGGER_PREFIXES = ("src/game/client/QmUi/", "src/game/client/components/qmclient/")


def should_run(changed: list[str]) -> bool:
	normalized = [path.replace("\\", "/") for path in changed]
	return any(path.startswith(TRIGGER_PREFIXES) for path in normalized)


def run(results: ResultCollector, changed: list[str], dry_run: bool = False) -> None:
	if not should_run(changed):
		results.add_not_applicable("UI 裸颜色防回潮", "改动范围未触及 QmUi/qmclient UI 层")
		return
	if dry_run:
		results.add("INFO", "UI 裸颜色防回潮", "DryRun，仅展示命令")
		return
	code, out = runner.run_python(
		str(AUDIT_SCRIPT),
		"--check",
		title="UI 裸颜色防回潮",
		check=False,
	)
	summary = out.strip().splitlines()[-1] if out.strip() else "无输出"
	if code != 0:
		results.add("FAIL", "UI 裸颜色防回潮", out.strip() or "audit 脚本退出码非零")
		return
	results.add("PASS", "UI 裸颜色防回潮", summary)
