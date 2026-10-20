#!/usr/bin/env python3
"""审计 QmClient UI 层绕过 ui_token 的裸颜色字面量。

「安卓感」治理的第一道防线：QmUi 与 qmclient 组件的所有绘制代码应引用
UiTokens.h 的语义 token，而不是就地写死 ColorRGBA 数字字面量。

模式：
  报告模式（默认）  打印汇总，可配合 --report-json 输出全量清单。
  --check           对照基线做防回潮判定：任一文件命中数超过基线即失败，
                    新文件默认基线为 0；行号漂移不影响判定（按文件计数）。
  --update-baseline 用当前扫描结果重写基线，仅在债务真实减少时使用，
                    需配合 --reason 记录原因。

范围刻意排除 UiTokens.h（token 定义源）与行尾注释；游戏语义色
（队伍/稀有度/特效渐变等）由 UI_TOKEN_COLOR_ALLOW 文件级白名单显式豁免。
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

# 审计范围：QmClient 自有 UI 基建与组件层。tclient/ 与 DDNet 核心 UI
# 文件（ui.cpp/ui_popups.cpp 等）不在此轮治理范围。
SCAN_ROOTS = [
	"src/game/client/QmUi",
	"src/game/client/components/qmclient",
]

SCAN_SUFFIXES = (".h", ".cpp")

# token 定义源本身豁免；其余文件级豁免必须在此登记并注明理由。
# 格式：路径 -> 豁免理由（显示在报告里，防止无感漂移）。
FILE_ALLOWLIST = {
	"src/game/client/QmUi/UiTokens.h": "ui_token 唯一定义源",
	# 例：特效渐变需要连续 HSV 数学，不适用离散语义色（示例条目，实际豁免时填写）。
}

# 裸字面量构造：实参全部是数字字面量（允许 f 后缀与负号）。变量、
# token 引用、color_cast 结果都不命中。
LITERAL_COLOR = re.compile(
	r"\bColorRGBA\s*\(\s*(-?\d+(?:\.\d+)?f?)\s*,"
	r"\s*(-?\d+(?:\.\d+)?f?)\s*,"
	r"\s*(-?\d+(?:\.\d+)?f?)\s*,"
	r"\s*(-?\d+(?:\.\d+)?f?)\s*\)"
)


def iter_scan_files() -> list[Path]:
	files: list[Path] = []
	for root in SCAN_ROOTS:
		root_path = REPO_ROOT / root
		if not root_path.is_dir():
			continue
		for path in sorted(root_path.rglob("*")):
			if path.suffix in SCAN_SUFFIXES and path.is_file():
				files.append(path)
	return files


def strip_line_comment(line: str) -> str:
	# 仅剔除行尾 // 注释；字符串内的 // 在本扫描口径下几乎不存在，
	# 且剔除过度只会漏报（保守方向正确）。
	index = line.find("//")
	return line if index < 0 else line[:index]


def scan_file(path: Path) -> list[dict]:
	hits: list[dict] = []
	relative = path.relative_to(REPO_ROOT).as_posix()
	try:
		text = path.read_text(encoding="utf-8", errors="replace")
	except OSError as error:
		print(f"warning: 无法读取 {relative}: {error}", file=sys.stderr)
		return hits
	for line_number, line in enumerate(text.splitlines(), start=1):
		code = strip_line_comment(line)
		for match in LITERAL_COLOR.finditer(code):
			values = [float(value.rstrip("f")) for value in match.groups()]
			hits.append(
				{
					"file": relative,
					"line": line_number,
					"rgba": values,
					"snippet": code.strip()[:160],
				}
			)
	return hits


def collect() -> dict:
	files: dict[str, list[dict]] = {}
	allowed: dict[str, list[dict]] = {}
	total = 0
	for path in iter_scan_files():
		relative = path.relative_to(REPO_ROOT).as_posix()
		hits = scan_file(path)
		if not hits:
			continue
		if relative in FILE_ALLOWLIST:
			allowed[relative] = hits
		else:
			files[relative] = hits
			total += len(hits)
	return {
		"generated_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
		"total_hits": total,
		"hit_files": len(files),
		"allowlisted_files": len(allowed),
		"files": files,
		"allowlist": allowed,
		"file_allowlist_reasons": dict(FILE_ALLOWLIST),
	}


def load_baseline(path: Path) -> dict:
	if not path.is_file():
		return {"version": 1, "per_file_counts": {}}
	with path.open(encoding="utf-8") as handle:
		return json.load(handle)


def check_against_baseline(report: dict, baseline: dict) -> list[str]:
	per_file = baseline.get("per_file_counts", {})
	violations: list[str] = []
	for file_path, hits in sorted(report["files"].items()):
		count = len(hits)
		allowed_count = per_file.get(file_path, 0)
		if count > allowed_count:
			extra = count - allowed_count
			violations.append(
				f"{file_path}: 命中 {count} 处，基线 {allowed_count} 处，新增 {extra} 处"
			)
	return violations


def write_baseline(report: dict, path: Path, reason: str) -> None:
	payload = {
		"version": 1,
		"updated_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
		"reason": reason,
		"total_hits": report["total_hits"],
		"per_file_counts": {name: len(hits) for name, hits in sorted(report["files"].items())},
	}
	path.parent.mkdir(parents=True, exist_ok=True)
	with path.open("w", encoding="utf-8", newline="\n") as handle:
		json.dump(payload, handle, ensure_ascii=False, indent=1)
		handle.write("\n")


def summarize(report: dict) -> str:
	lines = [
		f"裸颜色字面量：{report['total_hits']} 处 / {report['hit_files']} 个文件"
		f"（豁免文件 {report['allowlisted_files']} 个）",
	]
	for name, hits in sorted(report["files"].items(), key=lambda item: -len(item[1])):
		lines.append(f"  {len(hits):4d}  {name}")
	if report["allowlist"]:
		lines.append("白名单文件：")
		for name in sorted(report["allowlist"]):
			lines.append(f"  {len(report['allowlist'][name]):4d}  {name}  [{report['file_allowlist_reasons'].get(name, '')}]")
	return "\n".join(lines)


def git_tracked_relative(file_path: Path) -> str:
	try:
		output = subprocess.run(
			["git", "ls-files", "--", file_path.relative_to(REPO_ROOT).as_posix()],
			cwd=REPO_ROOT,
			capture_output=True,
			text=True,
			check=True,
		)
		return output.stdout.strip()
	except (OSError, subprocess.CalledProcessError):
		return ""


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--report-json", type=Path, help="输出全量命中清单 JSON 到指定路径")
	parser.add_argument("--check", action="store_true", help="对照基线做防回潮判定，回潮时退出码 1")
	parser.add_argument("--update-baseline", action="store_true", help="用当前结果重写基线（仅债务减少时使用）")
	parser.add_argument("--reason", default="", help="更新基线时的原因记录")
	parser.add_argument(
		"--baseline",
		type=Path,
		default=REPO_ROOT / "qmclient_scripts" / "gate" / "ui_color_token_baseline.json",
		help="基线文件路径",
	)
	args = parser.parse_args()

	report = collect()

	if args.report_json:
		args.report_json.parent.mkdir(parents=True, exist_ok=True)
		with args.report_json.open("w", encoding="utf-8", newline="\n") as handle:
			json.dump(report, handle, ensure_ascii=False, indent=1)
			handle.write("\n")

	if args.update_baseline:
		if not args.reason:
			print("错误：--update-baseline 必须配合 --reason 说明债务变化原因", file=sys.stderr)
			return 2
		write_baseline(report, args.baseline, args.reason)
		print(f"基线已更新：{args.baseline}（共 {report['total_hits']} 处）")
		return 0

	if args.check:
		baseline = load_baseline(args.baseline)
		violations = check_against_baseline(report, baseline)
		if violations:
			print("防回潮检查失败：以下文件裸颜色字面量超出基线（不得新增，应改用 ui_token）：")
			for item in violations:
				print(f"  {item}")
			return 1
		print(f"防回潮通过：无文件超出基线（当前存量 {report['total_hits']} 处，基线 {baseline.get('total_hits', '?')} 处）")
		return 0

	print(summarize(report))
	if args.report_json is None:
		print("\n提示：--report-json 输出全量清单；--check 防回潮；--update-baseline 减债后收基线。")
	return 0


if __name__ == "__main__":
	sys.exit(main())
