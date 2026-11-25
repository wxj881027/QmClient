#!/usr/bin/env python3
"""TClient 页面裸浮点防回潮检查。

TClient 卡片页（字体/档案/配置/战争列表/状态栏/视觉/绑轮盘）的排版与动效数值
统一来自 ui_token::tclient::*。函数体内只允许 0.0/1.0 这类结构值；
确属物理常量且无法归类的行用 `// qm-token-allow: <原因>` 标注。
"""

from __future__ import annotations

import re
from pathlib import Path

from lib.report import ResultCollector


REPO_ROOT = Path(__file__).resolve().parents[3]
TCARD_DIR = REPO_ROOT / "src" / "game" / "client" / "QmUi" / "cards"
TRIGGER_PREFIX = "src/game/client/QmUi/cards/QmCardCatalogTClient"
ALLOW_MARKER = "qm-token-allow"

# 结构值：0/1 是索引、开关、透明度端点；-1 的语义已迁入 ui_token::tclient::NONE。
ALLOWED_VALUES = {0.0, 1.0}
FLOAT_PATTERN = re.compile(r"(?<![\w.])(-?\d+\.\d+f?)(?![\w])")


def should_run(changed: list[str]) -> bool:
	normalized = [path.replace("\\", "/") for path in changed]
	return any(path.startswith(TRIGGER_PREFIX) for path in normalized)


def _scan_file(path: Path) -> list[tuple[int, str, str]]:
	violations: list[tuple[int, str, str]] = []
	for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
		if ALLOW_MARKER in line:
			continue
		for match in FLOAT_PATTERN.finditer(line):
			raw = match.group(1)
			value = float(raw.rstrip("f"))
			if value in ALLOWED_VALUES:
				continue
			violations.append((line_no, raw, line.strip()))
	return violations


def run(results: ResultCollector, changed: list[str], dry_run: bool = False) -> None:
	if not should_run(changed):
		results.add_not_applicable("TClient 裸浮点防回潮", "改动范围未触及 TClient 卡片页")
		return
	if dry_run:
		results.add("INFO", "TClient 裸浮点防回潮", "DryRun，仅展示命令")
		return

	files = sorted(TCARD_DIR.glob("QmCardCatalogTClient*.cpp"))
	if not files:
		results.add("FAIL", "TClient 裸浮点防回潮", f"未找到 {TCARD_DIR} 下的 TClient 卡片源文件")
		return

	violations: list[str] = []
	for path in files:
		for line_no, raw, line in _scan_file(path):
			violations.append(f"{path.relative_to(REPO_ROOT)}:{line_no}: {raw} -> {line}")

	if violations:
		detail = "\n".join(violations[:20])
		if len(violations) > 20:
			detail += f"\n... 共 {len(violations)} 处"
		results.add("FAIL", "TClient 裸浮点防回潮", detail)
		return

	results.add("PASS", "TClient 裸浮点防回潮", f"{len(files)} 个 TClient 页面均使用 ui_token::tclient")
