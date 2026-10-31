#!/usr/bin/env python3
"""检查设置搜索索引与客户端卡片源码的缺口。

索引维护在 src/game/client/QmUi/cards/QmCardCatalogSearch.cpp：
每张卡一张 SCardSettingSearch 表，表内写着可检索的界面标签与配置名。
本脚本做两件事：

1. 找出索引里指向不存在源码文案的失效标签（索引漂移）；
2. 对能静态映射到渲染函数的卡片，列出卡片实际渲染、但没有进索引的标签。

默认只输出报告，不阻断；--strict 只在索引漂移或表/卡片登记不一致时失败。
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
INDEX_RELATIVE = Path("src/game/client/QmUi/cards/QmCardCatalogSearch.cpp")
CLIENT_SOURCE_RELATIVE = Path("src/game/client")

TABLE_RE = re.compile(
	r"static constexpr SCardSettingSearch (s_\w+)\[\] = \{(.*?)\n[\t ]*\};",
	re.DOTALL,
)
LABEL_RE = re.compile(r'Localizable\("((?:[^"\\]|\\.)*)"')
CONFIG_RE = re.compile(r'\{nullptr,\s*"([^"]+)"')
METADATA_RE = re.compile(r'\{"((?:qm|deck|tclient):[^"]+)",\s*(s_\w+)\s*,')
ADD_RE = re.compile(r'Add\(Id,\s*"((?:qm|deck|tclient):[^"]+)"')
MAKE_MODULE_RE = re.compile(r"MakeModuleCard\(")
RENDER_HOOK_RE = re.compile(r"QmCardRenderHook::(Render\w+)")
LOCALIZE_RE = re.compile(r'Localize\("((?:[^"\\]|\\.)*)"')
FUNCTION_RE = re.compile(r"CMenus::(\w+)\s*\(")


def _skip_string(text: str, index: int) -> int:
	"""index 指向开引号，返回闭引号之后的位置。"""
	index += 1
	while index < len(text):
		char = text[index]
		if char == "\\":
			index += 2
			continue
		if char == '"':
			return index + 1
		index += 1
	return index


def _skip_char_literal(text: str, index: int) -> int:
	index += 1
	while index < len(text):
		char = text[index]
		if char == "\\":
			index += 2
			continue
		if char == "'":
			return index + 1
		index += 1
	return index


def _skip_comment(text: str, index: int) -> int | None:
	if text.startswith("//", index):
		end = text.find("\n", index)
		return len(text) if end < 0 else end + 1
	if text.startswith("/*", index):
		end = text.find("*/", index + 2)
		return len(text) if end < 0 else end + 2
	return None


def find_matching(text: str, open_index: int) -> int:
	"""返回与 text[open_index] 配对的右括号位置；找不到返回 -1。"""
	pairs = {"(": ")", "{": "}", "[": "]"}
	opener = text[open_index]
	closer = pairs.get(opener)
	if closer is None:
		return -1
	depth = 0
	index = open_index
	while index < len(text):
		char = text[index]
		if char == '"':
			index = _skip_string(text, index)
			continue
		if char == "'":
			index = _skip_char_literal(text, index)
			continue
		skipped = _skip_comment(text, index)
		if skipped is not None:
			index = skipped
			continue
		if char == opener:
			depth += 1
		elif char == closer:
			depth -= 1
			if depth == 0:
				return index
		index += 1
	return -1


def parse_tables(text: str) -> dict[str, dict[str, set[str]]]:
	tables: dict[str, dict[str, set[str]]] = {}
	for match in TABLE_RE.finditer(text):
		body = match.group(2)
		tables[match.group(1)] = {
			"labels": set(LABEL_RE.findall(body)),
			"configs": set(CONFIG_RE.findall(body)),
		}
	return tables


def parse_metadata(text: str) -> dict[str, str]:
	return {match.group(1): match.group(2) for match in METADATA_RE.finditer(text)}


def parse_add_mappings(text: str) -> dict[str, str]:
	"""stableId -> Render 函数名（仅 Add(...) 形式的卡片模块）。"""
	mappings: dict[str, str] = {}
	for match in ADD_RE.finditer(text):
		open_index = text.find("(", match.start())
		if open_index < 0:
			continue
		close_index = find_matching(text, open_index)
		if close_index < 0:
			continue
		render = RENDER_HOOK_RE.search(text[open_index:close_index])
		if render is not None:
			mappings[match.group(1)] = render.group(1)
	return mappings


def parse_make_module_mappings(text: str) -> dict[str, str]:
	"""stableId -> Render 函数名（MakeModuleCard 形式）。"""
	mappings: dict[str, str] = {}
	for match in MAKE_MODULE_RE.finditer(text):
		open_paren = text.find("(", match.start())
		if open_paren < 0:
			continue
		close_paren = find_matching(text, open_paren)
		if close_paren < 0:
			continue
		call = text[match.start() : close_paren + 1]
		stable_id = re.search(r'"((?:qm|deck|tclient):[^"]+)"', call)
		render = RENDER_HOOK_RE.search(call)
		if stable_id is not None and render is not None:
			mappings[stable_id.group(1)] = render.group(1)
	return mappings


def extract_function_body(text: str, function_name: str) -> str | None:
	match = re.search(r"CMenus::" + re.escape(function_name) + r"\s*\(", text)
	if match is None:
		return None
	open_paren = text.find("(", match.start())
	close_paren = find_matching(text, open_paren)
	if close_paren < 0:
		return None
	open_brace = text.find("{", close_paren)
	if open_brace < 0:
		return None
	close_brace = find_matching(text, open_brace)
	if close_brace < 0:
		return None
	return text[open_brace : close_brace + 1]


def strip_comments(text: str) -> str:
	"""移除 // 与 /* */ 注释，保留字符串内容。"""
	output: list[str] = []
	index = 0
	while index < len(text):
		char = text[index]
		if char == '"':
			end = _skip_string(text, index)
			output.append(text[index:end])
			index = end
			continue
		if char == "'":
			end = _skip_char_literal(text, index)
			output.append(text[index:end])
			index = end
			continue
		skipped = _skip_comment(text, index)
		if skipped is not None:
			index = skipped
			continue
		output.append(char)
		index += 1
	return "".join(output)


