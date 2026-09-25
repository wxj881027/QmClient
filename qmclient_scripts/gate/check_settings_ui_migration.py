#!/usr/bin/env python3
"""Audit whether P5 settings pages use the unified UI stack exclusively."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


PAGE_STABLE_IDS = {
	"general": ("deck:general-game", "deck:general-language", "deck:general-client", "deck:general-recording"),
	"player": ("deck:player-identity", "deck:player-country"),
	"tee": ("deck:tee-identity", "deck:tee-skin-options", "deck:tee-skin-list"),
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
		"qm:skin_appearance",
		"qm:skin_transition",
		"qm:weapon_animation",
		"qm:entity_overlay",
		"qm:collision_hitbox",
		"qm:streamer",
	),
	"contributors": ("deck:qmclient-contributors-community", "deck:qmclient-contributors-sponsors"),
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
	"tclient_info": ("deck:tclient-info-links", "deck:tclient-info-files", "deck:tclient-info-developers", "deck:tclient-info-tabs"),
	"tclient_profiles": ("deck:tclient-profiles-actions", "deck:tclient-profiles-options", "deck:tclient-profiles-list"),
	"tclient_configs": ("deck:tclient-configs-actions",),
	"assets": (),
}

PAGE_FUNCTIONS = {
	"general": ("CMenus::RenderSettingsGeneral",),
	"player": ("CMenus::RenderSettingsTeeIdentity", "CMenus::RenderSettingsPlayer"),
	"tee": ("CMenus::RenderSettingsTee(CUIRect MainView)",),
	"tee7": ("CMenus::RenderSettingsTee7(CUIRect MainView)", "CMenus::RenderSettingsTee7Content"),
	"graphics": ("CMenus::RenderSettingsGraphics",),
	"sound": ("CMenus::RenderSettingsSound",),
	"ddnet": ("CMenus::RenderSettingsDDNet",),
	"appearance": ("CMenus::RenderSettingsAppearance",),
	"controls": ("CMenusSettingsControls::Render",),
	"qmclient_hud": ("CMenus::RenderSettingsQmClientHudDeck",),
	"qmclient_function": ("CMenus::RenderSettingsQmClientFunctionDeck",),
	"qmclient_visual": ("CMenus::RenderSettingsQmClientVisualDeck",),
	"contributors": ("CMenus::RenderSettingsQmClientContributors",),
	"global_search": ("CMenus::RenderSettingsGlobalSearchContent",),
	"tclient": ("CMenus::RenderSettingsTClientSettings",),
	"tclient_bind_wheel": ("CMenus::RenderSettingsTClientBindWheel",),
	"tclient_chat_binds": ("CMenus::RenderSettingsTClientChatBinds",),
	"tclient_warlist": ("CMenus::RenderSettingsTClientWarList",),
	"tclient_status_bar": ("CMenus::RenderSettingsTClientStatusBar",),
	"tclient_info": ("CMenus::RenderSettingsTClientInfo",),
	"tclient_profiles": ("CMenus::RenderSettingsTClientProfiles",),
	"tclient_configs": ("CMenus::RenderSettingsTClientConfigs",),
	"assets": ("CMenus::RenderSettingsCustom",),
}

PRODUCER_COMPLETE_PAGES = {
	"appearance",
	"qmclient_hud",
	"qmclient_function",
	"qmclient_visual",
	"contributors",
	"tclient_configs",
	"tclient_warlist",
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
	"tclient_info": ("tclient-info",),
	"tclient_profiles": ("tclient-profiles",),
	"tclient_configs": ("tclient-configs",),
	"assets": (),
}

COMMON_REQUIRED = (
	"SettingsPageLayout(",
	"SSettingsPageLayoutFrame",
	"SSettingsCardDefinition",
	"QmResolveScrollPolicy(",
	"EQmScrollProfile::SETTINGS_OUTER",
)
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
PAGE_REQUIRED = {
	"general": ("SettingsCardDeckForRenderPass().RenderCached(", "ui_widget::NumericField("),
	"player": ("SettingsCardDeckForRenderPass().RenderCached(", "ui_widget::InputField("),
	"tee": ("SettingsCardDeckForRenderPass().RenderCached(", "ui_widget::InputField(", "ui_widget::NumericField("),
	"tee7": ("SettingsCardDeckForRenderPass().RenderCached(", "ui_widget::InputField(", "EQmScrollProfile::SETTINGS_GRID"),
	"graphics": ("SettingsCardDeckForRenderPass().RenderCached(", "ui_widget::NumericField("),
	"sound": ("SettingsCardDeckForRenderPass().RenderCached(", "ui_widget::NumericField("),
	"ddnet": ("SettingsCardDeckForRenderPass().RenderCached(", "ui_widget::InputField(", "ui_widget::NumericField("),
	"appearance": ("SettingsCardDeckForRenderPass().RenderCached(", "ui_widget::NumericField("),
	"controls": ("SettingsCardDeckForRenderPass().RenderCached(", "SettingsCardOrderModelForRenderPass()", "ui_widget::InputField(", "ui_widget::NumericField("),
	"qmclient_hud": ("CardDeck.RenderCached(", "ResolveSettingsCardDefinitionsRevision("),
	"qmclient_function": ("CardDeck.RenderCached(", "ResolveSettingsCardDefinitionsRevision("),
	"qmclient_visual": ("CardDeck.RenderCached(", "ResolveSettingsCardDefinitionsRevision("),
	"contributors": ("CardDeck.RenderCached(",),
	"global_search": ("CardDeck.RenderCached(",),
	"tclient": ('RenderCached(SettingsUiContext("settings_tclient_main"',),
	"tclient_bind_wheel": ("CardDeck.RenderCached(",),
	"tclient_chat_binds": ("CardDeck.RenderCached(",),
	"tclient_warlist": ("CardDeck.RenderCached(",),
	"tclient_status_bar": ("CardDeck.RenderCached(",),
	"tclient_info": ("CardDeck.RenderCached(",),
	"tclient_profiles": ("CardDeck.RenderCached(",),
	"tclient_configs": ("CardDeck.RenderCached(",),
	"assets": (
		"ResolveSettingsContentMetrics(",
		"s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_OUTER)",
		"s_WorkshopAssetsListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_OUTER)",
	),
}
PAGE_METRICS_REQUIRED = {
	"controls": "ApplyControlsContentMetrics(MainView.w)",
	"tclient": "ApplyTClientContentMetrics(MainView.w)",
	"tclient_bind_wheel": "ApplyTClientContentMetrics(MainView.w)",
	"tclient_chat_binds": "ApplyTClientContentMetrics(MainView.w)",
	"tclient_warlist": "ApplyTClientContentMetrics(MainView.w)",
	"tclient_status_bar": "ApplyTClientContentMetrics(MainView.w)",
	"tclient_info": "ApplyTClientContentMetrics(MainView.w)",
	"tclient_profiles": "ApplyTClientContentMetrics(MainView.w)",
	"tclient_configs": "ApplyTClientContentMetrics(MainView.w)",
}
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

PAGE_PRODUCER_REQUIRED = {
	"tclient_warlist": ('m_Spec = {"deck:tclient-warlist"',),
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
	"contributors": Path("src/game/client/components/qmclient/menus_qmclient.cpp"),
	"global_search": Path("src/game/client/components/qmclient/menus_qmclient.cpp"),
	"tclient": Path("src/game/client/components/tclient/menus_tclient.cpp"),
	"tclient_bind_wheel": Path("src/game/client/components/tclient/menus_tclient.cpp"),
	"tclient_chat_binds": Path("src/game/client/components/tclient/menus_tclient.cpp"),
	"tclient_warlist": Path("src/game/client/components/tclient/menus_tclient.cpp"),
	"tclient_status_bar": Path("src/game/client/components/tclient/menus_tclient.cpp"),
	"tclient_info": Path("src/game/client/components/tclient/menus_tclient.cpp"),
	"tclient_profiles": Path("src/game/client/components/tclient/menus_tclient.cpp"),
	"tclient_configs": Path("src/game/client/components/tclient/menus_tclient.cpp"),
	"assets": Path("src/game/client/components/menus_settings_assets.cpp"),
}
_DEFAULT_SOURCE = Path("src/game/client/components/menus_settings.cpp")
# 全局卡片目录：栖梦三个分类的卡片 stableId/构造入口登记在这里（页面只声明"这一页有哪些卡"）。
_CARD_CATALOG_SOURCES = (
	Path("src/game/client/QmUi/cards/QmCardCatalog.cpp"),
	Path("src/game/client/QmUi/cards/QmCardCatalogVisual.cpp"),
	Path("src/game/client/QmUi/cards/QmCardCatalogSkin.cpp"),
	Path("src/game/client/QmUi/cards/QmCardCatalogFunction.cpp"),
	Path("src/game/client/QmUi/cards/QmCardCatalogHud.cpp"),
)
_REGISTRY_SOURCE = Path("src/game/client/QmUi/QmCardRegistry.cpp")
_NAVIGATION_SOURCE = Path("src/game/client/components/menus.cpp")
# 卡片生产的归属：页面声明「这一页有哪些卡片」，具体生产在全局卡片目录的分类模块里（N3）。
_CATALOGUE_SOURCE = Path("src/game/client/QmUi/cards/QmCardCatalogIds.cpp")
PAGE_CATALOGUE_LIST = {
	"qmclient_hud": "HudCardStableIds",
	"qmclient_function": "FunctionCardStableIds",
	"qmclient_visual": "VisualCardStableIds",
}
_CATALOGUE_LIST_STATICS = {
	"HudCardStableIds": "s_vHudCards",
	"FunctionCardStableIds": "s_vFunctionCards",
	"VisualCardStableIds": "s_vVisualCards",
}


def _catalogue_list_contains(root: Path, list_name: str, stable_id: str) -> bool:
	"""该 stableId 是否出现在目录源码对应分类清单的字面量中。

	N3 之后卡片不再由页面逐个 AddCard 生产，「每张期望卡片都真的被本页生产」这一不变量
	改由「页面调用对应分类清单 + 该 stableId 确在该清单字面量内」两段共同保证。
	"""
	static_name = _CATALOGUE_LIST_STATICS.get(list_name)
	if static_name is None:
		return False
	source = _read(root, _CATALOGUE_SOURCE)
	marker = f"{static_name} = {{"
	start = source.find(marker)
	if start == -1:
		return False
	end = source.find("};", start)
	if end == -1:
		return False
	return f'"{stable_id}"' in source[start:end]

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
PAGE_CONTRACT_HELPERS = {
	"controls": ("ApplyControlsContentMetrics", "CMenusSettingsControls::DoSettingsControlsNumericField"),
	"tee7": ("CMenus::RenderSkinSelection7", "CMenus::RenderSkinPartSelection7"),
}


def _extract_function_body(source: str, symbol: str) -> str | None:
	"""Return a C++ function body using a small brace-aware scanner."""
	symbol_pos = source.find(symbol)
	if symbol_pos < 0:
		return None
	open_brace = source.find("{", symbol_pos)
	if open_brace < 0:
		return None

	depth = 0
	index = open_brace
	quote = ""
	line_comment = False
	block_comment = False
	while index < len(source):
		char = source[index]
		next_char = source[index + 1] if index + 1 < len(source) else ""
		if line_comment:
			if char == "\n":
				line_comment = False
		elif block_comment:
			if char == "*" and next_char == "/":
				block_comment = False
				index += 1
		elif quote:
			if char == "\\":
				index += 1
			elif char == quote:
				quote = ""
		elif char == "/" and next_char == "/":
			line_comment = True
			index += 1
		elif char == "/" and next_char == "*":
			block_comment = True
			index += 1
		elif char in ('"', "'"):
			quote = char
		elif char == "{":
			depth += 1
		elif char == "}":
			depth -= 1
			if depth == 0:
				return source[symbol_pos : index + 1]
		index += 1
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
	return token in remaining_source


def audit_page(repo_root: Path, page: str) -> list[str]:
	if page not in PAGE_STABLE_IDS:
		raise ValueError(f"unknown settings page: {page}")

	errors: list[str] = []
	source = _read(repo_root, _PAGE_SOURCE.get(page, _DEFAULT_SOURCE))
	bodies: list[str] = []
	for symbol in PAGE_FUNCTIONS[page]:
		body = _extract_function_body(source, symbol)
		if body is None:
			errors.append(f"{page}: {symbol}: function body missing")
		else:
			bodies.append(body)
	page_source = "\n".join(bodies)
	for helper_symbol in PAGE_CONTRACT_HELPERS.get(page, ()):
		# 只允许明确列出的 helper 承载入口函数委托的组件契约。
		helper = _extract_function_body(source, helper_symbol)
		if helper is None:
			errors.append(f"{page}: {helper_symbol}: function body missing")
		else:
			page_source += "\n" + helper

	required = PAGE_REQUIRED[page] if page == "assets" else COMMON_REQUIRED + PAGE_REQUIRED[page]
	# 全局搜索只用 Deck 的绘制副作用，结果由搜索缓存持有；其他卡片页仍消费返回值。
	if page not in ("assets", "global_search"):
		required += ("SSettingsCardDeckResult",)
	metrics_required = PAGE_METRICS_REQUIRED.get(page, "ResolveSettingsContentMetrics(")
	required += (metrics_required,)
	for token in required:
		if token not in page_source:
			errors.append(f"{page}: {token}: required unified contract missing")
	if page == "assets":
		forbidden = PAGE_FORBIDDEN.get(page, ())
	elif page in STRICT_LEGACY_PAGES:
		forbidden = COMMON_FORBIDDEN + PAGE_FORBIDDEN.get(page, ())
	else:
		forbidden = DECK_LEGACY_FORBIDDEN + PAGE_FORBIDDEN.get(page, ())
	for token in forbidden:
		if _contains_forbidden_token(page, page_source, token):
			errors.append(f"{page}: {token}: legacy path remains")

	registry = _read(repo_root, _REGISTRY_SOURCE)
	navigation = _read(repo_root, _NAVIGATION_SOURCE)
	# 卡片构造已迁入全局卡片目录：生产者条目既可能写在页面文件里，也可能登记在卡片目录模块里。
	producer_source = page_source
	for catalog_path in _CARD_CATALOG_SOURCES:
		producer_source += "\n" + _read(repo_root, catalog_path)
	for stable_id in PAGE_STABLE_IDS[page]:
		if stable_id not in registry:
			errors.append(f"{page}: {stable_id}: registry/navigation entry missing")
		if page in PRODUCER_COMPLETE_PAGES and page not in PAGE_PRODUCER_REQUIRED:
			# 卡片生产已迁入全局卡片目录（N3）：页面只声明本页含有哪些分类。
			# 仍校验「该页每张期望卡片都真的被生产」，但拆成两段：页面调用对应分类清单，
			# 且该 stableId 确实在该清单字面量内——不是取消检查。
			catalogue_list = PAGE_CATALOGUE_LIST.get(page)
			if catalogue_list is None:
				if stable_id not in page_source:
					errors.append(f"{page}: {stable_id}: page producer entry missing")
			else:
				if f"qm_card_catalog::{catalogue_list}()" not in page_source:
					errors.append(f"{page}: qm_card_catalog::{catalogue_list}(): page producer entry missing")
				elif not _catalogue_list_contains(repo_root, catalogue_list, stable_id):
					errors.append(f"{page}: {stable_id}: catalogue category entry missing")
	for token in PAGE_PRODUCER_REQUIRED.get(page, ()):
		if token not in producer_source:
			errors.append(f"{page}: {token}: page producer entry missing")
	for token in REGISTRY_FORBIDDEN.get(page, ()):
		if token in registry:
			errors.append(f"{page}: {token}: legacy registry entry remains")
	for route in PAGE_ROUTE_TABS[page]:
		if not _navigation_has_route(navigation, route):
			errors.append(f"{page}: {route}: registry/navigation entry missing")
	return errors


def audit_shared_contracts(repo_root: Path) -> list[str]:
	errors: list[str] = []
	menu_source = _read(repo_root, _NAVIGATION_SOURCE)
	settings_shell_source = _read(repo_root, _DEFAULT_SOURCE)
	tclient_source = _read(repo_root, Path("src/game/client/components/tclient/menus_tclient.cpp"))
	# Dropdown trigger/popup rendering lives in ui_popups.cpp after the UI split.
	ui_source = "\n".join(
		_read(repo_root, relative)
		for relative in (
			Path("src/game/client/ui.cpp"),
			Path("src/game/client/ui_popups.cpp"),
		)
	)
	if "ResolveSettingsRadioRowLayout(" not in menu_source:
		errors.append("shared: responsive settings radio resolver missing")
	if "SettingsPageUiScale(pRect->w)" in menu_source:
		errors.append("shared: settings control font still derives from local rect width")
	if "SettingsPageUiScale(0.0f)" in menu_source:
		errors.append("shared: settings control font still derives from a synthetic zero width")
	if "CurrentSettingsContentMetrics().m_BodySize" not in menu_source:
		errors.append("shared: settings buttons do not consume the shell content metrics")
	if "float RowHeight, float RowSpacing, float BodySize" not in menu_source:
		errors.append("shared: streamed settings checkbox rows do not consume explicit metrics")
	if re.search(r"DoSettingsButton_CheckBoxAutoVMarginAndSet\([^;{]+float RowSpacing\s*=", menu_source) or re.search(r"DoSettingsButton_CheckBoxAutoVMarginAndSet\([^;{]+float BodySize\s*=", menu_source):
		errors.append("shared: streamed settings checkbox metrics still have implicit defaults")
	if "VMargin, 0.0f, FontSize" not in tclient_source:
		errors.append("tclient: streamed checkbox rows do not use the shared BodySize")
	if "Ui()->SetDropDownFontSize(m_SettingsContentMetrics.m_BodySize);" not in settings_shell_source:
		errors.append("shared: settings shell does not provide the BodySize dropdown context")
	if "Props.m_FontSize = ResolvedFontSize;" not in ui_source or "State.m_SelectionPopupContext.m_FontSize = ResolvedFontSize;" not in ui_source:
		errors.append("shared: dropdown trigger and popup do not inherit one resolved font size")
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
