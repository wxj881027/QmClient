from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from qmclient_scripts.gate.check_settings_ui_migration import _contains_forbidden_token, _find_legacy_color_picker_geometry, _find_raw_font_literals, _find_rect_derived_font_arguments, _find_rect_derived_font_assignments, PAGE_STABLE_IDS, PRODUCER_COMPLETE_PAGES, audit_page, audit_shared_contracts


class SettingsUiMigrationAuditTest(unittest.TestCase):
	def setUp(self):
		self.temp_dir = TemporaryDirectory()
		self.root = Path(self.temp_dir.name)

	def tearDown(self):
		self.temp_dir.cleanup()

	def make_repo(self, *, drop: str = "", add: str = "") -> Path:
		source = """void CMenus::RenderSettingsGeneral(CUIRect MainView)
{
    const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(MainView.w);
    const SSettingsPageLayoutFrame Frame = SettingsPageLayout(MainView, 1.0f);
    std::vector<SSettingsCardDefinition> vCards;
    vCards.push_back({{"deck:general-game", "General", nullptr}, MeasureGeneralGame, RenderGeneralGame});
    CScrollRegion ScrollRegion;
    CQmScrollState &Scroll = ScrollRegion.State();
    QmResolveScrollPolicy(Request, 1.0f, 0.1f);
    Request.m_Profile = EQmScrollProfile::SETTINGS_OUTER;
    ui_widget::NumericField(Context);
    const SSettingsCardDeckResult DeckResult = SettingsCardDeckForRenderPass().RenderCached(Context, Frame, "general", vCards, SettingsCardOrderModelForRenderPass(), &ScrollRegion, Input, SettingsCardMotionSpec(), 1);
}
"""
		source = source.replace("\n}\n", f"\n    {add}\n}}\n", 1).replace(drop, "")
		files = {
			"src/game/client/components/menus_settings.cpp": source,
			"src/game/client/components/menus.cpp": "",
			"src/game/client/QmUi/QmCardRegistry.cpp": "\n".join(PAGE_STABLE_IDS["general"]),
			"src/game/client/components/qmclient/menus_qmclient.cpp": """static constexpr SQmGlobalSearchTabRoute s_aGlobalSearchTabRoutes[] = {
    {"general", CMenus::SETTINGS_GENERAL},
};
SQmGlobalSearchNavigation ResolveGlobalSearchNavigation(const SQmGlobalSearchCard &Card)
{
    return {};
}
""",
		}
		files["src/game/client/components/menus.cpp"] += """
bool CMenus::SetSettingsPageFromCardTab(const char *pTab)
{
    if(str_comp(pTab, "general") == 0)
        return true;
    return false;
}
"""
		for relative, content in files.items():
			path = self.root / relative
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_text(content, encoding="utf-8")
		return self.root

	def make_warlist_repo(self, *, drop: str = "", add: str = "", registry_add: str = "") -> Path:
		source = """void CMenus::RenderSettingsTClientWarList(CUIRect MainView, bool PrewarmOnly)
{
    ApplyTClientContentMetrics(MainView.w);
    const SSettingsPageLayoutFrame Frame = SettingsPageLayout(MainView, 1.0f);
    std::vector<SSettingsCardDefinition> vCards;
    SSettingsCardDefinition Card;
    Card.m_Spec = {"deck:tclient-warlist", "War List", nullptr};
    vCards.push_back(std::move(Card));
    SSettingsCardDeckResult Result;
    QmResolveScrollPolicy(Request, 1.0f, 0.1f);
    Request.m_Profile = EQmScrollProfile::SETTINGS_OUTER;
    Result = CardDeck.RenderCached(Context, Frame, "tclient-warlist", vCards, Model, &ScrollRegion, Input, Motion, 1);
}
"""
		source = source.replace("\n}\n", f"\n    {add}\n}}\n", 1).replace(drop, "")
		files = {
			"src/game/client/components/tclient/menus_tclient.cpp": source,
			"src/game/client/QmUi/QmCardRegistry.cpp": '"deck:tclient-warlist"\n' + registry_add,
			"src/game/client/components/menus.cpp": """bool CMenus::SetSettingsPageFromCardTab(const char *pTab)
{
    return str_comp(pTab, "tclient-warlist") == 0;
}
""",
		}
		for relative, content in files.items():
			path = self.root / relative
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_text(content, encoding="utf-8")
		return self.root

	def make_shared_contract_repo(self) -> Path:
		files = {
			"src/game/client/components/menus.cpp": "ResolveSettingsRadioRowLayout(); CurrentSettingsContentMetrics().m_BodySize; float RowHeight, float RowSpacing, float BodySize;",
			"src/game/client/components/menus_settings.cpp": "Ui()->SetDropDownFontSize(m_SettingsContentMetrics.m_BodySize);",
			"src/game/client/components/qmclient/menus_qmclient.cpp": "",
			"src/game/client/components/tclient/menus_tclient.cpp": "VMargin, 0.0f, FontSize",
			"src/game/client/ui.cpp": "",
			"src/game/client/ui_popups.cpp": "Props.m_FontSize = ResolvedFontSize; State.m_SelectionPopupContext.m_FontSize = ResolvedFontSize;",
		}
		for relative, content in files.items():
			path = self.root / relative
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_text(content, encoding="utf-8")
		return self.root

	def test_clean_page_passes(self):
		self.assertEqual(audit_page(self.make_repo(), "general"), [])

	def test_sdf_backed_edit_box_requires_the_explicit_tee_allowlist(self):
		allowed = "Ui()->DoEditBox(&ColorCodeInput, &ColorCodeEditBox, std::max(10.0f, BodySize * 0.85f), IGraphics::CORNER_ALL, {}, TEXTALIGN_MC)"
		self.assertFalse(_contains_forbidden_token("tee", allowed, "Ui()->DoEditBox("))
		self.assertTrue(_contains_forbidden_token("tee", "Ui()->DoEditBox(&OtherInput, &OtherRect, 10.0f)", "Ui()->DoEditBox("))
		self.assertTrue(_contains_forbidden_token("general", allowed, "Ui()->DoEditBox("))

	def test_missing_public_contract_fails(self):
		errors = audit_page(self.make_repo(drop="SettingsCardDeckForRenderPass().RenderCached("), "general")
		self.assertTrue(any("SettingsCardDeckForRenderPass().RenderCached" in item for item in errors))

	def test_tee7_requires_render_pass_isolated_deck(self):
		from qmclient_scripts.gate.check_settings_ui_migration import PAGE_REQUIRED

		self.assertIn("SettingsCardDeckForRenderPass().RenderCached(", PAGE_REQUIRED["tee7"])
		self.assertNotIn("m_SettingsCardDeck.RenderCached(", PAGE_REQUIRED["tee7"])

	def test_manifest_covers_every_new_settings_page(self):
		self.assertTrue(
			{
				"general",
				"player",
				"tee",
				"tee7",
				"graphics",
				"sound",
				"ddnet",
				"appearance",
				"controls",
				"qmclient_hud",
				"qmclient_function",
				"qmclient_visual",
				"contributors",
				"global_search",
				"tclient",
				"tclient_bind_wheel",
				"tclient_chat_binds",
				"tclient_warlist",
				"tclient_status_bar",
				"tclient_info",
				"tclient_profiles",
				"tclient_configs",
				"assets",
			}.issubset(PAGE_STABLE_IDS)
		)
		self.assertIn("qm:dummy_miniview", PAGE_STABLE_IDS["qmclient_hud"])
		self.assertIn("qm:lyrics", PAGE_STABLE_IDS["qmclient_hud"])
		self.assertIn("qm:bind_status_hud", PAGE_STABLE_IDS["qmclient_hud"])
		self.assertIn("qm:emoticons", PAGE_STABLE_IDS["qmclient_function"])
		self.assertIn("qm:map_upload", PAGE_STABLE_IDS["qmclient_function"])
		self.assertIn("qm:solo_split", PAGE_STABLE_IDS["qmclient_function"])
		self.assertIn("qm:favorite_maps", PAGE_STABLE_IDS["qmclient_function"])
		self.assertIn("qm:skin_appearance", PAGE_STABLE_IDS["qmclient_visual"])
		self.assertIn("qm:skin_transition", PAGE_STABLE_IDS["qmclient_visual"])
		self.assertTrue({"appearance", "qmclient_hud", "qmclient_function", "qmclient_visual", "contributors", "tclient_configs", "tclient_warlist"}.issubset(PRODUCER_COMPLETE_PAGES))

	def test_visual_page_requires_its_catalogue_call_and_own_category_entry(self):
		page = self.root / "src/game/client/components/qmclient/menus_qmclient.cpp"
		page.parent.mkdir(parents=True, exist_ok=True)
		page.write_text("""void CMenus::RenderSettingsQmClientVisualDeck()
{
    SettingsPageLayout(MainView, 1.0f);
    SSettingsPageLayoutFrame Frame;
    SSettingsCardDefinition Card;
    QmResolveScrollPolicy(Request, 1.0f, 0.1f);
    EQmScrollProfile::SETTINGS_OUTER;
    SSettingsCardDeckResult Result;
    ResolveSettingsContentMetrics(MainView.w);
    CardDeck.RenderCached(Context);
    ResolveSettingsCardDefinitionsRevision();
    qm_card_catalog::VisualCardStableIds();
}
""", encoding="utf-8")
		registry = self.root / "src/game/client/QmUi/QmCardRegistry.cpp"
		registry.parent.mkdir(parents=True, exist_ok=True)
		registry.write_text('"qm:skin_appearance"', encoding="utf-8")
		navigation = self.root / "src/game/client/components/menus.cpp"
		navigation.parent.mkdir(parents=True, exist_ok=True)
		navigation.write_text('bool CMenus::SetSettingsPageFromCardTab(const char *pTab) { return str_comp(pTab, "visual") == 0; }', encoding="utf-8")
		catalogue = self.root / "src/game/client/QmUi/cards/QmCardCatalogIds.cpp"
		catalogue.parent.mkdir(parents=True, exist_ok=True)
		catalogue.write_text('s_vHudCards = {"qm:skin_appearance"};\ns_vVisualCards = {"qm:skin_appearance"};', encoding="utf-8")
		with patch.dict(PAGE_STABLE_IDS, {"qmclient_visual": ("qm:skin_appearance",)}):
			self.assertEqual(audit_page(self.root, "qmclient_visual"), [])
			page.write_text(page.read_text(encoding="utf-8").replace("qm_card_catalog::VisualCardStableIds();", ""), encoding="utf-8")
			self.assertTrue(any("page producer entry missing" in error for error in audit_page(self.root, "qmclient_visual")))
			page.write_text(page.read_text(encoding="utf-8").replace("ResolveSettingsCardDefinitionsRevision();", "ResolveSettingsCardDefinitionsRevision();\n    qm_card_catalog::VisualCardStableIds();"), encoding="utf-8")
			catalogue.write_text('s_vHudCards = {"qm:skin_appearance"};\ns_vVisualCards = {"qm:other"};', encoding="utf-8")
			self.assertTrue(any("catalogue category entry missing" in error for error in audit_page(self.root, "qmclient_visual")))

	def test_warlist_single_card_contract_passes(self):
		self.assertEqual(audit_page(self.make_warlist_repo(), "tclient_warlist"), [])

	def test_card_catalog_counts_as_producer_source(self):
		"""卡片构造迁到全局卡片目录后，stableId 登记在卡片模块里也算页面生产者契约成立。"""
		root = self.make_warlist_repo(drop='Card.m_Spec = {"deck:tclient-warlist", "War List", nullptr};')
		catalog_path = root / "src/game/client/QmUi/cards/QmCardCatalogHud.cpp"
		catalog_path.parent.mkdir(parents=True, exist_ok=True)
		catalog_path.write_text('SSettingsCardDefinition Card;\nCard.m_Spec = {"deck:tclient-warlist", "War List", nullptr};\n', encoding="utf-8")
		self.assertEqual(audit_page(root, "tclient_warlist"), [])

	def test_warlist_missing_producer_card_fails(self):
		errors = audit_page(
			self.make_warlist_repo(
				drop='Card.m_Spec = {"deck:tclient-warlist", "War List", nullptr};',
				add='str_startswith(FocusStableId, "deck:tclient-warlist");',
			),
			"tclient_warlist",
		)
		self.assertTrue(any("page producer" in item for item in errors))

	def test_warlist_legacy_split_card_fails(self):
		errors = audit_page(self.make_warlist_repo(add='Card.m_Spec = {"deck:tclient-warlist-editor", "Edit Entry", nullptr};'), "tclient_warlist")
		self.assertTrue(any("deck:tclient-warlist-editor" in item and "legacy path" in item for item in errors))

	def test_warlist_legacy_split_card_in_registry_fails(self):
		errors = audit_page(
			self.make_warlist_repo(registry_add='"deck:tclient-warlist-editor"'),
			"tclient_warlist",
		)
		self.assertTrue(any("deck:tclient-warlist-editor" in item and "legacy registry" in item for item in errors))

	def test_missing_outer_scroll_profile_fails(self):
		errors = audit_page(self.make_repo(drop="Request.m_Profile = EQmScrollProfile::SETTINGS_OUTER;"), "general")
		self.assertTrue(any("SETTINGS_OUTER" in item for item in errors))

	def test_missing_content_metrics_fails(self):
		errors = audit_page(self.make_repo(drop="ResolveSettingsContentMetrics("), "general")
		self.assertTrue(any("ResolveSettingsContentMetrics" in item for item in errors))

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

	def test_shared_contract_audit_rejects_numeric_and_dropdown_font_bypasses(self):
		root = self.make_shared_contract_repo()
		self.assertEqual(audit_shared_contracts(root), [])
		qmclient = root / "src/game/client/components/qmclient/menus_qmclient.cpp"
		qmclient.write_text("Options.m_FontSize = std::min(BodySize, ControlColumn.h * 0.8f);", encoding="utf-8")
		self.assertTrue(any("settings font assignment still derives" in item for item in audit_shared_contracts(root)))
		qmclient.write_text("", encoding="utf-8")
		ui = root / "src/game/client/ui_popups.cpp"
		ui.write_text("Props.m_FontSize = ResolvedFontSize;", encoding="utf-8")
		self.assertTrue(any("dropdown trigger and popup" in item for item in audit_shared_contracts(root)))

	def test_legacy_path_fails(self):
		errors = audit_page(self.make_repo(add="DoSettingsScrollbarOption("), "general")
		self.assertTrue(any("DoSettingsScrollbarOption" in item for item in errors))

	def test_missing_registry_or_navigation_fails(self):
		root = self.make_repo()
		(root / "src/game/client/QmUi/QmCardRegistry.cpp").write_text("", encoding="utf-8")
		errors = audit_page(root, "general")
		self.assertTrue(any("registry/navigation" in item for item in errors))

	def test_route_name_outside_explicit_navigation_contract_fails(self):
		root = self.make_repo()
		navigation = root / "src/game/client/components/menus.cpp"
		navigation.write_text('const char *pUnrelated = "general";', encoding="utf-8")
		errors = audit_page(root, "general")
		self.assertTrue(any("general: general: registry/navigation entry missing" in item for item in errors))


if __name__ == "__main__":
	unittest.main()
