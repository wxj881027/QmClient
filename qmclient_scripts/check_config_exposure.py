# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
"""配置暴露审计：配置声明与设置页入口的差集。

规则：
  - 审计对象是 src/engine/shared/config_variables*.h 里带 CFGFLAG_CLIENT 的 MACRO_CONFIG_*。
  - 「已暴露」= 配置代码名出现在设置页实现代码（QmUi 卡片、menus_settings*、qmclient/tclient 菜单）。
  - qm_ 前缀是 QmClient 自有配置，未暴露时必须登记在 config_exposure_baseline.json：
      internal     —— 代码内部状态（迁移标记、统计计数等），不需要入口；
      pending-ui   —— 应当补设置页入口，属于待办债务；
      console-only —— 只有控制台/配置文件入口、刻意不做设置页的配置（例如需要正则校验的输入）。
      in-context   —— 已有就地入口（取色器/控制台/计分板内部按钮），设置页不再重复。
  - 上游配置（cl_/br_/ed_ 等）只做盘点，不阻断。
  - 基线里已接线的条目、以及指向已删除配置的条目都会判为过期，避免基线腐化成假清单。

用法：
  python qmclient_scripts/check_config_exposure.py
  python qmclient_scripts/check_config_exposure.py --list
  python qmclient_scripts/check_config_exposure.py --list-internal
  python qmclient_scripts/check_config_exposure.py --list-upstream
  python qmclient_scripts/check_config_exposure.py --write-baseline
退出码：0 通过；1 存在未登记的 qm_ 配置或基线过期。
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

CONFIG_HEADERS = (
	"src/engine/shared/config_variables.h",
	"src/engine/shared/config_variables_tclient.h",
	"src/engine/shared/config_variables_qmclient.h",
)

# 设置页入口的实现面。QmUi 只看 .cpp（头部多是布局/渲染逻辑）；qmclient、tclient 的
# 设置页有大量行代码写在内部头里，两个扩展都算。
EXPOSURE_GLOBS = (
	"src/game/client/QmUi/**/*.cpp",
	"src/game/client/components/menus*.cpp",
	"src/game/client/components/qmclient/**/*.cpp",
	"src/game/client/components/qmclient/**/*.h",
	"src/game/client/components/tclient/**/*.cpp",
	"src/game/client/components/tclient/**/*.h",
	# 页内编辑器也是真实入口：HUD 编辑器的输入行、地图编辑器的弹层按钮。
	"src/game/client/components/hud_editor*.cpp",
	"src/game/editor/popups.cpp",
)

BASELINE_RELPATH = "qmclient_scripts/config_exposure_baseline.json"
BASELINE_VERSION = 1
REQUIRES_EXPOSURE_PREFIXES = ("qm_",)
CATEGORY_INTERNAL = "internal"
CATEGORY_PENDING_UI = "pending-ui"
CATEGORY_CONSOLE_ONLY = "console-only"
CATEGORY_IN_CONTEXT = "in-context"
BASELINE_CATEGORIES = (CATEGORY_INTERNAL, CATEGORY_PENDING_UI, CATEGORY_CONSOLE_ONLY, CATEGORY_IN_CONTEXT)
# 一眼可判的代码内部状态：迁移标记、布局/配置版本号、统计计数、渲染管线开关。
INTERNAL_NAME_PATTERNS = (
	re.compile(r"_migrated$"),
	re.compile(r"_version$"),
	re.compile(r"^qm_launch_count$"),
	re.compile(r"^qm_enhanced_procedural_ring$"),
)

DECL_START = re.compile(r"^\s*MACRO_CONFIG_(INT|FLOAT|COLOR|STR)\s*\(")
DECL_SHAPE = re.compile(r"^MACRO_CONFIG_(INT|FLOAT|COLOR|STR)\s*\((.*)\)\s*;?\s*$")


@dataclass(frozen=True)
class SConfigVar:
	header: str
	kind: str
	code: str
	name: str
	flags: str

	@property
	def client_facing(self) -> bool:
		return "CFGFLAG_CLIENT" in self.flags

	@property
	def requires_exposure(self) -> bool:
		return self.name.startswith(REQUIRES_EXPOSURE_PREFIXES)


@dataclass(frozen=True)
class SBaselineEntry:
	category: str
	note: str


def _join_macro_statements(text: str) -> list[str]:
	"""把跨行的 MACRO_CONFIG_* 调用拼成单条语句。"""
	statements: list[str] = []
	buffer = ""
	depth = 0
	for raw_line in text.splitlines():
		segment = raw_line.strip()
		if not buffer:
			if not DECL_START.match(raw_line):
				continue
			buffer = segment
			depth = 0
		else:
			buffer += " " + segment
		depth += segment.count("(") - segment.count(")")
		if depth <= 0:
			statements.append(buffer)
			buffer = ""
			depth = 0
	if buffer:
		statements.append(buffer)
	return statements


def _split_macro_args(body: str) -> list[str]:
	args: list[str] = []
	buffer = ""
	depth = 0
	for char in body:
		if char == "(":
			depth += 1
		elif char == ")":
			depth -= 1
		if char == "," and depth == 0:
			args.append(buffer.strip())
			buffer = ""
			continue
		buffer += char
	args.append(buffer.strip())
	return args


def parse_config_header(path: Path) -> list[SConfigVar]:
	"""解析一个配置头文件里的 MACRO_CONFIG_* 声明。"""
	variables: list[SConfigVar] = []
	for statement in _join_macro_statements(path.read_text(encoding="utf-8", errors="replace")):
		match = DECL_SHAPE.match(statement)
		if match is None:
			continue
		kind, body = match.groups()
		args = _split_macro_args(body)
		if len(args) < 3:
			continue
		code = args[0]
		name = args[1]
		# flags 定位到实际参数，避免描述文本里出现 CFGFLAG_* 字样时误判为客户端配置。
		flags = next((arg for arg in args[2:] if "CFGFLAG" in arg), "")
		variables.append(SConfigVar(path.name, kind, code, name, flags))
	return variables


def load_config_variables(root: Path = REPO_ROOT) -> list[SConfigVar]:
	variables: list[SConfigVar] = []
	for relpath in CONFIG_HEADERS:
		variables.extend(parse_config_header(root / relpath))
	return variables


def collect_exposure_text(root: Path = REPO_ROOT) -> str:
	"""拼接设置页实现代码，用于判断配置是否有入口。"""
	chunks: list[str] = []
	seen: set[Path] = set()
	for pattern in EXPOSURE_GLOBS:
		for path in sorted(root.glob(pattern)):
			if path in seen or not path.is_file():
				continue
			seen.add(path)
			chunks.append(path.read_text(encoding="utf-8", errors="replace"))
	return "\n".join(chunks)


def _exposure_pattern(code: str) -> re.Pattern[str]:
	# 词边界避免 m_QmFoo 命中 m_QmFooBackup。
	return re.compile(r"\bm_" + re.escape(code) + r"\b")


def find_exposed_codes(variables: list[SConfigVar], exposure_text: str) -> set[str]:
	return {var.code for var in variables if _exposure_pattern(var.code).search(exposure_text)}


def default_category(name: str) -> str:
	for pattern in INTERNAL_NAME_PATTERNS:
		if pattern.search(name):
			return CATEGORY_INTERNAL
	return CATEGORY_PENDING_UI


def load_baseline(path: Path = REPO_ROOT / BASELINE_RELPATH) -> dict[str, SBaselineEntry]:
	if not path.is_file():
		return {}
	raw = json.loads(path.read_text(encoding="utf-8"))
	entries: dict[str, SBaselineEntry] = {}
	for item in raw.get("entries", []):
		name = str(item.get("name", "")).strip()
		category = str(item.get("category", "")).strip()
		if not name or category not in BASELINE_CATEGORIES:
			continue
		entries[name] = SBaselineEntry(category, str(item.get("note", "")).strip())
	return entries


def dump_baseline(entries: dict[str, SBaselineEntry], path: Path = REPO_ROOT / BASELINE_RELPATH) -> None:
	payload = {
		"version": BASELINE_VERSION,
		"entries": [
			{"name": name, "category": entry.category, "note": entry.note}
			for name, entry in sorted(entries.items())
		],
	}
	path.write_text(json.dumps(payload, ensure_ascii=False, indent="\t") + "\n", encoding="utf-8")


@dataclass(frozen=True)
class SAudit:
	unexposed_owned: list[SConfigVar]
	unexposed_upstream: list[SConfigVar]
	unregistered: list[SConfigVar]
	stale: list[str]
	baseline: dict[str, SBaselineEntry]


def audit(variables: list[SConfigVar], exposure_text: str, baseline: dict[str, SBaselineEntry]) -> SAudit:
	exposed = find_exposed_codes(variables, exposure_text)
	client_vars = [var for var in variables if var.client_facing]
	unexposed = [var for var in client_vars if var.code not in exposed]
	unexposed.sort(key=lambda var: var.name)

	owned = [var for var in unexposed if var.requires_exposure]
	upstream = [var for var in unexposed if not var.requires_exposure]
	unregistered = [var for var in owned if var.name not in baseline]

	declared_owned = {var.name for var in client_vars if var.requires_exposure}
	owned_names = {var.name for var in owned}
	stale: list[str] = []
	for name in baseline:
		if name not in declared_owned:
			stale.append(f"{name}（配置已不存在）")
		elif name not in owned_names:
			# console-only 的配置会在设置页代码里被读取（控制台链、初始化），
			# 所以「看起来已接线」是预期结果，不算基线过期。
			if baseline[name].category != CATEGORY_CONSOLE_ONLY:
				stale.append(f"{name}（已接线，请从基线移除）")
	return SAudit(owned, upstream, unregistered, sorted(stale), baseline)


def _category_of(audit_result: SAudit, name: str) -> str:
	entry = audit_result.baseline.get(name)
	return entry.category if entry is not None else ""


def _print_owned(audit_result: SAudit, list_internal: bool) -> None:
	pending = [var for var in audit_result.unexposed_owned if _category_of(audit_result, var.name) == CATEGORY_PENDING_UI]
	internal = [var for var in audit_result.unexposed_owned if _category_of(audit_result, var.name) == CATEGORY_INTERNAL]
	in_context = [var for var in audit_result.unexposed_owned if _category_of(audit_result, var.name) == CATEGORY_IN_CONTEXT]
	unregistered = audit_result.unregistered

	print(f"Qm 自有配置未接线：{len(audit_result.unexposed_owned)} 项（待补入口 {len(pending)} · 内部状态 {len(internal)} · 就地入口 {len(in_context)} · 未登记 {len(unregistered)}）")
	if pending:
		print("\n待补设置页入口（pending-ui）：")
		for var in pending:
			note = audit_result.baseline[var.name].note
			print(f"  {var.name:<48} {var.header:<32} {note}".rstrip())
	if unregistered:
		print("\n未登记（需接线或写入基线）：")
		for var in unregistered:
			print(f"  {var.name:<48} {var.header}")
	if list_internal and internal:
		print("\n内部状态（internal，不需要入口）：")
		for var in internal:
			note = audit_result.baseline[var.name].note
			print(f"  {var.name:<48} {var.header:<32} {note}".rstrip())
	if list_internal:
		if in_context:
			print("\n已有就地入口（in-context，设置页不再重复）：")
			for var in in_context:
				note = audit_result.baseline[var.name].note
				print(f"  {var.name:<48} {var.header:<32} {note}".rstrip())
		console_only = sorted(name for name, entry in audit_result.baseline.items() if entry.category == CATEGORY_CONSOLE_ONLY)
		if console_only:
			print("\n仅控制台入口（console-only，刻意不做设置页）：")
			for name in console_only:
				print(f"  {name:<48} {audit_result.baseline[name].note}".rstrip())


def _print_upstream(audit_result: SAudit) -> None:
	print(f"\n上游配置无设置页入口（仅盘点，不阻断）：{len(audit_result.unexposed_upstream)} 项")
	for var in audit_result.unexposed_upstream:
		print(f"  {var.name:<48} {var.header}")


def main(argv: list[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description="配置暴露审计")
	parser.add_argument("--list", action="store_true", help="列出 Qm 自有未接线配置与未登记项")
	parser.add_argument("--list-internal", action="store_true", help="同时列出内部状态与已有就地入口的配置")
	parser.add_argument("--list-upstream", action="store_true", help="列出无设置页入口的上游配置（仅盘点）")
	parser.add_argument("--write-baseline", action="store_true", help="把当前未接线配置写入基线（保留已有分类）")
	args = parser.parse_args(argv)

	variables = load_config_variables()
	baseline = load_baseline()
	audit_result = audit(variables, collect_exposure_text(), baseline)

	if args.write_baseline:
		entries: dict[str, SBaselineEntry] = {}
		for var in audit_result.unexposed_owned:
			existing = baseline.get(var.name)
			if existing is not None:
				entries[var.name] = existing
			else:
				entries[var.name] = SBaselineEntry(default_category(var.name), "新登记，待人工确认分类")
		dump_baseline(entries)
		print(f"已写入基线：{BASELINE_RELPATH}（{len(entries)} 项）")
		print("请复核 internal/pending-ui/console-only/in-context 分类与 note，再提交。")
		return 0

	if args.list or args.list_internal or args.list_upstream:
		_print_owned(audit_result, args.list_internal)
		if args.list_upstream:
			_print_upstream(audit_result)
		return 0 if not audit_result.unregistered and not audit_result.stale else 1

	if audit_result.unregistered or audit_result.stale:
		print("配置暴露审计未通过：")
		for var in audit_result.unregistered:
			print(f"  [未登记] {var.name:<48} {var.header} —— 请补设置页入口，或写入 {BASELINE_RELPATH}")
		for item in audit_result.stale:
			print(f"  [基线过期] {item}")
		return 1

	print(f"配置暴露审计通过：Qm 自有配置 {len([var for var in variables if var.requires_exposure and var.client_facing])} 项，未接线 {len(audit_result.unexposed_owned)} 项均已登记")
	return 0


if __name__ == "__main__":
	sys.exit(main())