def extract_localize_labels(text: str) -> set[str]:
	return set(LOCALIZE_RE.findall(strip_comments(text)))


def read_client_sources(root: Path) -> dict[Path, str]:
	sources: dict[Path, str] = {}
	for path in (root / CLIENT_SOURCE_RELATIVE).rglob("*"):
		if path.suffix not in {".cpp", ".h"}:
			continue
		if path == root / INDEX_RELATIVE:
			continue
		sources[path] = path.read_text(encoding="utf-8", errors="replace")
	return sources


def analyze(root: Path) -> dict:
	index_text = (root / INDEX_RELATIVE).read_text(encoding="utf-8")
	tables = parse_tables(index_text)
	metadata = parse_metadata(index_text)

	client_sources = read_client_sources(root)
	corpus = "\n".join(client_sources.values()).lower()

	tables_by_card = {stable_id: table for stable_id, table in metadata.items() if table in tables}
	registered_tables = set(tables_by_card.values())

	drifted = []
	for table_name, table in sorted(tables.items()):
		for label in sorted(table["labels"]):
			if label.lower() not in corpus:
				drifted.append({"table": table_name, "label": label})

	add_mappings: dict[str, tuple[str, str]] = {}
	for path, text in client_sources.items():
		for stable_id, function_name in parse_add_mappings(text).items():
			add_mappings.setdefault(stable_id, (str(path.relative_to(root)), function_name))
		for stable_id, function_name in parse_make_module_mappings(text).items():
			add_mappings.setdefault(stable_id, (str(path.relative_to(root)), function_name))

	cards = []
	unmapped = []
	for stable_id, table_name in sorted(tables_by_card.items()):
		mapping = add_mappings.get(stable_id)
		if mapping is None:
			unmapped.append(stable_id)
			continue
		source_path, function_name = mapping
		body = extract_function_body(client_sources[root / source_path], function_name)
		source_labels = extract_localize_labels(body) if body is not None else set()
		missing = sorted(source_labels - tables[table_name]["labels"])
		cards.append({
			"stable_id": stable_id,
			"function": function_name,
			"source": source_path,
			"source_labels": len(source_labels),
			"indexed_labels": len(tables[table_name]["labels"]),
			"missing": missing,
		})

	metadata_tables = set(tables_by_card.values())
	return {
		"tables": len(tables),
		"registered_cards": len(metadata),
		"unregistered_tables": sorted(set(tables) - registered_tables),
		"missing_tables": sorted(stable_id for stable_id, table in metadata.items() if table not in tables),
		"index_labels": sum(len(table["labels"]) for table in tables.values()),
		"drifted_labels": drifted,
		"mapped_cards": len(cards),
		"unmapped_cards": unmapped,
		"cards": cards,
		"missing_total": sum(len(card["missing"]) for card in cards),
		"metadata_tables": len(metadata_tables),
	}


