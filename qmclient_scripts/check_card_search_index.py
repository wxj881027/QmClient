#!/usr/bin/env python3
"""检查设置搜索索引与客户端卡片源码的缺口。

索引维护在 src/game/client/QmUi/cards/QmCardCatalogSearch.cpp：
每张卡一张 SCardSettingSearch 表，表内写着可检索的界面标签与配置名。
本脚本做三件事：

1. 找出索引里指向不存在源码文案的失效标签（索引漂移）；
2. 按可映射程度收集每张卡实际渲染的 Localize 文案，列出没有进索引的标签；
3. 把共享取色弹窗、运行期状态等不属于具体卡片的文案列为例外。

默认只报告；--strict 只在索引漂移或表/卡片登记不一致时失败。
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

try:
	from card_search_action_labels import ACTION_BUTTON_LABELS
except ImportError:
	from qmclient_scripts.card_search_action_labels import ACTION_BUTTON_LABELS

REPO_ROOT = Path(__file__).resolve().parents[1]
INDEX_RELATIVE = Path("src/game/client/QmUi/cards/QmCardCatalogSearch.cpp")
CLIENT_SOURCE_RELATIVE = Path("src/game/client")
CARD_SOURCE_RELATIVE = Path("src/game/client/QmUi/cards")

TABLE_RE = re.compile(
	r"static constexpr SCardSettingSearch (s_\w+)\[\] = \{(.*?)\n[\t ]*\};",
	re.DOTALL,
)
LABEL_RE = re.compile(r'Localizable\("((?:[^"\\]|\\.)*)"')
CONFIG_RE = re.compile(r'\{nullptr,\s*"([^"]+)"')
METADATA_RE = re.compile(r'\{"((?:qm|deck|tclient):[^"]+)",\s*(s_\w+)\s*,')
ADD_RE = re.compile(r'Add\(Id,\s*"((?:qm|deck|tclient):[^"]+)"')
MAKE_MODULE_RE = re.compile(r"MakeModuleCard\(")
RENDER_HOOK_RE = re.compile(r"(?:QmCardRenderHook::)?(Render\w+)")
LOCALIZE_RE = re.compile(r'Localize\("((?:[^"\\]|\\.)*)"')
FUNCTION_RE = re.compile(r"CMenus::(\w+)\s*\(")

# 不属于具体设置卡片的文案：共享取色弹窗、运行期状态、卡片标题与预览内容。
IGNORED_SOURCE_LABELS = {
	"Edit color",
	"Gradient settings",
	"Gradient type",
	"Linear gradient",
	"Radial gradient",
	"Angular gradient",
	"Reflected gradient",
	"Diamond gradient",
	"Radial gradients spread from the center",
	"Gradient direction",
	"Gradient horizontal position",
	"Gradient vertical position",
	"Gradient range",
	"Reverse gradient colors",
	"Loading…",
	"Preview unavailable",
	"Preview loading…",
	"Unavailable",
	"Console settings",
	"Info",
}

# 只用于搜索副标题、没有独立 Localize 渲染点的卡片标签（迁移前也只存在于集中表）。
# 没有设置行、因此不需要搜索表的默认卡片（预览/输入框/信息页）。
CARDS_WITHOUT_SEARCH_TABLE = {
	"deck:credits-friend-links",
	"deck:global-search-input",
	"deck:global-search-results",
	"deck:qmclient-contributors-community",
	"deck:qmclient-contributors-ddnet",
	"deck:qmclient-contributors-sponsors",
	"deck:tclient-bind-wheel-preview",
	"deck:tclient-info-developers",
	"qm:info",
	"qm:laser",
	"qm:nameplate_text",
	"tclient:visual-font-cursor",
}

SEARCH_ONLY_LABELS = {
	"Anti Latency Tools",
	"Background Draw",
	"Finish Name",
	"Tee Trails",
}


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


DECLARATION_RE = re.compile(r'QM_CARD_SEARCH_TABLE\(\s*"((?:qm|deck|tclient):[^"]+)"\s*,\s*(\w+)\s*,')
ARRAY_RE = re.compile(r"constexpr qm_card_registry::SCardSettingSearch (s_aQmSearch_\w+)\[\] = \{")
REGISTER_RE = re.compile(r'QM_CARD_SEARCH_REGISTER\(\s*"((?:qm|deck|tclient):[^"]+)"\s*,\s*(\w+)\s*\)')
INCLUDE_RE = re.compile(r'#include\s+"(QmCardSearch/[^"]+\.inc)"')


def parse_declaration_file(text: str) -> dict:
	"""解析单个源文件里的声明表、注册调用与旧格式宏。"""
	arrays: dict[str, set[str]] = {}
	for match in ARRAY_RE.finditer(text):
		open_brace = text.find("{", match.end() - 1)
		if open_brace < 0:
			continue
		close_brace = find_matching(text, open_brace)
		if close_brace < 0:
			continue
		arrays[match.group(1)] = set(LABEL_RE.findall(text[open_brace : close_brace + 1]))
	registrations = [(match.group(1), match.group(2)) for match in REGISTER_RE.finditer(text)]
	legacy: list[tuple[str, set[str]]] = []
	for match in DECLARATION_RE.finditer(text):
		open_paren = text.find("(", match.start())
		if open_paren < 0:
			continue
		close_paren = find_matching(text, open_paren)
		if close_paren < 0:
			continue
		legacy.append((match.group(1), set(LABEL_RE.findall(text[match.end() : close_paren]))))
	cards: dict[str, set[str]] = {}
	for stable_id, symbol in registrations:
		labels = arrays.get("s_aQmSearch_" + symbol)
		if labels is not None:
			cards.setdefault(stable_id, set()).update(labels)
	for stable_id, labels in legacy:
		cards.setdefault(stable_id, set()).update(labels)
	return {"cards": cards, "arrays": arrays, "registrations": registrations, "legacy": legacy}


def parse_declared_entries(text: str) -> dict[str, set[str]]:
	return parse_declaration_file(text)["cards"]


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


def parse_find_stable_id_vars(text: str) -> dict[str, str]:
	"""变量名 -> stableId（X = ...FindByStableId("id")）。"""
	return {match.group(1): match.group(2) for match in re.finditer(r'(\w+)\s*=\s*[^;\n]*FindByStableId\("([^"]+)"\)', text)}


def parse_spec_vars(text: str, var_to_stable_id: dict[str, str]) -> dict[str, str]:
	"""SSettingsCardSpec 变量 -> stableId。"""
	specs: dict[str, str] = {}
	for match in re.finditer(r"SSettingsCardSpec\s+(\w+)\s*\{\s*(\w+)->m_pStableId", text):
		stable_id = var_to_stable_id.get(match.group(2))
		if stable_id is not None:
			specs[match.group(1)] = stable_id
	return specs


def parse_stable_id_branches(text: str) -> dict[str, str]:
	"""str_comp(pStableId, "id") 之后的 if 块正文。"""
	bodies: dict[str, str] = {}
	for match in re.finditer(r'str_comp\(pStableId,\s*"((?:qm|deck|tclient):[^"]+)"\)', text):
		open_paren = text.find("(", match.start())
		close_paren = find_matching(text, open_paren)
		if close_paren < 0:
			continue
		open_brace = text.find("{", close_paren)
		if open_brace < 0 or open_brace - close_paren > 200:
			continue
		close_brace = find_matching(text, open_brace)
		if close_brace < 0:
			continue
		bodies[match.group(1)] = bodies.get(match.group(1), "") + text[open_brace : close_brace + 1]
	return bodies


def parse_addcard_spec_blocks(text: str, spec_to_stable_id: dict[str, str]) -> dict[str, str]:
	bodies: dict[str, str] = {}
	for match in re.finditer(r"AddCard\((\w+)\s*,", text):
		stable_id = spec_to_stable_id.get(match.group(1))
		if stable_id is None:
			continue
		open_paren = text.find("(", match.start())
		close_paren = find_matching(text, open_paren)
		if close_paren < 0:
			continue
		bodies[stable_id] = bodies.get(stable_id, "") + text[match.start() : close_paren + 1]
	return bodies


def parse_appearance_index_blocks(text: str) -> dict[str, str]:
	"""Appearance 的 aAppearanceIds 数组 + AddCard(Index, ...)。"""
	array_match = re.search(r"aAppearanceIds\[\] = \{(.*?)\};", text, re.DOTALL)
	if array_match is None:
		return {}
	stable_ids = re.findall(r'"((?:qm|deck|tclient):[^"]+)"', array_match.group(1))
	bodies: dict[str, str] = {}
	for match in re.finditer(r"AddCard\((\d+)\s*,", text):
		index = int(match.group(1))
		if index >= len(stable_ids):
			continue
		open_paren = text.find("(", match.start())
		close_paren = find_matching(text, open_paren)
		if close_paren < 0:
			continue
		stable_id = stable_ids[index]
		bodies[stable_id] = bodies.get(stable_id, "") + text[match.start() : close_paren + 1]
	return bodies


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


def extract_named_function_body(text: str, function_name: str) -> str | None:
	pattern = re.compile(r"^[A-Za-z_][^\n;]*::" + re.escape(function_name) + r"\s*\(", re.MULTILINE)
	match = pattern.search(text)
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


def read_sources(root: Path, relative: Path) -> dict[Path, str]:
	sources: dict[Path, str] = {}
	for path in (root / relative).rglob("*"):
		if path.suffix in {".cpp", ".h", ".inc"}:
			sources[path] = path.read_text(encoding="utf-8", errors="replace")
	return sources


def called_render_labels(body: str, sources: dict[Path, str], seen: set[str]) -> set[str]:
	"""块内调用的 Render* 函数正文标签（只展开一层）。"""
	labels: set[str] = set()
	for name in set(re.findall(r"\b(Render\w+)\s*\(", body)):
		if name in seen:
			continue
		seen.add(name)
		for text in sources.values():
			function_body = extract_named_function_body(text, name)
			if function_body is not None:
				labels |= extract_localize_labels(function_body)
				break
	return labels


def analyze(root: Path) -> dict:
	index_text = (root / INDEX_RELATIVE).read_text(encoding="utf-8")
	tables = parse_tables(index_text)
	metadata = parse_metadata(index_text)

	all_sources = read_sources(root, CLIENT_SOURCE_RELATIVE)
	all_sources.pop(root / INDEX_RELATIVE, None)
	client_sources = {path: text for path, text in all_sources.items() if path.suffix != ".inc"}
	card_sources = read_sources(root, CARD_SOURCE_RELATIVE)
	card_sources.pop(root / INDEX_RELATIVE, None)
	card_sources = {path: text for path, text in card_sources.items() if path.suffix != ".inc"}
	corpus = "\n".join(client_sources.values()).lower()

	declared: dict[str, set[str]] = {}
	declaration_problems: list[dict[str, str]] = []
	declared_symbols: dict[str, str] = {}
	registered_ids: set[str] = set()
	for path, text in all_sources.items():
		parsed = parse_declaration_file(text)
		relative = str(path.relative_to(root))
		file_registered: set[str] = set()
		for symbol in parsed["arrays"]:
			if symbol in declared_symbols:
				declaration_problems.append({"kind": "duplicate_symbol", "symbol": symbol, "source": relative})
			else:
				declared_symbols[symbol] = relative
		for stable_id, symbol in parsed["registrations"]:
			full_symbol = "s_aQmSearch_" + symbol
			file_registered.add(full_symbol)
			if full_symbol not in parsed["arrays"]:
				declaration_problems.append({"kind": "registration_without_array", "stable_id": stable_id, "symbol": symbol, "source": relative})
			if stable_id in registered_ids:
				declaration_problems.append({"kind": "duplicate_stable_id", "stable_id": stable_id, "source": relative})
			registered_ids.add(stable_id)
		for symbol in parsed["arrays"]:
			if symbol not in file_registered:
				declaration_problems.append({"kind": "array_without_registration", "symbol": symbol, "source": relative})
		for stable_id, labels in parsed["cards"].items():
			declared.setdefault(stable_id, set()).update(labels)

	decl_dir = root / "src/game/client/QmUi/cards/QmCardSearch"
	if decl_dir.is_dir():
		disk_files = {path.name for path in decl_dir.glob("*.inc")}
		included_files = {Path(name).name for name in INCLUDE_RE.findall(index_text)}
		for name in sorted(disk_files - included_files):
			declaration_problems.append({"kind": "missing_aggregator_include", "symbol": name, "source": str(decl_dir.relative_to(root) / name)})
		for name in sorted(included_files - disk_files):
			declaration_problems.append({"kind": "missing_declaration_file", "symbol": name, "source": str(decl_dir.relative_to(root) / name)})

	defaults: set[str] | None = None
	registry_path = root / "src/game/client/QmUi/QmCardRegistry.cpp"
	if registry_path.exists():
		registry_text = registry_path.read_text(encoding="utf-8")
		start = registry_text.find("DefaultsTable()")
		end = registry_text.find("return s_aDefaults;", start)
		if start >= 0 and end > start:
			defaults = set(re.findall(r'\{"((?:qm|deck|tclient):[^"]+)"\s*,', registry_text[start:end]))
	if defaults is not None:
		for stable_id in sorted(declared.keys() - defaults):
			declaration_problems.append({"kind": "unknown_card", "stable_id": stable_id})
		known_cards = set(declared) | set(metadata)
		for stable_id in sorted(defaults - known_cards - CARDS_WITHOUT_SEARCH_TABLE):
			declaration_problems.append({"kind": "unregistered_default_card", "stable_id": stable_id})

	all_index_labels: set[str] = set()
	for labels in declared.values():
		all_index_labels |= labels
	for table in tables.values():
		all_index_labels |= table["labels"]

	card_labels: set[str] = set()
	for text in card_sources.values():
		card_labels |= extract_localize_labels(text)
	global_unindexed = sorted(label for label in card_labels if label not in all_index_labels and label not in IGNORED_SOURCE_LABELS and label not in ACTION_BUTTON_LABELS)

	tables_by_card: dict[str, set[str]] = {stable_id: labels for stable_id, labels in declared.items()}
	missing_tables = []
	for stable_id, table_name in metadata.items():
		if table_name not in tables:
			missing_tables.append(stable_id)
			continue
		tables_by_card.setdefault(stable_id, set()).update(tables[table_name]["labels"])
	registered_tables = set(metadata.values())

	drifted = []
	for stable_id, labels in sorted(declared.items()):
		for label in sorted(labels):
			if label not in SEARCH_ONLY_LABELS and label.lower() not in corpus:
				drifted.append({"table": stable_id, "label": label})
	for table_name, table in sorted(tables.items()):
		for label in sorted(table["labels"]):
			if label not in SEARCH_ONLY_LABELS and label.lower() not in corpus:
				drifted.append({"table": table_name, "label": label})

	add_mappings: dict[str, tuple[str, str]] = {}
	block_mappings: dict[str, tuple[str, str]] = {}
	for path, text in client_sources.items():
		relative = str(path.relative_to(root))
		for stable_id, function_name in parse_add_mappings(text).items():
			add_mappings.setdefault(stable_id, (relative, function_name))
		for stable_id, function_name in parse_make_module_mappings(text).items():
			add_mappings.setdefault(stable_id, (relative, function_name))
		var_to_stable_id = parse_find_stable_id_vars(text)
		spec_to_stable_id = parse_spec_vars(text, var_to_stable_id)
		blocks: dict[str, str] = {}
		for source in (parse_stable_id_branches(text), parse_addcard_spec_blocks(text, spec_to_stable_id), parse_appearance_index_blocks(text)):
			for stable_id, body in source.items():
				blocks[stable_id] = blocks.get(stable_id, "") + body
		for stable_id, body in blocks.items():
			block_mappings.setdefault(stable_id, (relative, body))

	unindexed_set = set(global_unindexed)
	cards = []
	unmapped = []
	exact_cards = 0
	for stable_id, table_labels in sorted(tables_by_card.items()):
		source_labels: set[str] = set()
		origin = ""
		confidence = ""
		if stable_id in add_mappings:
			source_path, function_name = add_mappings[stable_id]
			body = extract_function_body(client_sources[root / source_path], function_name)
			origin = function_name
			confidence = "function"
			if body is not None:
				source_labels = extract_localize_labels(body)
				source_labels |= called_render_labels(body, client_sources, set())
		elif stable_id in block_mappings:
			source_path, body = block_mappings[stable_id]
			origin = "block"
			confidence = "block"
			source_labels = extract_localize_labels(body)
			source_labels |= called_render_labels(body, client_sources, set())
		else:
			unmapped.append(stable_id)
			cards.append({
				"stable_id": stable_id,
				"confidence": "none",
				"function": "",
				"source": "",
				"source_labels": 0,
				"indexed_labels": len(table_labels),
				"missing": [],
			})
			continue
		exact_cards += 1
		missing = sorted((source_labels - table_labels) & unindexed_set)
		cards.append({
			"stable_id": stable_id,
			"confidence": confidence,
			"function": origin,
			"source": source_path,
			"source_labels": len(source_labels),
			"indexed_labels": len(table_labels),
			"missing": missing,
		})

	return {
		"tables": len(declared) + len(tables),
		"registered_cards": len(tables_by_card),
		"unregistered_tables": sorted(set(tables) - registered_tables),
		"missing_tables": sorted(missing_tables),
		"declaration_problems": declaration_problems,
		"index_labels": len(all_index_labels),
		"drifted_labels": drifted,
		"mapped_cards": exact_cards,
		"exact_cards": exact_cards,
		"no_source_cards": unmapped,
		"cards": cards,
		"missing_total": sum(len(card["missing"]) for card in cards),
		"unindexed_labels": global_unindexed,
		"ignored_labels": sorted(IGNORED_SOURCE_LABELS),
		"indexed_action_labels": sorted(ACTION_BUTTON_LABELS & all_index_labels),
	}


def render_markdown(report: dict) -> str:
	lines = [
		"# 设置搜索索引缺口",
		"",
		f"- 索引表：{report['tables']}，登记卡：{report['registered_cards']}",
		f"- 索引标签：{report['index_labels']}",
		f"- 精确映射卡片：{report['mapped_cards']}；未精确映射：{len(report['no_source_cards'])}",
		f"- 索引标签在源码中失配：{len(report['drifted_labels'])}",
		f"- 声明结构问题：{len(report['declaration_problems'])}",
		f"- 未入索引的卡片文案：{len(report['unindexed_labels'])}",
		f"- 已知例外（共享弹窗/状态/标题）：{len(report['ignored_labels'])}",
		f"- 仍被索引的动作按钮文案：{len(report['indexed_action_labels'])}",
		"",
	]
	if report["declaration_problems"]:
		lines.extend(["## 声明结构问题", "", "| 类型 | stableId | symbol | 位置 |", "| --- | --- | --- | --- |"])
		for item in report["declaration_problems"]:
			lines.append(f"| {item['kind']} | {item.get('stable_id', '')} | {item.get('symbol', '')} | {item.get('source', '')} |")
		lines.append("")
	if report["drifted_labels"]:
		lines.extend(["## 失效索引标签", "", "| 表 | 标签 |", "| --- | --- |"])
		for item in report["drifted_labels"]:
			lines.append(f"| {item['table']} | {item['label']} |")
		lines.append("")
	if report["unindexed_labels"]:
		lines.extend(["## 未入索引的卡片文案", ""])
		for label in report["unindexed_labels"]:
			lines.append(f"- {label}")
		lines.append("")
	rows = [card for card in report["cards"] if card["missing"]]
	if rows:
		lines.extend([
			"## 有缺口的卡片",
			"",
			"| 卡片 | 映射 | 源码标签 | 已入索引 | 缺口 | 示例 |",
			"| --- | --- | --- | --- | --- | --- |",
		])
		for card in sorted(rows, key=lambda item: len(item["missing"]), reverse=True):
			examples = "；".join(card["missing"][:4])
			lines.append(f"| {card['stable_id']} | {card['confidence']} | {card['source_labels']} | {card['indexed_labels']} | {len(card['missing'])} | {examples} |")
		lines.append("")
	if report["no_source_cards"]:
		lines.extend([
			"## 未精确映射的卡片",
			"",
			"这些卡的索引标签来自绑定命令、共享组件或纯配置名，无法在卡片源码中定位到唯一文案；它们仍参与全局“未入索引文案”统计：",
			"",
			", ".join(report["no_source_cards"]),
			"",
		])
	if report["ignored_labels"]:
		lines.extend([
			"## 已知例外",
			"",
			"共享取色弹窗、运行期状态与卡片标题不计入缺口：",
			"",
			", ".join(report["ignored_labels"]),
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
		problems = len(report["drifted_labels"]) + len(report["unregistered_tables"]) + len(report["missing_tables"]) + len(report["declaration_problems"]) + len(report["indexed_action_labels"])
		if problems:
			print(f"索引结构检查失败：{problems} 项", file=sys.stderr)
			return 1
	return 0


if __name__ == "__main__":
	sys.exit(main())
