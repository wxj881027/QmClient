#!/usr/bin/env python3
"""CI Linux 依赖单一来源守卫的 gate 接线。"""

from __future__ import annotations

from pathlib import Path

from lib import runner
from lib.report import ResultCollector

REPO_ROOT = Path(__file__).resolve().parents[3]


def run(results: ResultCollector, included: list[str], dry_run: bool = False) -> None:
	if dry_run:
		results.add("INFO", "CI 依赖单一来源守卫", "DryRun，仅展示命令")
		return
	code, out = runner.run_python(
		str(REPO_ROOT / "qmclient_scripts" / "check_ci_deps.py"),
		title="CI 依赖单一来源守卫",
		check=False,
	)
	if code != 0:
		results.add("FAIL", "CI 依赖单一来源守卫", out)
		return
	results.add("PASS", "CI 依赖单一来源守卫", "所有 Linux 构建作业依赖已被单一来源清单覆盖")