def render_markdown(report: dict) -> str:
	lines = [
		"# 设置搜索索引缺口",
		"",
		f"- 索引表：{report['tables']}，登记卡：{report['registered_cards']}",
		f"- 索引标签：{report['index_labels']}",
		f"- 可映射渲染函数的卡：{report['mapped_cards']}，未映射：{len(report['unmapped_cards'])}",
		f"- 索引标签在源码中失配：{len(report['drifted_labels'])}",
		f"- 已映射卡未入索引的标签合计：{report['missing_total']}",
		"",
	]
	if report["drifted_labels"]:
		lines.extend(["## 失效索引标签", "", "| 表 | 标签 |", "| --- | --- |"])
		for item in report["drifted_labels"]:
			lines.append(f"| {item['table']} | {item['label']} |")
		lines.append("")
	rows = [card for card in report["cards"] if card["missing"]]
	if rows:
		lines.extend([
			"## 有缺口的卡片",
			"",
			"| 卡片 | 渲染函数 | 源码标签 | 已入索引 | 缺口 | 示例 |",
			"| --- | --- | --- | --- | --- | --- |",
		])
		for card in sorted(rows, key=lambda item: len(item["missing"]), reverse=True):
			examples = "；".join(card["missing"][:4])
			lines.append(f"| {card['stable_id']} | {card['function']} | {card['source_labels']} | {card['indexed_labels']} | {len(card['missing'])} | {examples} |")
		lines.append("")
	if report["unmapped_cards"]:
		lines.extend([
			"## 未映射的卡片",
			"",
			"这些卡没有 Add(...) 形式的渲染函数映射（标准卡/预览卡），需要人工核对：",
			"",
			", ".join(report["unmapped_cards"]),
			"",
		])
	return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description="检查设置搜索索引缺口")
	parser.add_argument("--root", type=Path, default=REPO_ROOT, help="仓库根目录")
	parser.add_argument("--format", choices=["markdown", "json"], default="markdown")
	parser.add_argument("--json", type=Path, help="同时把 JSON 报告写到该路径")
	parser.add_argument("--strict", action="store_true", help="索引漂移或表登记不一致时失败")
	args = parser.parse_args(argv)

	report = analyze(args.root)
	if args.format == "json":
		print(json.dumps(report, ensure_ascii=False, indent=2))
	else:
		print(render_markdown(report), end="")
	if args.json is not None:
		args.json.parent.mkdir(parents=True, exist_ok=True)
		args.json.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

	if args.strict:
		problems = len(report["drifted_labels"]) + len(report["unregistered_tables"]) + len(report["missing_tables"])
		if problems:
			print(f"索引结构检查失败：{problems} 项", file=sys.stderr)
			return 1
	return 0


if __name__ == "__main__":
	sys.exit(main())
