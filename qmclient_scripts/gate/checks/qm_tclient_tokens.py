#!/usr/bin/env python3
"""TClient 页面裸浮点防回潮检查。

TClient 卡片页（字体/档案/配置/战争列表/状态栏/视觉/绑轮盘）的排版与动效数值
统一来自 ui_token::tclient::*。函数体内只允许 0.0/1.0 这类结构值；
确属物理常量且无法归类的行用 `// qm-token-allow: <原因>` 标注。
扫描前先剥离注释与字符串，避免注释里的“0.22 s”或文案里的“1.5”误报。
"""

from __future__ import annotations

import re
from pathlib import Path

from lib.report import ResultCollector


REPO_ROOT = Path(__file__).resolve().parents[3]
TCARD_DIR = REPO_ROOT / "src" / "game" / "client" / "QmUi" / "cards"
TRIGGER_PREFIX = "src/game/client/QmUi/cards/QmCardCatalogTClient"
ALLOW_PATTERN = re.compile(r"//\s*qm-token-allow:\s*\S+")

# 结构值：0/1 是索引、开关、透明度端点；-1 的语义已迁入 ui_token::tclient::NONE。
ALLOWED_VALUES = {0.0, 1.0}
# 覆盖 1.0f、1.0F、1.f、.5f、1e-3f、1E+2 等写法；后缀只吃掉 f/F/l/L。
FLOAT_PATTERN = re.compile(r"(?<![\w.])(?:(?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?|\d+[eE][+-]?\d+)[fFlL]?(?![\w.])")


def should_run(changed: list[str]) -> bool:
	normalized = [path.replace("\\", "/") for path in changed]
	return any(path.startswith(TRIGGER_PREFIX) for path in normalized)


def _display_path(path: Path) -> str:
	try:
		return str(path.relative_to(REPO_ROOT))
	except ValueError:
		return path.name


def _blank(chunk: str) -> str:
	return "".join("\n" if ch == "\n" else " " for ch in chunk)


def _strip_comments_and_strings(text: str) -> str:
	"""把注释、字符串与字符字面量替换为空格，保留换行与行号。"""
	out: list[str] = []
	i = 0
	n = len(text)
	state = "code"
	while i < n:
		ch = text[i]
		if state == "code":
			if ch == "/" and i + 1 < n and text[i + 1] == "/":
				state = "line_comment"
				out.append("  ")
				i += 2
				continue
			if ch == "/" and i + 1 < n and text[i + 1] == "*":
				state = "block_comment"
				out.append("  ")
				i += 2
				continue
			if ch == "R" and i + 1 < n and text[i + 1] == '"':
				j = i + 2
				while j < n and text[j] not in "(\n":
					j += 1
				if j < n and text[j] == "(":
					delimiter = text[i + 2 : j]
					terminator = ")" + delimiter + '"'
					end = text.find(terminator, j + 1)
					end = n if end < 0 else end + len(terminator)
					out.append(_blank(text[i:end]))
					i = end
					continue
			if ch == '"':
				state = "string"
				out.append(" ")
				i += 1
				continue
			if ch == "'":
				state = "char"
				out.append(" ")
				i += 1
				continue
			out.append(ch)
			i += 1
			continue

		if ch == "\n":
			if state == "line_comment":
				state = "code"
			out.append("\n")
			i += 1
			continue
		if state == "line_comment":
			out.append(" ")
			i += 1
			continue
		if state == "block_comment":
			if ch == "*" and i + 1 < n and text[i + 1] == "/":
				state = "code"
				out.append("  ")
				i += 2
				continue
			out.append(" ")
			i += 1
			continue
		# string / char
		quote = '"' if state == "string" else "'"
		if ch == "\\" and i + 1 < n:
			out.append("  ")
			i += 2
			continue
		if ch == quote:
			state = "code"
			out.append(" ")
			i += 1
			continue
		out.append(" ")
		i += 1
	return "".join(out)


def _scan_file(path: Path) -> list[tuple[int, str, str]]:
	violations: list[tuple[int, str, str]] = []
	text = path.read_text(encoding="utf-8")
	code_lines = _strip_comments_and_strings(text).splitlines()
	original_lines = text.splitlines()
	for line_no, (code, original) in enumerate(zip(code_lines, original_lines), start=1):
		if ALLOW_PATTERN.search(original):
			continue
		for match in FLOAT_PATTERN.finditer(code):
			raw = match.group(0)
			value = float(raw.rstrip("fFlL"))
			if value in ALLOWED_VALUES:
				continue
			violations.append((line_no, raw, original.strip()))
	return violations


def run(results: ResultCollector, changed: list[str], dry_run: bool = False) -> None:
	if not should_run(changed):
		results.add_not_applicable("TClient 裸浮点防回潮", "改动范围未触及 TClient 卡片页")
		return
	if dry_run:
		results.add("INFO", "TClient 裸浮点防回潮", "DryRun，仅展示命令")
		return

	# 页面 .cpp 与公共头（Internal.h）都算 TClient 页面数值；只扫 .cpp 会让头文件改动 false PASS。
	files = sorted(path for path in TCARD_DIR.glob("QmCardCatalogTClient*.*") if path.suffix in {".cpp", ".h", ".hpp"})
	if not files:
		results.add("FAIL", "TClient 裸浮点防回潮", f"未找到 {TCARD_DIR} 下的 TClient 卡片源文件")
		return

	violations: list[str] = []
	for path in files:
		for line_no, raw, line in _scan_file(path):
			violations.append(f"{_display_path(path)}:{line_no}: {raw} -> {line}")

	if violations:
		detail = "\n".join(violations[:20])
		if len(violations) > 20:
			detail += f"\n... 共 {len(violations)} 处"
		results.add("FAIL", "TClient 裸浮点防回潮", detail)
		return

	results.add("PASS", "TClient 裸浮点防回潮", f"{len(files)} 个 TClient 页面均使用 ui_token::tclient")
