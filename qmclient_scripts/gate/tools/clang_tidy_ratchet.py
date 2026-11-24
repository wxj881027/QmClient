#!/usr/bin/env python3
"""clang-tidy 诊断的防回潮闸门。

两种判据，按扫描范围自动选择：

- **增量**（PR 只改了 .cpp）：诊断必须落在本次 diff 的新增行上才算违规。PR 只对
  自己引入的东西负责，存量噪声和行号漂移都不参与判断。
- **全量**（改了头文件、构建配置，或 master 定时扫描）：与基线比较 `文件|检查`
  的计数，超出即失败。基线只降不升，债务真的减少时用 `--update-baseline` 收紧。

全量的计数会随「扫描了哪些 TU」变化，所以增量模式不能拿全量基线做减法——这也是
两种判据要分开的原因。
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_BASELINE = REPO_ROOT / "qmclient_scripts" / "gate" / "clang_tidy_baseline.json"

# `/abs/path/file.cpp:12:34: warning: message [check-name]`
DIAGNOSTIC_RE = re.compile(
	r"^(?P<path>.+?):(?P<line>\d+):(?P<column>\d+): warning: (?P<message>.*?)"
	r"(?: \[(?P<check>[A-Za-z0-9_.\-]+)\])?$"
)

# `@@ -1,2 +3,5 @@`，`+3,5` 表示新文件第 3 行起连续 5 行。
HUNK_RE = re.compile(r"^@@ -\d+(?:,\d+)? \+(?P<start>\d+)(?:,(?P<count>\d+))? @@")


def normalize_path(path: str, root: Path = REPO_ROOT) -> str:
	"""把 clang-tidy 输出的路径归一化成相对仓库根、`/` 分隔的形式。

	基线可能在 CI 上生成（`/home/runner/work/QmClient/QmClient/...`），与本机
	仓库根不同，所以除了前缀裁剪，还按仓库内的固定子目录兜底截断。
	"""
	normalized = path.replace("\\", "/").strip()
	root_text = str(root).replace("\\", "/").rstrip("/")
	if normalized.lower().startswith(root_text.lower() + "/"):
		return normalized[len(root_text) + 1 :]
	# 绝对路径但不在当前仓库根下（例如 CI 的工作目录）：按仓库内锚点截断。
	if normalized.startswith("/") or re.match(r"^[A-Za-z]:/", normalized):
		for anchor in ("/src/", "/qmclient_scripts/", "/cmake/", "/datasrc/", "/data/"):
			index = normalized.find(anchor)
			if index >= 0:
				return normalized[index + 1 :]
	# 相对路径（clang-tidy 用 -p 时可能给相对编译数据库目录的路径）保持原样。
	return normalized


def parse_diagnostics(text: str, root: Path = REPO_ROOT) -> list[dict]:
	"""从日志中提取每一条独立的诊断，按 (文件, 行, 列, 检查) 去重。

	同一个头文件会被多个 TU 各报一次，去重后才是「唯一位置」，与审计时用的口径一致。
	"""
	seen = set()
	diagnostics = []
	for line in text.splitlines():
		match = DIAGNOSTIC_RE.match(line)
		if match is None:
			continue
		path = normalize_path(match.group("path"), root)
		line_no = int(match.group("line"))
		column = int(match.group("column"))
		check = match.group("check") or "clang-diagnostic"
		key = (path, line_no, column, check)
		if key in seen:
			continue
		seen.add(key)
		diagnostics.append(
			{
				"path": path,
				"line": line_no,
				"column": column,
				"check": check,
				"message": match.group("message"),
			}
		)
	return diagnostics


def count_by_file_check(diagnostics: list[dict]) -> dict[str, int]:
	counts: dict[str, int] = {}
	for diagnostic in diagnostics:
		key = f"{diagnostic['path']}|{diagnostic['check']}"
		counts[key] = counts.get(key, 0) + 1
	return counts


def parse_added_lines(diff_text: str, root: Path = REPO_ROOT) -> dict[str, list[tuple[int, int]]]:
	"""从 `git diff -U0` 输出里取出每个文件的新增行区间（含单行区间）。"""
	added: dict[str, list[tuple[int, int]]] = {}
	current = None
	for line in diff_text.splitlines():
		if line.startswith("+++ "):
			path = line[4:].strip()
			if path == "/dev/null":
				current = None
				continue
			if path.startswith("b/"):
				path = path[2:]
			current = normalize_path(path, root)
			added.setdefault(current, [])
			continue
		if current is None:
			continue
		match = HUNK_RE.match(line)
		if match is None:
			continue
		start = int(match.group("start"))
		count = int(match.group("count") or "1")
		if count <= 0:
			# 纯删除的 hunk，没有新增行。
			continue
		added[current].append((start, start + count - 1))
	return {path: ranges for path, ranges in added.items() if ranges}


def filter_added_lines(
	diagnostics: list[dict], added: dict[str, list[tuple[int, int]]]
) -> list[dict]:
	"""只保留落在新增行上的诊断。"""
	hit = []
	for diagnostic in diagnostics:
		for start, end in added.get(diagnostic["path"], ()):
			if start <= diagnostic["line"] <= end:
				hit.append(diagnostic)
				break
	return hit


def load_baseline(path: Path) -> dict:
	if not path.is_file():
		return {"version": 1, "per_check_counts": {}, "total_hits": 0}
	return json.loads(path.read_text(encoding="utf-8"))


def compare_with_baseline(counts: dict[str, int], baseline: dict) -> list[dict]:
	"""返回超出基线的条目；基线里没有登记的一律算超标。"""
	baseline_counts = baseline.get("per_check_counts", {})
	violations = []
	for key, current in sorted(counts.items()):
		allowed = baseline_counts.get(key, 0)
		if current > allowed:
			violations.append({"key": key, "current": current, "baseline": allowed})
	return violations


def write_baseline(
	counts: dict[str, int], path: Path, reason: str, total_hits: int
) -> None:
	payload = {
		"version": 1,
		"reason": reason,
		"total_hits": total_hits,
		"per_check_counts": dict(sorted(counts.items())),
	}
	path.write_text(
		json.dumps(payload, ensure_ascii=False, indent=1) + "\n",
		encoding="utf-8",
		newline="\n",
	)


def changed_line_ranges(base: str, root: Path = REPO_ROOT) -> dict[str, list[tuple[int, int]]] | None:
	result = subprocess.run(
		["git", "diff", "--unified=0", "--diff-filter=ACMR", f"{base}...HEAD"],
		cwd=root,
		text=True,
		stdout=subprocess.PIPE,
		stderr=subprocess.PIPE,
		encoding="utf-8",
		errors="replace",
		check=False,
	)
	if result.returncode != 0:
		print(f"[clang-tidy] 无法取出 {base} 的改动行: {result.stderr.strip()}", flush=True)
		return None
	return parse_added_lines(result.stdout, root)


def format_diagnostic(diagnostic: dict) -> str:
	return f"  {diagnostic['path']}:{diagnostic['line']}:{diagnostic['column']} [{diagnostic['check']}] {diagnostic['message']}"


def report_json(path: Path, mode: str, diagnostics: list[dict], violations: list[dict]) -> None:
	payload = {
		"mode": mode,
		"diagnostics": diagnostics,
		"violations": violations,
	}
	path.write_text(
		json.dumps(payload, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n"
	)


def main(argv: list[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description="clang-tidy 防回潮检查")
	parser.add_argument("--log", required=True, type=Path, help="clang-tidy 构建日志")
	parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
	parser.add_argument("--diff-base", default="", help="给出基准则用「改动行」判据，否则与基线比计数")
	parser.add_argument("--update-baseline", action="store_true")
	parser.add_argument("--reason", default="")
	parser.add_argument("--report-json", type=Path)
	args = parser.parse_args(argv)

	text = args.log.read_text(encoding="utf-8", errors="replace")
	diagnostics = parse_diagnostics(text)
	counts = count_by_file_check(diagnostics)
	total = len(diagnostics)

	if args.update_baseline:
		if not args.reason:
			print("错误：--update-baseline 必须用 --reason 说明债务变化原因", file=sys.stderr)
			return 2
		if args.diff_base:
			print("错误：--update-baseline 只能在全量扫描结果上使用", file=sys.stderr)
			return 2
		write_baseline(counts, args.baseline, args.reason, total)
		print(f"基线已更新：{args.baseline}（{total} 个唯一位置 / {len(counts)} 个 文件|检查 组合）")
		return 0

	if args.diff_base:
		added = changed_line_ranges(args.diff_base)
		if added is None:
			print("[clang-tidy] 改动行不可用，本次跳过防回潮判断", flush=True)
			return 0
		hit = filter_added_lines(diagnostics, added)
		if args.report_json:
			report_json(args.report_json, "changed-lines", hit, [])
		if not hit:
			print(f"[clang-tidy] 防回潮通过：改动行上没有新诊断（本次扫描 {total} 个唯一位置）")
			return 0
		print(f"[clang-tidy] 防回潮失败：改动行上有 {len(hit)} 条诊断", flush=True)
		for diagnostic in hit:
			print(format_diagnostic(diagnostic), flush=True)
		return 1

	baseline = load_baseline(args.baseline)
	violations = compare_with_baseline(counts, baseline)
	if args.report_json:
		report_json(args.report_json, "baseline", diagnostics, violations)
	if not violations:
		print(
			f"[clang-tidy] 防回潮通过：{total} 个唯一位置，未超出基线"
			f"（基线 {baseline.get('total_hits', '?')} 个）"
		)
		return 0
	print(f"[clang-tidy] 防回潮失败：{len(violations)} 个 文件|检查 超出基线", flush=True)
	for violation in violations:
		print(
			f"  {violation['key']}: 当前 {violation['current']} > 基线 {violation['baseline']}",
			flush=True,
		)
	return 1


if __name__ == "__main__":
	sys.exit(main())
