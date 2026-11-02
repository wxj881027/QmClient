#!/usr/bin/env python3
"""审计浮层语义用法：拦住回潮，量化批 3 迁移进度。

规范来源：docs/规格/2026-10-21-弹窗与二级菜单交互规范.md
  §1.5 术语映射 / §3.2 Tooltip 边界 / §3.7 ConfirmDialog 硬约束。

判定分两类：
  A. 硬错误（必须 0 处，不可用基线豁免）
     R1 ShowPopupConfirm(..., false)  —— 非居中确认 = 伪气泡，缺遮罩
     R2 YesNoButtons()                —— 新路径禁用 Yes/No，指代不明
  B. 计数基线（只许减少，不许增加）
     R3 旧 CMenus::PopupMessage/PopupConfirm 调用点（批 3 迁移债）
     R4 DoToolTip 里的长字符串字面量（≥80 字符，tooltip 越界承载段落）

模式：
  默认            打印报告
  --check         对照基线，任一文件计数超过基线即退出 1
  --update-baseline 用当前结果重写基线，须附 --reason（只许在债务真实减少时用）
  --report-json   输出全量清单
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
BASELINE_PATH = REPO_ROOT / "qmclient_scripts" / "overlay_usage_baseline.json"

DEFAULT_SCAN_ROOTS = ("src/game/client",)
SCAN_SUFFIXES = (".h", ".cpp")

# 长 tooltip 判定阈值：一段话的长度，超过就不是「名称补全」了。
TOOLTIP_LONG_LITERAL = 80

LEGACY_POPUP = re.compile(r"(?<!Show)\bPopup(Message|Confirm)\s*\(")
LEGACY_DEFINITION = re.compile(
	r"^\s*(static\s+)?(void|bool|CUi::EPopupMenuFunctionResult)\b.*\bPopup(Message|Confirm)\s*\("
)
# 渲染回调（CUi::PopupMessage / PopupConfirm）不是调用点。
LEGACY_RENDER_CALLBACK = re.compile(r"\bCUi::Popup(Message|Confirm)\s*\(")
CENTERED_FALSE = re.compile(r"ShowPopupConfirm\s*\(")
YES_NO_CALL = re.compile(r"\bYesNoButtons\s*\(\s*\)")
TOOLTIP_CALL = re.compile(r"\bDoToolTip\s*\(")
STRING_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')


def strip_line_comment(line: str) -> str:
	index = line.find("//")
	return line if index < 0 else line[:index]


def split_top_level(arguments: str) -> list[str]:
	"""按顶层逗号切分实参（忽略字符串与括号内的逗号）。"""
	parts: list[str] = []
	depth = 0
	in_string = False
	escaped = False
	current: list[str] = []
	for character in arguments:
		if in_string:
			current.append(character)
			if escaped:
				escaped = False
			elif character == "\\":
				escaped = True
			elif character == '"':
				in_string = False
			continue
		if character == '"':
			in_string = True
			current.append(character)
		elif character in "([{":
			depth += 1
			current.append(character)
		elif character in ")]}":
			depth -= 1
			current.append(character)
		elif character == "," and depth == 0:
			parts.append("".join(current).strip())
			current = []
		else:
			current.append(character)
	tail = "".join(current).strip()
	if tail:
		parts.append(tail)
	return parts


def call_arguments(text: str, open_index: int) -> tuple[str, int]:
	"""从 '(' 的下标开始，返回括号内文本与右括号下标。"""
	depth = 0
	in_string = False
	escaped = False
	for index in range(open_index, len(text)):
		character = text[index]
		if in_string:
			if escaped:
				escaped = False
			elif character == "\\":
				escaped = True
			elif character == '"':
				in_string = False
			continue
		if character == '"':
			in_string = True
		elif character == "(":
			depth += 1
		elif character == ")":
			depth -= 1
			if depth == 0:
				return text[open_index + 1 : index], index
	return "", -1


def line_of(text: str, index: int) -> int:
	return text.count("\n", 0, index) + 1


def scan_text(text: str) -> dict:
	"""对一段 C++ 源码做四类判定。纯函数、不碰磁盘，便于单测直接喂样例。"""
	hard: list[dict] = []
	legacy: list[dict] = []
	tooltips: list[dict] = []

	# R1：非居中确认。
	for match in CENTERED_FALSE.finditer(text):
		arguments, close_index = call_arguments(text, match.end() - 1)
		if close_index < 0:
			continue
		parts = split_top_level(arguments)
		if len(parts) >= 4 and parts[-1] == "false":
			hard.append(
				{
					"rule": "R1",
					"line": line_of(text, match.start()),
					"snippet": text[match.start() : close_index + 1].replace("\n", " ")[:160],
					"message": "ShowPopupConfirm 传了 CenterInViewport=false：这是伪气泡确认；单条可恢复动作用 Popconfirm，模态确认用居中",
				}
			)

	# R2：Yes/No 按钮（跳过函数定义本身）。
	for match in YES_NO_CALL.finditer(text):
		line_start = text.rfind("\n", 0, match.start()) + 1
		probe = text[line_start : match.end() + 1]
		if re.search(r"\bvoid\b[^;]*\bYesNoButtons\s*\(", probe):
			continue
		line_end = text.find("\n", match.start())
		snippet = text[line_start : line_end if line_end >= 0 else len(text)].strip()
		hard.append(
			{
				"rule": "R2",
				"line": line_of(text, match.start()),
				"snippet": snippet[:160],
				"message": "确认框用了 Yes/No：改用 DefaultButtons()（Cancel / OK）",
			}
		)

	# R3：旧 CMenus 弹窗。
	for line_number, raw_line in enumerate(text.splitlines(), start=1):
		code = strip_line_comment(raw_line)
		if not LEGACY_POPUP.search(code):
			continue
		if LEGACY_DEFINITION.search(code) or LEGACY_RENDER_CALLBACK.search(code):
			continue
		legacy.append({"line": line_number, "snippet": code.strip()[:160]})

	# R4：长 tooltip 字面量。
	for match in TOOLTIP_CALL.finditer(text):
		arguments, close_index = call_arguments(text, match.end() - 1)
		if close_index < 0:
			continue
		# 文本可能是第 2 或第 3 个实参，还常被 Localize("...") 包着：
		# 直接在整段实参里找最长的字符串字面量，签名变化不会让规则失效。
		literals = [literal.group(1) for literal in STRING_LITERAL.finditer(arguments)]
		if not literals:
			continue
		value = max(literals, key=len)
		if len(value) < TOOLTIP_LONG_LITERAL:
			continue
		tooltips.append(
			{
				"rule": "R4",
				"line": line_of(text, match.start()),
				"length": len(value),
				"snippet": value[:160],
				"message": "tooltip 字面量过长：段落说明属于 InlineHint / Wiki，不属于悬停提示",
			}
		)

	return {"hard": hard, "legacy": legacy, "tooltips": tooltips}


def scan_file(path: Path, root: Path) -> dict:
	relative = path.relative_to(root).as_posix()
	try:
		text = path.read_text(encoding="utf-8", errors="replace")
	except OSError as error:
		print(f"warning: 无法读取 {relative}: {error}", file=sys.stderr)
		return {}
	return scan_text(text)


def iter_scan_files(root: Path, scan_roots: tuple[str, ...]) -> list[Path]:
	files: list[Path] = []
	for entry in scan_roots:
		base = root / entry
		if not base.is_dir():
			continue
		for path in sorted(base.rglob("*")):
			if path.suffix in SCAN_SUFFIXES and path.is_file():
				files.append(path)
	return files


def collect(root: Path, scan_roots: tuple[str, ...]) -> dict:
	hard: list[dict] = []
	legacy_per_file: dict[str, list[dict]] = {}
	tooltip_per_file: dict[str, list[dict]] = {}
	for path in iter_scan_files(root, scan_roots):
		result = scan_file(path, root)
		if not result:
			continue
		relative = path.relative_to(root).as_posix()
		if result["hard"]:
			for hit in result["hard"]:
				hard.append({**hit, "file": relative})
		if result["legacy"]:
			legacy_per_file[relative] = result["legacy"]
		if result["tooltips"]:
			tooltip_per_file[relative] = result["tooltips"]
	return {
		"generated_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
		"root": str(root),
		"hard": hard,
		"legacy": legacy_per_file,
		"tooltips": tooltip_per_file,
		"legacy_total": sum(len(items) for items in legacy_per_file.values()),
		"tooltip_total": sum(len(items) for items in tooltip_per_file.values()),
	}


def load_baseline(path: Path) -> dict:
	if not path.is_file():
		return {"version": 1, "legacy_per_file": {}, "tooltips_per_file": {}}
	with path.open(encoding="utf-8") as handle:
		return json.load(handle)


def compare(report: dict, baseline: dict) -> list[str]:
	violations = [
		f"{hit['file']}:{hit['line']}  [{hit['rule']}] {hit['message']}"
		for hit in report["hard"]
	]
	for section, baseline_key in (("legacy", "legacy_per_file"), ("tooltips", "tooltips_per_file")):
		allowed = baseline.get(baseline_key, {})
		for file_path, hits in sorted(report[section].items()):
			limit = allowed.get(file_path, 0)
			if len(hits) > limit:
				violations.append(
					f"{file_path}: {section} 命中 {len(hits)} 处，基线 {limit} 处，新增 {len(hits) - limit} 处"
				)
	return violations


def summarize(report: dict) -> str:
	lines = [
		f"浮层语义审计：硬错误 {len(report['hard'])} 处；旧弹窗 {report['legacy_total']} 处 / {len(report['legacy'])} 文件；长 tooltip {report['tooltip_total']} 处 / {len(report['tooltips'])} 文件",
	]
	for hit in report["hard"]:
		lines.append(f"  HARD {hit['file']}:{hit['line']} [{hit['rule']}] {hit['message']}")
	for name, hits in sorted(report["legacy"].items(), key=lambda item: -len(item[1])):
		lines.append(f"  {len(hits):4d}  {name}  (旧弹窗)")
	for name, hits in sorted(report["tooltips"].items(), key=lambda item: -len(item[1])):
		lines.append(f"  {len(hits):4d}  {name}  (长 tooltip)")
	return "\n".join(lines)


def write_baseline(report: dict, path: Path, reason: str) -> None:
	payload = {
		"version": 1,
		"updated_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
		"reason": reason,
		"legacy_total": report["legacy_total"],
		"tooltip_total": report["tooltip_total"],
		"legacy_per_file": {name: len(hits) for name, hits in sorted(report["legacy"].items())},
		"tooltips_per_file": {name: len(hits) for name, hits in sorted(report["tooltips"].items())},
	}
	path.parent.mkdir(parents=True, exist_ok=True)
	with path.open("w", encoding="utf-8", newline="\n") as handle:
		json.dump(payload, handle, ensure_ascii=False, indent=1)
		handle.write("\n")


def main(argv: list[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description="审计浮层语义用法（Toast / Popconfirm / Alert / ConfirmDialog / Tooltip）")
	parser.add_argument("--root", default=str(REPO_ROOT), help="仓库根目录（测试用）")
	parser.add_argument("--check", action="store_true", help="对照基线判定回潮")
	parser.add_argument("--update-baseline", action="store_true", help="用当前结果重写基线")
	parser.add_argument("--reason", default="", help="重写基线的原因（--update-baseline 必填）")
	parser.add_argument("--report-json", default="", help="输出全量清单到指定路径")
	parser.add_argument("--baseline", default="", help="基线路径（测试用）")
	args = parser.parse_args(argv)

	root = Path(args.root).resolve()
	baseline_path = Path(args.baseline).resolve() if args.baseline else root / "qmclient_scripts" / "overlay_usage_baseline.json"
	report = collect(root, DEFAULT_SCAN_ROOTS)

	print(summarize(report))
	if args.report_json:
		output = Path(args.report_json)
		output.parent.mkdir(parents=True, exist_ok=True)
		output.write_text(json.dumps(report, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")

	if args.update_baseline:
		if not args.reason.strip():
			print("error: --update-baseline 需要 --reason 说明债务为何减少", file=sys.stderr)
			return 2
		write_baseline(report, baseline_path, args.reason.strip())
		print(f"基线已写入 {baseline_path}")
		return 0

	if args.check:
		violations = compare(report, load_baseline(baseline_path))
		if violations:
			print("")
			print("浮层语义回潮：")
			for violation in violations:
				print(f"  {violation}")
			return 1
		print("基线检查通过（无硬错误，旧弹窗与长 tooltip 未增加）")
		return 0

	return 1 if report["hard"] else 0


if __name__ == "__main__":
	sys.exit(main())
