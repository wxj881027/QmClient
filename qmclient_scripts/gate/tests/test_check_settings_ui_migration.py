from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from qmclient_scripts.gate.check_settings_ui_migration import (
	CATALOG_PAGE_SOURCES, PAGE_CATALOGUE_LIST, PAGE_FUNCTIONS, PAGE_ROUTE_TABS,
	PAGE_STABLE_IDS, _CATALOGUE_LIST_STATICS, _DEFAULT_SOURCE, _PAGE_SOURCE,
	_contains_forbidden_token, _find_legacy_color_picker_geometry, _find_raw_font_literals,
	_find_rect_derived_font_arguments, _find_rect_derived_font_assignments,
	audit_catalog_build_contracts, audit_page, audit_shared_contracts,
)


class SettingsUiMigrationAuditTest(unittest.TestCase):
	def setUp(self):
		self.temp_dir = TemporaryDirectory()
		self.addCleanup(self.temp_dir.cleanup)
		self.root = Path(self.temp_dir.name)

	def write(self, relative, content):
		path = self.root / relative
		path.parent.mkdir(parents=True, exist_ok=True)
		path.write_text(content, encoding="utf-8")
		return path

	def replace(self, relative, before, after):
		path = self.root / relative
		source = path.read_text(encoding="utf-8")
		self.assertIn(before, source)
		path.write_text(source.replace(before, after), encoding="utf-8")

	def make_page(self, page):
		# 这些是校验器的最小输入，不复制 UI 算法或声称验证 C++ 运行时行为。
		tabs = PAGE_ROUTE_TABS[page]
		if page in CATALOG_PAGE_SOURCES:
			if len(tabs) > 1:
				body = 'const char *s_apTabs[] = {' + ', '.join(f'"{tab}"' for tab in tabs) + '};\nRenderSettingsCatalogPage(MainView, s_apTabs[Index]);'
			else:
				body = f'RenderSettingsCatalogPage(MainView, "{tabs[0]}");'
			self.write("src/game/client/QmUi/cards/" + CATALOG_PAGE_SOURCES[page], "// 独立编译单元\n")
		elif page in PAGE_CATALOGUE_LIST:
			list_name = PAGE_CATALOGUE_LIST[page]
			body = f"qm_card_catalog::BuildCards(Ctx, qm_card_catalog::{list_name}(), Cards);"
			static = _CATALOGUE_LIST_STATICS[list_name]
			ids = ', '.join(f'"{stable_id}"' for stable_id in PAGE_STABLE_IDS[page])
			self.write("src/game/client/QmUi/cards/QmCardCatalogIds.cpp", f"const auto {static} = {{{ids}}};\nconst auto &{list_name}() {{ return {static}; }}")
		else:
			body = ""
		source = "\n".join(f"void {symbol if '(' in symbol else symbol + '()'} {{ {body} }}" for symbol in PAGE_FUNCTIONS[page])
		self.write(_PAGE_SOURCE.get(page, _DEFAULT_SOURCE), source)
		entries = ',\n'.join(f'{{"{stable_id}", "{tabs[0] if tabs else "search"}", ECardColumn::Left, 0}}' for stable_id in PAGE_STABLE_IDS[page])
		self.write("src/game/client/QmUi/QmCardRegistry.cpp", "std::vector<SCardDefault> Cards = {" + entries + "};")
		routes = "\n".join(f'if(str_comp(pTab, "{tab}") == 0) return true;' for tab in tabs)
		self.write("src/game/client/components/menus.cpp", "bool CMenus::SetSettingsPageFromCardTab(const char *pTab) {" + routes + "return false;}")
		return self.root

	def test_every_manifest_page_accepts_its_current_structural_boundary(self):
		for page in PAGE_STABLE_IDS:
			with self.subTest(page=page):
				self.assertEqual(audit_page(self.make_page(page), page), [])

	def test_thin_tee7_entry_does_not_need_moved_render_helpers(self):
		self.assertEqual(audit_page(self.make_page("tee7"), "tee7"), [])

	def test_render_body_details_are_not_positive_runtime_contracts(self):
		root = self.make_page("graphics")
		self.assertEqual(audit_page(root, "graphics"), [])
		self.write("src/game/client/QmUi/cards/QmCardCatalogGraphics.cpp", "void AnyImplementation() {}")
		self.assertEqual(audit_page(root, "graphics"), [])

	def test_missing_entry_definition_is_rejected(self):
		self.make_page("tee7")
		self.write(_PAGE_SOURCE["tee7"], "void CMenus::RenderSettingsTee7(CUIRect MainView);\nvoid Other() {}")
		self.assertTrue(any("entry definition missing" in error for error in audit_page(self.root, "tee7")))

	def test_comment_cannot_supply_page_definition(self):
		self.make_page("graphics")
		self.write(_DEFAULT_SOURCE, '// void CMenus::RenderSettingsGraphics() { RenderSettingsCatalogPage(MainView, "graphics"); }')
		self.assertTrue(any("entry definition missing" in error for error in audit_page(self.root, "graphics")))

	def test_missing_delegation_is_rejected(self):
		self.make_page("graphics")
		self.replace(_DEFAULT_SOURCE, 'RenderSettingsCatalogPage(MainView, "graphics");', '// RenderSettingsCatalogPage(MainView, "graphics");\n')
		self.assertTrue(any("delegation missing" in error for error in audit_page(self.root, "graphics")))

	def test_unrelated_tab_literal_cannot_hide_wrong_delegate_route(self):
		self.make_page("graphics")
		self.replace(_DEFAULT_SOURCE, 'RenderSettingsCatalogPage(MainView, "graphics");', 'const char *pUnrelated = "graphics"; RenderSettingsCatalogPage(MainView, "sound");')
		self.assertTrue(any("catalog page route missing" in error for error in audit_page(self.root, "graphics")))

	def test_appearance_requires_all_delegated_subtabs(self):
		self.make_page("appearance")
		self.replace(_DEFAULT_SOURCE, '"appearance-chat"', '"unrelated"')
		self.assertTrue(any("appearance-chat: catalog page route missing" in error for error in audit_page(self.root, "appearance")))

	def test_missing_producer_file_is_rejected(self):
		self.make_page("player")
		self.write("src/game/client/QmUi/cards/QmCardCatalogPlayer.cpp", "")
		self.assertTrue(any("producer source missing" in error for error in audit_page(self.root, "player")))

	def test_registry_card_must_belong_to_its_catalog_page(self):
		self.make_page("sound")
		self.replace("src/game/client/QmUi/QmCardRegistry.cpp", '"sound"', '"graphics"')
		self.assertTrue(any("category mismatch" in error for error in audit_page(self.root, "sound")))

	def test_missing_registry_card_is_rejected_and_restoring_it_recovers(self):
		self.make_page("player")
		path = self.root / "src/game/client/QmUi/QmCardRegistry.cpp"
		original = path.read_text(encoding="utf-8")
		self.replace(path.relative_to(self.root), '"deck:player-country"', '"unrelated"')
		self.assertTrue(any("deck:player-country: registry entry missing" in error for error in audit_page(self.root, "player")))
		path.write_text(original, encoding="utf-8")
		self.assertEqual(audit_page(self.root, "player"), [])

	def test_duplicate_registry_card_is_rejected(self):
		self.make_page("player")
		self.replace("src/game/client/QmUi/QmCardRegistry.cpp", 'Cards = {', 'Cards = {{"deck:player-country", "player", 0},')
		self.assertTrue(any("deck:player-country: registry entry missing or duplicated" in error for error in audit_page(self.root, "player")))

	def test_comment_and_unrelated_string_are_not_registry_entries(self):
		self.make_page("player")
		self.write("src/game/client/QmUi/QmCardRegistry.cpp", '// Cards = {{"deck:player-identity", "player", 0}};\nconst char *pText = "deck:player-country";')
		self.assertTrue(any("registry entry missing" in error for error in audit_page(self.root, "player")))

	def test_explicit_category_requires_its_accessor_and_card(self):
		self.make_page("qmclient_visual")
		self.replace("src/game/client/QmUi/cards/QmCardCatalogIds.cpp", '"qm:skin_transition"', '"unrelated"')
		self.assertTrue(any("qm:skin_transition: catalogue category entry missing" in error for error in audit_page(self.root, "qmclient_visual")))

	def test_explicit_category_accessor_cannot_return_another_list(self):
		self.make_page("general")
		self.replace("src/game/client/QmUi/cards/QmCardCatalogIds.cpp", "return s_vGeneralCards;", "return s_vOtherCards;")
		self.assertTrue(any("catalogue category entry missing" in error for error in audit_page(self.root, "general")))

	def test_duplicate_explicit_category_card_is_rejected(self):
		self.make_page("general")
		self.replace("src/game/client/QmUi/cards/QmCardCatalogIds.cpp", '"deck:general-game"', '"deck:general-game", "deck:general-game"')
		self.assertTrue(any("catalogue category entry missing or duplicated" in error for error in audit_page(self.root, "general")))

	def test_missing_explicit_category_delegation_is_rejected(self):
		self.make_page("controls")
		self.replace(_PAGE_SOURCE["controls"], "qm_card_catalog::ControlsCardStableIds()", "OtherCards()")
		self.assertTrue(any("category delegation missing" in error for error in audit_page(self.root, "controls")))

	def test_warlist_single_registry_card_is_the_migration_terminal_state(self):
		self.assertEqual(audit_page(self.make_page("tclient_warlist"), "tclient_warlist"), [])
		self.replace("src/game/client/QmUi/QmCardRegistry.cpp", 'Cards = {', 'Cards = {{"deck:tclient-warlist-editor", "tclient-warlist", 0},')
		self.assertTrue(any("legacy registry" in error for error in audit_page(self.root, "tclient_warlist")))

	def test_route_literal_outside_navigation_function_cannot_supply_route(self):
		self.make_page("general")
		self.write("src/game/client/components/menus.cpp", 'const char *pText = "general";')
		self.assertTrue(any("registry/navigation entry missing" in error for error in audit_page(self.root, "general")))

	def test_legacy_card_entry_is_rejected(self):
		self.make_page("general")
		self.replace(_DEFAULT_SOURCE, "qm_card_catalog::BuildCards", "BeginSettingsCardDeck(); qm_card_catalog::BuildCards")
		self.assertTrue(any("legacy path remains" in error for error in audit_page(self.root, "general")))

	def test_existing_tee_sdf_exception_is_not_expanded(self):
		allowed = "Ui()->DoEditBox(&ColorCodeInput, &ColorCodeEditBox, std::max(10.0f, BodySize * 0.85f), IGraphics::CORNER_ALL, {}, TEXTALIGN_MC)"
		self.assertFalse(_contains_forbidden_token("tee", allowed, "Ui()->DoEditBox("))
		self.assertTrue(_contains_forbidden_token("tee", allowed + "; Ui()->DoEditBox(&Other);", "Ui()->DoEditBox("))

	def make_build_contract(self):
		names = {"QmCardCatalog.cpp", "QmCardCatalogIds.cpp", "QmCardCatalogStandard.cpp", "QmCardCatalogTClient.cpp", "QmCardRenderBridge.cpp", "QmCardCatalogControls.cpp", *CATALOG_PAGE_SOURCES.values()}
		for name in names:
			self.write("src/game/client/QmUi/cards/" + name, "// 编译单元\n")
		self.write("CMakeLists.txt", "set(CLIENT_SRC\n" + "\n".join("QmUi/cards/" + name for name in sorted(names)) + "\n)")

	def test_catalog_sources_require_cmake_registration_and_recover(self):
		self.make_build_contract()
		self.assertEqual(audit_catalog_build_contracts(self.root), [])
		self.replace("CMakeLists.txt", "QmUi/cards/QmCardCatalogPlayer.cpp", "# QmUi/cards/QmCardCatalogPlayer.cpp")
		self.assertIn("catalog: QmCardCatalogPlayer.cpp: CMake registration missing", audit_catalog_build_contracts(self.root))
		self.replace("CMakeLists.txt", "# QmUi/cards/QmCardCatalogPlayer.cpp", "QmUi/cards/QmCardCatalogPlayer.cpp")
		self.assertEqual(audit_catalog_build_contracts(self.root), [])

	def test_cmake_entry_without_source_is_rejected(self):
		self.make_build_contract()
		self.replace("CMakeLists.txt", "set(CLIENT_SRC", "set(CLIENT_SRC\nQmUi/cards/Removed.cpp")
		self.assertIn("catalog: Removed.cpp: source missing", audit_catalog_build_contracts(self.root))

	def test_new_catalog_unit_cannot_remain_unregistered(self):
		self.make_build_contract()
		self.write("src/game/client/QmUi/cards/NewCard.cpp", "void NewCard() {}")
		self.assertIn("catalog: NewCard.cpp: CMake registration missing", audit_catalog_build_contracts(self.root))

	def test_moved_producer_cannot_reintroduce_legacy_card_api(self):
		self.make_page("graphics")
		self.write("src/game/client/QmUi/cards/QmCardCatalogGraphics.cpp", "void Build() { BeginSettingsCardDeck(); }")
		self.assertTrue(any("legacy path remains" in error for error in audit_page(self.root, "graphics")))

	def test_documented_legacy_api_is_not_an_active_call(self):
		self.make_page("graphics")
		self.write("src/game/client/QmUi/cards/QmCardCatalogGraphics.cpp", '// BeginSettingsCardDeck();\nconst char *pDoc = "BeginSettingsCardDeck()";')
		self.assertEqual(audit_page(self.root, "graphics"), [])

	def test_catalog_builder_name_is_not_the_removed_settings_card_api(self):
		self.assertFalse(_contains_forbidden_token("controls", "Controls.BuildSettingsCard(Ctx, Id, Out);", "SettingsCard("))
		self.assertTrue(_contains_forbidden_token("controls", "SettingsCard(Ctx);", "SettingsCard("))

	def test_unknown_page_is_rejected(self):
		with self.assertRaises(ValueError):
			audit_page(self.root, "unknown")

	def test_scalar_color_picker_call_is_rejected(self):
		self.assertEqual(_find_legacy_color_picker_geometry("DoLine_ColorPicker(&Reset, LineHeight, BodySize, LineSpacing, &View, Text, &Color, Default);"), [(1, "LineHeight")])
		self.assertEqual(_find_legacy_color_picker_geometry("const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(640.0f);\nDoLine_ColorPicker(&Reset, Metrics, &View, Text, &Color, Default);"), [])
		self.assertEqual(_find_legacy_color_picker_geometry("DoLine_ColorPicker(&Reset, CurrentSettingsContentMetrics(), &View, Text, &Color, Default);"), [])

	def test_multiline_scalar_color_picker_call_is_rejected(self):
		source = """const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(640.0f);
DoLine_ColorPicker(
    &Reset,
    LineHeight,
    BodySize,
    LineSpacing,
    &View,
    Text,
    &Color,
    Default);
DoLine_ColorPicker(
    &Reset,
    Metrics,
    &View,
    Text,
    &Color,
    Default);
"""
		self.assertEqual(_find_legacy_color_picker_geometry(source), [(2, "LineHeight")])

	def test_undeclared_metrics_lookalike_is_rejected(self):
		self.assertEqual(_find_legacy_color_picker_geometry("DoLine_ColorPicker(&Reset, FakeMetrics, &View, Text, &Color, Default);"), [(1, "FakeMetrics")])

	def test_comment_and_string_metrics_declarations_are_rejected(self):
		comment = "// SSettingsContentMetrics FakeMetrics;\nDoLine_ColorPicker(&Reset, FakeMetrics, &View, Text, &Color, Default);"
		string = 'const char *pText = "SSettingsContentMetrics FakeMetrics;";\nDoLine_ColorPicker(&Reset, FakeMetrics, &View, Text, &Color, Default);'
		self.assertEqual(_find_legacy_color_picker_geometry(comment), [(2, "FakeMetrics")])
		self.assertEqual(_find_legacy_color_picker_geometry(string), [(2, "FakeMetrics")])

	def test_scalar_shadow_after_metrics_declaration_is_rejected(self):
		source = """SSettingsContentMetrics Metrics;
{
    float Metrics = 0.0f;
    DoLine_ColorPicker(&Reset, Metrics, &View, Text, &Color, Default);
}
"""
		self.assertEqual(_find_legacy_color_picker_geometry(source), [(4, "Metrics")])

	def test_nested_brace_and_bracket_commas_do_not_shift_second_argument(self):
		source = """DoLine_ColorPicker(
    BuildResetId(std::array<int, 2>{1, 2}[0]),
    LineHeight,
    BodySize,
    LineSpacing,
    &View,
    Text,
    &Color,
    Default);
"""
		self.assertEqual(_find_legacy_color_picker_geometry(source), [(1, "LineHeight")])

	def test_template_commas_do_not_shift_second_argument(self):
		source = "DoLine_ColorPicker(BuildResetId<std::array<int, 2>>(), Metrics, &View, Text, &Color, Default);"
		self.assertEqual(_find_legacy_color_picker_geometry("SSettingsContentMetrics Metrics;\n" + source), [])

	def test_spaced_template_commas_do_not_shift_second_argument(self):
		source = "DoLine_ColorPicker(BuildResetId <1, 2>(), Metrics, &View, Text, &Color, Default);"
		self.assertEqual(_find_legacy_color_picker_geometry("SSettingsContentMetrics Metrics;\n" + source), [])

	def test_comparison_operator_does_not_hide_second_argument(self):
		source = "SSettingsContentMetrics Metrics;\nDoLine_ColorPicker(Left < Right, Metrics, &View, Text, &Color, Default);"
		self.assertEqual(_find_legacy_color_picker_geometry(source), [])

	def test_raw_strings_cannot_declare_fake_metrics_or_fonts(self):
		source = '''const char *pText = R"tag(SSettingsContentMetrics FakeMetrics; "quoted" DoLabel(&View, Text, 14.0f, Align);)tag";
DoLine_ColorPicker(&Reset, FakeMetrics, &View, Text, &Color, Default);'''
		self.assertEqual(_find_legacy_color_picker_geometry(source), [(2, "FakeMetrics")])
		self.assertEqual(_find_raw_font_literals(source), [])

	def test_multiline_raw_font_literal_is_rejected(self):
		source = """Ui()->DoLabel(
    &View,
    Text,
    14.0f,
    TEXTALIGN_ML);"""
		self.assertEqual(_find_raw_font_literals(source), [1])

	def test_rect_derived_label_font_is_rejected(self):
		self.assertEqual(_find_rect_derived_font_arguments("Ui()->DoLabel(&Text, Label, Text.h * 0.8f, TEXTALIGN_MC);"), [(1, "Text.h * 0.8f")])
		self.assertEqual(_find_rect_derived_font_arguments("DoSettingsLabel(Page, Tab, Subtab, &Text, Label, Text.h * 0.8f, Align);"), [(1, "Text.h * 0.8f")])
		self.assertEqual(_find_rect_derived_font_arguments("DoSettingsMenuLabel(Page, Tab, Subtab, Id, &Text, Label, Text.h * 0.8f, Align);"), [(1, "Text.h * 0.8f")])
		self.assertEqual(_find_rect_derived_font_arguments("Ui()->DoLabel(&Text, Label, CurrentSettingsContentMetrics().m_BodySize, TEXTALIGN_MC);"), [])

	def test_rect_derived_font_detection_ignores_comments_and_strings(self):
		source = '// Ui()->DoLabel(&Text, Label, Text.h * 0.8f, TEXTALIGN_MC);\nconst char *pText = "DoLabel(&Text, Label, Rect.h * 0.8f, Align)";'
		self.assertEqual(_find_rect_derived_font_arguments(source), [])

	def test_rect_derived_font_assignment_rejects_nested_and_arbitrary_rect_names(self):
		source = '''
Options.m_FontSize = std::min(BodySize, ControlColumn.h * 0.8f);
Props.m_FontSize = Rect.h * 0.8f;
Style.m_FontSize = (pControl->h - Padding) * 0.8f;
Clean.m_FontSize = Metrics.m_BodySize;
'''
		self.assertEqual(
			_find_rect_derived_font_assignments(source),
			[(2, "std::min(BodySize, ControlColumn.h * 0.8f)"), (3, "Rect.h * 0.8f"), (4, "(pControl->h - Padding) * 0.8f")],
		)

	def test_rect_derived_font_assignment_ignores_comments_and_strings(self):
		source = '// Options.m_FontSize = Rect.h * 0.8f;\nconst char *pText = "Props.m_FontSize = Other.h;";'
		self.assertEqual(_find_rect_derived_font_assignments(source), [])

	def test_rect_derived_font_assignment_ignores_comparisons(self):
		self.assertEqual(_find_rect_derived_font_assignments("Props.m_FontSize == Rect.h;"), [])

	def test_shared_contract_rejects_legacy_api_but_not_font_propagation_details(self):
		self.assertEqual(audit_shared_contracts(self.root), [])
		self.write("src/game/client/components/qmclient/menus_qmclient.cpp", "Ui()->DoDropDown();")
		self.assertTrue(any("dropdown bypasses" in error for error in audit_shared_contracts(self.root)))
		self.write("src/game/client/components/qmclient/menus_qmclient.cpp", "")
		self.write("src/game/client/ui_popups.cpp", "void ArbitraryPopupImplementation() {}")
		self.assertEqual(audit_shared_contracts(self.root), [])


if __name__ == "__main__":
	unittest.main()
