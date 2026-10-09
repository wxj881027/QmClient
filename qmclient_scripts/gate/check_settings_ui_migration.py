#!/usr/bin/env python3
"""Audit settings catalog migration structure, not UI runtime behavior."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


PAGE_STABLE_IDS = {
	"general": ("deck:general-game", "deck:general-language", "deck:general-client", "deck:general-recording", "deck:tclient-info-files"),
	"player": ("deck:player-identity", "deck:player-country"),
	"tee": ("deck:tee-identity", "deck:tee-skin-options", "deck:tee-skin-list", "deck:tee-skin-queue", "qm:skin_appearance", "deck:tee-glow"),
	"tee7": ("deck:tee7-editor",),
	"graphics": ("deck:graphics-display", "deck:graphics-visual", "deck:graphics-modes", "deck:graphics-interaction"),
	"sound": ("deck:sound-toggle", "deck:sound-volume", "deck:sound-audio-pack"),
	"ddnet": ("deck:ddnet-demo", "deck:ddnet-gameplay", "deck:ddnet-background", "deck:ddnet-miscellaneous"),
	"appearance": (
		"deck:appearance-hud-main",
		"deck:appearance-hud-ddrace",
		"deck:appearance-chat-settings",
		"deck:appearance-chat-messages",
		"deck:appearance-chat-preview",
		"deck:appearance-name-plate-settings",
		"deck:appearance-name-plate-preview",
		"deck:appearance-hook-collision-main",
		"deck:appearance-hook-collision-preview",
		"deck:appearance-info-messages",
		"deck:appearance-laser-enhanced",
		"deck:appearance-laser-colors",
		"deck:appearance-laser-preview",
	),
	"controls": (
		"deck:controls-movement",
		"deck:controls-weapon",
		"deck:controls-voting",
		"deck:controls-chat",
		"deck:controls-dummy",
		"deck:controls-miscellaneous",
		"deck:controls-custom",
		"deck:controls-mouse",
		"deck:controls-controller",
	),
	"qmclient_hud": (
		"qm:coords",
		"qm:player_stats",
		"qm:debug_graph",
		"qm:debug_mode",
		"qm:input_overlay",
		"qm:hud_notifications",
		"qm:voice",
		"qm:dummy_miniview",
		"qm:dynamic_island",
		"qm:system_media_controls",
		"qm:lyrics",
		"qm:bind_status_hud",
		"qm:background_3d",
	),
	"qmclient_function": (
		"qm:translate_ui",
		"qm:gores_actor",
		"qm:gores",
		"qm:key_binds",
		"qm:emoticons",
		"qm:mini_features",
		"qm:jump_hint",
		"qm:weapon_trajectory",
		"qm:friend_notify",
		"qm:block_words",
		"qm:qiafen",
		"qm:translate",
		"qm:pie_menu",
		"qm:map_upload",
		"qm:favorite_maps",
		"qm:hj_assist",
		"qm:solo_split",
	),
	"qmclient_visual": (
		"qm:chat_bubble",
		"qm:camera_view",
		"qm:skin_transition",
		"qm:focus_mode",
		"qm:weapon_animation",
		"qm:entity_overlay",
		"qm:collision_hitbox",
		"qm:streamer",
	),
	"contributors": ("deck:qmclient-contributors-community", "deck:qmclient-contributors-title", "deck:qmclient-contributors-title-display", "deck:qmclient-contributors-sponsors", "deck:credits-friend-links", "deck:qmclient-contributors-ddnet", "deck:tclient-info-developers"),
	"global_search": ("deck:global-search-input", "deck:global-search-results"),
	"tclient": (
		"tclient:visual-font-cursor",
		"tclient:visual-nameplates",
		"tclient:visual-effects",
		"tclient:input",
		"tclient:anti-latency-tools",
		"tclient:improved-anti-ping",
		"tclient:execute-on-join",
		"tclient:voting",
		"tclient:auto-reply",
		"tclient:player-indicator",
		"tclient:pet",
		"tclient:hud",
		"tclient:tee-status-bar",
		"tclient:tile-outlines",
		"tclient:ghost-tools",
		"tclient:rainbow",
		"tclient:tee-trails",
		"tclient:background-draw",
		"tclient:finish-name",
	),
	"tclient_bind_wheel": ("deck:tclient-bind-wheel-editor", "deck:tclient-bind-wheel-preview"),
	"tclient_chat_binds": ("deck:tclient-chat-binds-kaomoji", "deck:tclient-chat-binds-warlist", "deck:tclient-chat-binds-other"),
	"tclient_warlist": ("deck:tclient-warlist",),
	"tclient_status_bar": ("deck:tclient-status-bar-settings", "deck:tclient-status-bar-preview"),
	"tclient_profiles": ("deck:tclient-profiles-actions", "deck:tclient-profiles-options", "deck:tclient-profiles-list"),
	"tclient_configs": ("deck:tclient-configs-actions",),
	"assets": (),
}

PAGE_FUNCTIONS = {
	"general": ("CMenus::RenderSettingsGeneral",),
	"player": ("CMenus::RenderSettingsPlayer",),
	"tee": ("CMenus::RenderSettingsTee(CUIRect MainView)",),
	"tee7": ("CMenus::RenderSettingsTee7(CUIRect MainView)",),
	"graphics": ("CMenus::RenderSettingsGraphics",),
	"sound": ("CMenus::RenderSettingsSound",),
	"ddnet": ("CMenus::RenderSettingsDDNet",),
	"appearance": ("CMenus::RenderSettingsAppearance",),
	"controls": ("CMenusSettingsControls::Render",),
	"qmclient_hud": ("CMenus::RenderSettingsQmClientHudDeck",),
	"qmclient_function": ("CMenus::RenderSettingsQmClientFunctionDeck",),
	"qmclient_visual": ("CMenus::RenderSettingsQmClientVisualDeck",),
	"contributors": ("CMenus::RenderSettingsContributors",),
	"global_search": ("CMenus::RenderSettingsGlobalSearchContent",),
	"tclient": ("CMenus::RenderSettingsTClientSettings",),
	"tclient_bind_wheel": ("CMenus::RenderSettingsTClientBindWheel",),
	"tclient_chat_binds": ("CMenus::RenderSettingsTClientChatBinds",),
	"tclient_warlist": ("CMenus::RenderSettingsTClientWarList",),
	"tclient_status_bar": ("CMenus::RenderSettingsTClientStatusBar",),
	"tclient_profiles": ("CMenus::RenderSettingsTClientProfiles",),
	"tclient_configs": ("CMenus::RenderSettingsTClientConfigs",),
	"assets": ("CMenus::RenderSettingsCustom",),
}

PAGE_ROUTE_TABS = {
	"general": ("general",),
	"player": ("player",),
	"tee": ("tee",),
	"tee7": ("tee7",),
	"graphics": ("graphics",),
	"sound": ("sound",),
	"ddnet": ("ddnet",),
	"appearance": (
		"appearance-hud",
		"appearance-chat",
		"appearance-name-plate",
		"appearance-hook-collision",
		"appearance-info-messages",
		"appearance-laser",
	),
	"controls": ("controls",),
	"qmclient_hud": ("hud",),
	"qmclient_function": ("function",),
	"qmclient_visual": ("visual",),
	"contributors": ("qmclient-contributors",),
	"global_search": (),
	"tclient": ("tclient",),
	"tclient_bind_wheel": ("tclient-bind-wheel",),
	"tclient_chat_binds": ("tclient-chat-binds",),
	"tclient_warlist": ("tclient-warlist",),
	"tclient_status_bar": ("tclient-status-bar",),
	"tclient_profiles": ("tclient-profiles",),
	"tclient_configs": ("tclient-configs",),
	"assets": (),
}

COMMON_FORBIDDEN = (
	"SettingsCard(",
	"BeginSettingsCardDeck(",
	"BeginSettingsCardDeckCard(",
	"DoSettingsScrollbarOption(",
	"DoSettingsSliderInputField(",
	"ui_widget::TextFieldEx(",
	"ui_widget::SearchFieldEx(",
	"ui_widget::ClearableTextFieldEx(",
	"ui_widget::IconTextFieldEx(",
	"ui_widget::LegacyTextFieldEx(",
	"ui_widget::TextField(",
	"ui_widget::SearchField(",
	"ui_widget::ClearableTextField(",
	"ui_widget::IconTextField(",
	"Ui()->DoEditBox(",
	"Ui()->DoScrollbarH(",
)
PAGE_ALLOWED_FORBIDDEN_CALLS = {
	"tee": {
		"Ui()->DoEditBox(": (
			"Ui()->DoEditBox(&ColorCodeInput, &ColorCodeEditBox, std::max(10.0f, BodySize * 0.85f), IGraphics::CORNER_ALL, {}, TEXTALIGN_MC)",
		),
	},
}
STRICT_LEGACY_PAGES = {"general", "player", "tee", "tee7", "graphics", "sound", "ddnet", "appearance", "controls"}
DECK_LEGACY_FORBIDDEN = ("BeginSettingsCardDeck(", "BeginSettingsCardDeckCard(")
PAGE_FORBIDDEN = {
	"graphics": ("s_GraphicsSettingsScrollRegion",),
	"sound": ("s_SoundSettingsScrollRegion",),
	"ddnet": ("s_DDNetSettingsScrollRegion",),
	"appearance": (
		"BeginAppearanceCard",
		"s_ChatSettingsScrollRegion",
		"s_NamePlateSettingsScrollRegion",
		"s_LaserSettingsScrollRegion",
	),
	"controls": ("RenderSettingsBlock",),
	"tclient_warlist": (
		"deck:tclient-warlist-entries",
		"deck:tclient-warlist-editor",
		"deck:tclient-warlist-settings",
		"deck:tclient-warlist-groups",
		"deck:tclient-warlist-players",
	),
}


REGISTRY_FORBIDDEN = {
	"tclient_warlist": PAGE_FORBIDDEN["tclient_warlist"],
}

_PAGE_SOURCE = {
	"controls": Path("src/game/client/components/menus_settings_controls.cpp"),
	"tee7": Path("src/game/client/components/menus_settings7.cpp"),
	"qmclient_hud": Path("src/game/client/components/qmclient/menus_qmclient.cpp"),
	"qmclient_function": Path("src/game/client/components/qmclient/menus_qmclient.cpp"),
	"qmclient_visual": Path("src/game/client/components/qmclient/menus_qmclient.cpp"),
	"contributors": Path("src/game/client/components/menus_credits.cpp"),
	"global_search": Path("src/game/client/components/qmclient/menus_qmclient.cpp"),
	"tclient": Path("src/game/client/components/tclient/menus_tclient.cpp"),
	"tclient_bind_wheel": Path("src/game/client/components/tclient/menus_tclient.cpp"),
	"tclient_chat_binds": Path("src/game/client/components/tclient/menus_tclient.cpp"),
	"tclient_warlist": Path("src/game/client/components/tclient/menus_tclient.cpp"),
	"tclient_status_bar": Path("src/game/client/components/tclient/menus_tclient.cpp"),
	"tclient_profiles": Path("src/game/client/components/tclient/menus_tclient.cpp"),
	"tclient_configs": Path("src/game/client/components/tclient/menus_tclient.cpp"),
	"assets": Path("src/game/client/components/menus_settings_assets.cpp"),
}
_DEFAULT_SOURCE = Path("src/game/client/components/menus_settings.cpp")
_REGISTRY_SOURCE = Path("src/game/client/QmUi/QmCardRegistry.cpp")
_NAVIGATION_SOURCE = Path("src/game/client/components/menus.cpp")
# 卡片生产的归属：页面声明「这一页有哪些卡片」，具体生产在全局卡片目录的分类模块里（N3）。
_CATALOGUE_SOURCE = Path("src/game/client/QmUi/cards/QmCardCatalogIds.cpp")
PAGE_CATALOGUE_LIST = {
	"controls": "ControlsCardStableIds",
	"general": "GeneralCardStableIds",
	"tee": "TeeCardStableIds",
	"qmclient_hud": "HudCardStableIds",
	"qmclient_function": "FunctionCardStableIds",
	"qmclient_visual": "VisualCardStableIds",
}
_CATALOGUE_LIST_STATICS = {
	"ControlsCardStableIds": "s_vControlsCards",
	"GeneralCardStableIds": "s_vGeneralCards",
	"TeeCardStableIds": "s_vTeeCards",
	"HudCardStableIds": "s_vHudCards",
	"FunctionCardStableIds": "s_vFunctionCards",
	"VisualCardStableIds": "s_vVisualCards",
}


# registry 驱动的页面已迁入目录模块；这里只约束入口、归属和注册。
CATALOG_PAGE_SOURCES = {
	"player": "QmCardCatalogPlayer.cpp",
	"tee7": "QmCardCatalogTee7.cpp",
	"graphics": "QmCardCatalogGraphics.cpp",
	"sound": "QmCardCatalogSound.cpp",
	"ddnet": "QmCardCatalogDDNet.cpp",
	"appearance": "QmCardCatalogAppearance.cpp",
	"tclient": "QmCardCatalogTClientMain.cpp",
	"tclient_bind_wheel": "QmCardCatalogTClientBindWheel.cpp",
	"tclient_chat_binds": "QmCardCatalogTClientChatBinds.cpp",
	"tclient_warlist": "QmCardCatalogTClientWarList.cpp",
	"tclient_status_bar": "QmCardCatalogTClientStatusBar.cpp",
	"tclient_profiles": "QmCardCatalogTClientProfiles.cpp",
	"tclient_configs": "QmCardCatalogTClientConfigs.cpp",
}
_CARDS_DIRECTORY = Path("src/game/client/QmUi/cards")


def _initializer(source: str, name: str) -> str:
	"""读取具名清单初始化范围，不接受注释或字符串内的伪声明。"""
	code = _mask_cpp_comments_and_strings(source)
	match = re.search(r"\b" + re.escape(name) + r"\s*(?:\[[^\]]*\]\s*)?=\s*\{", code)
	if match is None:
		return ""
	start = match.end() - 1
	depth = 0
	for index in range(start, len(code)):
		if code[index] == "{":
			depth += 1
		elif code[index] == "}":
			depth -= 1
			if depth == 0:
				return source[start:index + 1]
	return ""


def _string_literals(source: str) -> list[str]:
	"""按词法顺序跳过注释、字符和 raw string，只读取实际字符串清单。"""
	tokens = re.compile(
		r'//[^\n]*|/\*[\s\S]*?(?:\*/|$)|'
		r'(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\	\r\n]{0,16})\([\s\S]*?\)(?P=delimiter)"|'
		r"'(?:\\.|[^'\\])*'|"
		r'"(?P<literal>(?:\\.|[^"\\])*)"'
	)
	return [match.group("literal") for match in tokens.finditer(source) if match.group("literal") is not None]


def _registry_entries(source: str) -> dict[str, list[str]]:
	entries: dict[str, list[str]] = {}
	initializer = _initializer(source, "Cards")
	code = _mask_cpp_comments_and_strings(initializer)
	for match in re.finditer(r'\{\s*"([^"\\]+)"\s*,\s*"([^"\\]+)"\s*,', initializer):
		if code[match.start()] == "{":
			entries.setdefault(match.group(1), []).append(match.group(2))
	return entries


def _catalogue_list_contains(root: Path, list_name: str, stable_id: str) -> bool:
	static_name = _CATALOGUE_LIST_STATICS.get(list_name)
	if static_name is None:
		return False
	source = _read(root, _CATALOGUE_SOURCE)
	body = _extract_function_body(source, list_name) or ""
	if not re.search(r"\breturn\s+" + re.escape(static_name) + r"\s*;", _mask_cpp_comments_and_strings(body)):
		return False
	return _string_literals(_initializer(source, static_name)).count(stable_id) == 1

_TYPOGRAPHY_SOURCES = (
	Path("src/game/client/components/menus_settings.cpp"),
	Path("src/game/client/components/menus_settings7.cpp"),
	Path("src/game/client/components/menus_settings_controls.cpp"),
	Path("src/game/client/components/tclient/menus_tclient.cpp"),
	Path("src/game/client/components/qmclient/menus_qmclient.cpp"),
)
_FONT_ASSIGNMENT_SOURCES = _TYPOGRAPHY_SOURCES + (_NAVIGATION_SOURCE,)
# 特殊视觉元素不属于设置内容文字：国旗代码、Tee/皮肤状态图标、统计预览、
# 颜色拾取器和地图 popup。新增例外必须在这里集中说明，禁止在业务页静默散落裸字号。
_RAW_FONT_ALLOWLIST = (
	"Entry.m_aCountryCodeString",
	"Entry.m_pFlag->m_aCountryCodeString",
	"pEntry->m_aCountryCodeString",
	"&ChangeInfo, aStats",
	"FONT_ICON_QUESTION",
	"FONT_ICON_SQUARE_MINUS",
	"&Label, aBuf, 12.0f",
	"&Icon, pIconType",
	"&Label, aLabelText",
)
_RAW_FONT_LITERAL = re.compile(r"\b(?:9|10|12|13|14|16|20|24|25)\.0f\b")
_CPP_RAW_STRING_START = re.compile(r'(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(')
def _extract_function_body(source: str, symbol: str) -> str | None:
	"""提取真实定义，忽略注释、字符串和没有函数体的声明。"""
	code = _mask_cpp_comments_and_strings(source)
	pattern = re.escape(symbol) if "(" in symbol else r"\b" + re.escape(symbol) + r"(?=\s*\()"
	match = re.search(pattern, code)
	if match is None:
		return None
	start = code.find("{", match.end())
	semicolon = code.find(";", match.end())
	if start < 0 or (0 <= semicolon < start):
		return None
	depth = 0
	for index in range(start, len(code)):
		if code[index] == "{":
			depth += 1
		elif code[index] == "}":
			depth -= 1
			if depth == 0:
				return source[match.start():index + 1]
	return None


def _read(root: Path, relative: Path) -> str:
	path = root / relative
	try:
		return path.read_text(encoding="utf-8")
	except FileNotFoundError:
		return ""


def _navigation_has_route(navigation: str, route: str) -> bool:
	set_page_body = _extract_function_body(navigation, "CMenus::SetSettingsPageFromCardTab") or ""
	return f'str_comp(pTab, "{route}") == 0' in set_page_body


def _mask_cpp_comments_and_strings(source: str) -> str:
	masked = list(source)
	index = 0
	quote = ""
	line_comment = False
	block_comment = False
	while index < len(source):
		char = source[index]
		next_char = source[index + 1] if index + 1 < len(source) else ""
		if line_comment:
			if char == "\n":
				line_comment = False
			else:
				masked[index] = " "
		elif block_comment:
			if char != "\n":
				masked[index] = " "
			if char == "*" and next_char == "/":
				masked[index + 1] = " "
				block_comment = False
				index += 1
		elif quote:
			if char != "\n":
				masked[index] = " "
			if char == "\\":
				if index + 1 < len(source) and source[index + 1] != "\n":
					masked[index + 1] = " "
				index += 1
			elif char == quote:
				quote = ""
		elif char == "/" and next_char == "/":
			masked[index] = masked[index + 1] = " "
			line_comment = True
			index += 1
		elif char == "/" and next_char == "*":
			masked[index] = masked[index + 1] = " "
			block_comment = True
			index += 1
		elif (index == 0 or not (source[index - 1].isalnum() or source[index - 1] == "_")) and (raw_match := _CPP_RAW_STRING_START.match(source, index)) is not None:
			raw_end_marker = ")" + raw_match.group("delimiter") + '"'
			raw_end = source.find(raw_end_marker, raw_match.end())
			masked_end = len(source) if raw_end < 0 else raw_end + len(raw_end_marker)
			for raw_index in range(index, masked_end):
				if source[raw_index] != "\n":
					masked[raw_index] = " "
			index = masked_end - 1
		elif char in ('"', "'"):
			masked[index] = " "
			quote = char
		index += 1
	return "".join(masked)


def _looks_like_template_angle_open(code: str, index: int) -> bool:
	previous = index - 1
	while previous >= 0 and code[previous].isspace():
		previous -= 1
	if previous < 0 or not (code[previous].isalnum() or code[previous] in "_>:"):
		return False
	depth = 1
	cursor = index + 1
	while cursor < len(code):
		if code[cursor] == "<":
			depth += 1
		elif code[cursor] == ">":
			depth -= 1
			if depth == 0:
				following = cursor + 1
				while following < len(code) and code[following].isspace():
					following += 1
				return following == len(code) or code[following] in "({[,:>)"
		elif code[cursor] == ";" or (code[cursor] == "\n" and depth == 1):
			return False
		cursor += 1
	return False


def _iter_cpp_call_arguments(source: str, function_name: str):
	code = _mask_cpp_comments_and_strings(source)
	pattern = re.compile(rf"\b{re.escape(function_name)}\s*\(")
	for match in pattern.finditer(code):
		open_paren = code.find("(", match.start())
		paren_depth = 1
		bracket_depth = 0
		brace_depth = 0
		angle_depth = 0
		index = open_paren + 1
		argument_start = index
		arguments: list[str] = []
		while index < len(code):
			char = code[index]
			if char == "(":
				paren_depth += 1
			elif char == ")":
				paren_depth -= 1
				if paren_depth == 0:
					arguments.append(code[argument_start:index].strip())
					yield match.start(), index + 1, code.count("\n", 0, match.start()) + 1, arguments
					break
			elif char == "[":
				bracket_depth += 1
			elif char == "]":
				bracket_depth = max(0, bracket_depth - 1)
			elif char == "{":
				brace_depth += 1
			elif char == "}":
				brace_depth = max(0, brace_depth - 1)
			elif char == "<" and _looks_like_template_angle_open(code, index):
				angle_depth += 1
			elif char == ">" and angle_depth > 0:
				angle_depth -= 1
			elif char == "," and paren_depth == 1 and bracket_depth == 0 and brace_depth == 0 and angle_depth == 0:
				arguments.append(code[argument_start:index].strip())
				argument_start = index + 1
			index += 1


def _find_raw_font_literals(source: str) -> list[int]:
	code = _mask_cpp_comments_and_strings(source)
	result: list[int] = []
	for function_name in ("DoLabel", "DoSettingsLabel", "DoSettingsMenuLabel"):
		for call_start, call_end, line_number, _ in _iter_cpp_call_arguments(source, function_name):
			call = code[call_start:call_end]
			if _RAW_FONT_LITERAL.search(call) and not any(token in call for token in _RAW_FONT_ALLOWLIST):
				result.append(line_number)
	return sorted(result)


def _find_rect_derived_font_arguments(source: str) -> list[tuple[int, str]]:
	"""Find settings labels whose font argument is derived from a control rectangle."""
	result: list[tuple[int, str]] = []
	font_argument_indices = {
		"DoLabel": 2,
		"DoSettingsLabel": 5,
		"DoSettingsMenuLabel": 6,
	}
	for function_name, font_argument_index in font_argument_indices.items():
		for _, _, line_number, arguments in _iter_cpp_call_arguments(source, function_name):
			if len(arguments) <= font_argument_index:
				continue
			font_argument = arguments[font_argument_index]
			if re.search(r"\b[A-Za-z_]\w*(?:\.|->)h\b", font_argument):
				result.append((line_number, font_argument))
	return sorted(result)


def _find_rect_derived_font_assignments(source: str) -> list[tuple[int, str]]:
	"""Find every m_FontSize assignment whose right-hand side depends on a rectangle height."""
	code = _mask_cpp_comments_and_strings(source)
	result: list[tuple[int, str]] = []
	pattern = re.compile(r"\b[A-Za-z_]\w*(?:\.|->)m_FontSize\s*=(?!=)\s*(?P<rhs>[^;]+);")
	for match in pattern.finditer(code):
		rhs = match.group("rhs").strip()
		if re.search(r"\b[A-Za-z_]\w*(?:\.|->)h\b", rhs):
			original_rhs = source[match.start("rhs"):match.end("rhs")].strip()
			result.append((code.count("\n", 0, match.start()) + 1, original_rhs))
	return result


def _find_legacy_color_picker_geometry(source: str) -> list[tuple[int, str]]:
	code = _mask_cpp_comments_and_strings(source)
	declaration_pattern = re.compile(r"\b(?P<type>SSettingsContentMetrics|float|double|int|unsigned|long|short|auto)\s+(?:const\s+)?[&*]?\s*(?P<name>[A-Za-z_]\w*)\s*(?=[=;,){}\[])")
	legacy: list[tuple[int, str]] = []
	for call_position, _, line_number, arguments in _iter_cpp_call_arguments(source, "DoLine_ColorPicker"):
		if len(arguments) < 2:
			continue
		argument = arguments[1]
		if argument == "CurrentSettingsContentMetrics()":
			continue
		if re.fullmatch(r"[A-Za-z_]\w*", argument) is not None:
			declarations = [match for match in declaration_pattern.finditer(code, 0, call_position) if match.group("name") == argument]
			if declarations and declarations[-1].group("type") == "SSettingsContentMetrics":
				continue
		legacy.append((line_number, argument))
	return legacy


def _contains_forbidden_token(page: str, source: str, token: str) -> bool:
	"""Return whether a page contains a forbidden call outside its explicit migration allowlist."""
	remaining_source = source
	for allowed_call in PAGE_ALLOWED_FORBIDDEN_CALLS.get(page, {}).get(token, ()):
		remaining_source = remaining_source.replace(allowed_call, "", 1)
	if token not in remaining_source:
		return False
	if ":" in token:
		return token in _string_literals(remaining_source)
	return re.search(r"(?<![\w])" + re.escape(token), _mask_cpp_comments_and_strings(remaining_source)) is not None


def audit_page(repo_root: Path, page: str) -> list[str]:
	if page not in PAGE_STABLE_IDS:
		raise ValueError(f"unknown settings page: {page}")
	errors: list[str] = []
	source = _read(repo_root, _PAGE_SOURCE.get(page, _DEFAULT_SOURCE))
	bodies = []
	for symbol in PAGE_FUNCTIONS[page]:
		body = _extract_function_body(source, symbol)
		if body is None:
			errors.append(f"{page}: {symbol}: page entry definition missing")
		else:
			bodies.append(body)
	page_source = "\n".join(bodies)
	contract_source = page_source
	entries = _registry_entries(_read(repo_root, _REGISTRY_SOURCE))
	navigation = _read(repo_root, _NAVIGATION_SOURCE)
	if page in CATALOG_PAGE_SOURCES:
		# 接线属于迁移架构合同；布局、滚动、测量和输入行为由运行时测试负责。
		calls = list(_iter_cpp_call_arguments(page_source, "RenderSettingsCatalogPage"))
		if not calls:
			errors.append(f"{page}: catalog page delegation missing")
		tabs = PAGE_ROUTE_TABS[page]
		delegated_tabs = []
		for start, end, _, arguments in calls:
			delegated_tabs.extend(_string_literals(page_source[start:end]))
			if len(arguments) > 1 and (array := re.match(r"(\w+)\s*\[", arguments[1])):
				delegated_tabs.extend(_string_literals(_initializer(page_source, array.group(1))))
		for tab in tabs:
			if tab not in delegated_tabs:
				errors.append(f"{page}: {tab}: catalog page route missing")
		producer = _CARDS_DIRECTORY / CATALOG_PAGE_SOURCES[page]
		producer_source = _read(repo_root, producer)
		contract_source += "\n" + producer_source
		if not producer_source.strip():
			errors.append(f"{page}: {producer}: catalog producer source missing")
		for stable_id in PAGE_STABLE_IDS[page]:
			if len(entries.get(stable_id, [])) == 1 and entries[stable_id][0] not in tabs:
				errors.append(f"{page}: {stable_id}: registry category mismatch")
	else:
		catalogue_list = PAGE_CATALOGUE_LIST.get(page)
		if catalogue_list is not None:
			if not list(_iter_cpp_call_arguments(page_source, f"qm_card_catalog::{catalogue_list}")):
				errors.append(f"{page}: {catalogue_list}: page category delegation missing")
			for stable_id in PAGE_STABLE_IDS[page]:
				if not _catalogue_list_contains(repo_root, catalogue_list, stable_id):
					errors.append(f"{page}: {stable_id}: catalogue category entry missing or duplicated")
	for stable_id in PAGE_STABLE_IDS[page]:
		if len(entries.get(stable_id, [])) != 1:
			errors.append(f"{page}: {stable_id}: registry entry missing or duplicated")
	for token in REGISTRY_FORBIDDEN.get(page, ()):
		if token in entries:
			errors.append(f"{page}: {token}: legacy registry entry remains")
	for route in PAGE_ROUTE_TABS[page]:
		if not _navigation_has_route(navigation, route):
			errors.append(f"{page}: {route}: registry/navigation entry missing")
	if page == "controls":
		contract_source += "\n" + _read(repo_root, _CARDS_DIRECTORY / "QmCardCatalogControls.cpp")
	forbidden = (COMMON_FORBIDDEN if page in STRICT_LEGACY_PAGES else DECK_LEGACY_FORBIDDEN) + PAGE_FORBIDDEN.get(page, ())
	for token in forbidden:
		if _contains_forbidden_token(page, contract_source, token):
			errors.append(f"{page}: {token}: legacy path remains")
	return errors


def audit_catalog_build_contracts(repo_root: Path) -> list[str]:
	"""独立编译单元必须存在并注册，不以函数体片段声称卡片行为正确。"""
	errors = []
	cmake = re.sub(r"#[^\n]*", "", _read(repo_root, Path("CMakeLists.txt")))
	registered = set(re.findall(r"(?<![\w/])QmUi/cards/([\w]+\.cpp)(?![\w.])", cmake))
	required = {"QmCardCatalog.cpp", "QmCardCatalogIds.cpp", "QmCardCatalogStandard.cpp", "QmCardCatalogTClient.cpp", "QmCardRenderBridge.cpp", "QmCardCatalogControls.cpp", *CATALOG_PAGE_SOURCES.values()}
	actual = {path.name for path in (repo_root / _CARDS_DIRECTORY).glob("*.cpp")}
	for name in sorted(required | actual | registered):
		if name not in actual:
			errors.append(f"catalog: {name}: source missing")
		if name not in registered:
			errors.append(f"catalog: {name}: CMake registration missing")
	return errors


def audit_shared_contracts(repo_root: Path) -> list[str]:
	errors: list[str] = []
	for relative in _TYPOGRAPHY_SOURCES:
		if "Ui()->DoDropDown(" in _read(repo_root, relative):
			errors.append(f"{relative}: settings dropdown bypasses CMenus::DoSettingsDropDown")

	for relative in _TYPOGRAPHY_SOURCES:
		source = _read(repo_root, relative)
		if "SetScrollProfile(EQmScrollProfile::GRID)" in source:
			errors.append(f"{relative}: settings grid still uses generic GRID profile")
		if re.search(r"\w*ColorPickerLineSize\s*=\s*\w+Metrics\.m_LineHeight\s*\+\s*\w+Metrics\.m_LineSpacing", source):
			errors.append(f"{relative}: color picker control height still includes row spacing")
		for line_number, argument in _find_legacy_color_picker_geometry(source):
			errors.append(f"{relative}:{line_number}: settings color row still uses scalar geometry ({argument})")
		for line_number in _find_raw_font_literals(source):
			errors.append(f"{relative}:{line_number}: raw settings font literal is not allowlisted")
		for line_number, argument in _find_rect_derived_font_arguments(source):
			errors.append(f"{relative}:{line_number}: settings font still derives from control geometry ({argument})")
	for relative in _FONT_ASSIGNMENT_SOURCES:
		for line_number, argument in _find_rect_derived_font_assignments(_read(repo_root, relative)):
			errors.append(f"{relative}:{line_number}: settings font assignment still derives from control geometry ({argument})")
	return errors


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	group = parser.add_mutually_exclusive_group(required=True)
	group.add_argument("--page", choices=tuple(PAGE_STABLE_IDS))
	group.add_argument("--all", action="store_true")
	parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
	args = parser.parse_args()

	pages = tuple(PAGE_STABLE_IDS) if args.all else (args.page,)
	errors = [error for page in pages for error in audit_page(args.repo_root, page)]
	errors.extend(audit_shared_contracts(args.repo_root))
	errors.extend(audit_catalog_build_contracts(args.repo_root))
	if errors:
		print("P5 设置页迁移结构清单失败：")
		for error in errors:
			print(f"- {error}")
		return 1
	for page in pages:
		print(f"{page}: clean")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
