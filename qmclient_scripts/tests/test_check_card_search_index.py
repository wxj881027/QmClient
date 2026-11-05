from __future__ import annotations

import contextlib
import io
import tempfile
import unittest
from pathlib import Path

from qmclient_scripts import check_card_search_index as checker


INDEX_SAMPLE = """namespace
{
	static constexpr SCardSettingSearch s_acard[] = {
		{Localizable("Alpha label"), nullptr, "别名"},
		{Localizable("Beta", "ctx"), nullptr, "测试"},
		{nullptr, "qm_alpha", nullptr},
	};

	struct SCardMetadata
	{
		const char *m_pStableId;
		const SCardSettingSearch *m_pSettings;
		size_t m_Count;
	};

	static constexpr SCardMetadata s_aCards[] = {
		{"qm:alpha", s_acard, std::size(s_acard)},
	};
}
"""

CARD_SOURCE = """namespace
{
	void Register()
	{
		Add(Id, "qm:alpha", "Alpha", "desc", [](CUIRect &Content) {
			QmCardRenderHook::RenderAlpha(Content);
		});
	}
}

void CMenus::RenderAlpha(CUIRect &Content)
{
	const char *pIgnored = "}";
	// Localize("Commented") 不应被收集
	Localize("Alpha label");
	Localize("Beta");
	Localize("Gamma");
	if(Content.w > 0.0f)
	{
		Localize("Delta");
	}
}

void CMenus::RenderOther(CUIRect &Content)
{
	Localize("ShouldNotBeCollected");
}
"""


DEFAULTS_SAMPLE = """static const std::vector<SCardDefault> &DefaultsTable()
{
	static const std::vector<SCardDefault> s_aDefaults = [] {
		std::vector<SCardDefault> Cards = {
			{"qm:alpha", "visual", ECardColumn::Left, 1, "Alpha", "alpha", "desc"},
		};
		return Cards;
	}();
	return s_aDefaults;
}
"""


class ParseTablesTest(unittest.TestCase):
	def test_collects_labels_and_configs(self) -> None:
		tables = checker.parse_tables(INDEX_SAMPLE)
		self.assertEqual(set(tables), {"s_acard"})
		self.assertEqual(tables["s_acard"]["labels"], {"Alpha label", "Beta"})
		self.assertEqual(tables["s_acard"]["configs"], {"qm_alpha"})

	def test_collects_card_metadata(self) -> None:
		self.assertEqual(checker.parse_metadata(INDEX_SAMPLE), {"qm:alpha": "s_acard"})


class ParserTest(unittest.TestCase):
	def test_find_matching_ignores_strings_and_comments(self) -> None:
		text = 'call(")") { // }\n if (x) { y(); } } tail'
		open_brace = text.index("{")
		close_brace = checker.find_matching(text, open_brace)
		self.assertEqual(text[close_brace], "}")
		self.assertEqual(text[close_brace + 1 :].strip(), "tail")

	def test_extracts_only_the_function_body(self) -> None:
		body = checker.extract_function_body(CARD_SOURCE, "RenderAlpha")
		self.assertIsNotNone(body)
		assert body is not None
		labels = checker.extract_localize_labels(body)
		self.assertEqual(labels, {"Alpha label", "Beta", "Gamma", "Delta"})
		self.assertNotIn("ShouldNotBeCollected", labels)

	def test_maps_add_and_make_module_card(self) -> None:
		text = 'Add(Id, "qm:alpha", [](CUIRect &Content) { QmCardRenderHook::RenderAlpha(Content); });MakeModuleCard(Ctx, qm_module::EQmModuleId::Alpha, "qm:beta", "Beta", "desc", [](CUIRect &Content) { QmCardRenderHook::RenderBeta(Content); }, Out);'
		self.assertEqual(checker.parse_add_mappings(text), {"qm:alpha": "RenderAlpha"})
		self.assertEqual(checker.parse_make_module_mappings(text), {"qm:beta": "RenderBeta"})

	def test_detects_button_labels_but_not_checkbox_rows(self) -> None:
		source = '''
		if(DoButton_Menu(&id, Localize("Apply"), 0, &Button)) {}
		if(DoSettingsButton_CheckBox(..., Localize("Save ghost"), ...)) {}
		if(ui_widget::SecondaryButton(..., Localize("Reset"), ...)) {}
		'''
		self.assertEqual(checker.extract_button_labels([source]), {"Apply", "Reset"})

	def test_resolves_spec_vars_and_stable_id_branches(self) -> None:
		text = """void Build()
{
	const auto *pCard = FindByStableId("deck:alpha");
	const SSettingsCardSpec AlphaSpec{pCard->m_pStableId, Localize("Alpha"), ""};
	if(str_comp(pStableId, "deck:beta") == 0)
	{
		Localize("Beta");
	}
	AddCard(AlphaSpec, [](CUIRect &Content) { Localize("Alpha"); });
}
"""
		variables = checker.parse_find_stable_id_vars(text)
		self.assertEqual(variables, {"pCard": "deck:alpha"})
		self.assertEqual(checker.parse_spec_vars(text, variables), {"AlphaSpec": "deck:alpha"})
		blocks = checker.parse_addcard_spec_blocks(text, {"AlphaSpec": "deck:alpha"})
		self.assertIn('Localize("Alpha")', blocks["deck:alpha"])
		branches = checker.parse_stable_id_branches(text)
		self.assertIn('Localize("Beta")', branches["deck:beta"])


DECLARED_SOURCE = """namespace
{
	void Register()
	{
		Add(Id, "qm:alpha", "Alpha", "desc", [](CUIRect &Content) {
			QmCardRenderHook::RenderAlpha(Content);
		});
	}
}

namespace
{
constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_alpha[] = {
	{Localizable("Alpha label"), nullptr, "别名"},
	{nullptr, "qm_alpha", nullptr},
};
QM_CARD_SEARCH_REGISTER("qm:alpha", qm_alpha);
}

void CMenus::RenderAlpha(CUIRect &Content)
{
	Localize("Alpha label");
}
"""


class AnalyzeTest(unittest.TestCase):
	def _write_repo(self, root: Path, index_text: str, card_text: str) -> None:
		index_path = root / checker.INDEX_RELATIVE
		index_path.parent.mkdir(parents=True)
		index_path.write_text(index_text, encoding="utf-8")
		card_path = root / checker.CARD_SOURCE_RELATIVE / "Card.cpp"
		card_path.parent.mkdir(parents=True, exist_ok=True)
		card_path.write_text(card_text, encoding="utf-8")

	def _analyze(self, index_text: str, card_text: str) -> dict:
		with tempfile.TemporaryDirectory() as temp_dir:
			root = Path(temp_dir)
			self._write_repo(root, index_text, card_text)
			return checker.analyze(root)

	def test_reports_missing_labels_and_drift(self) -> None:
		index_text = INDEX_SAMPLE.replace(
			'\t\t{Localizable("Beta", "ctx"), nullptr, "测试"},',
			'\t\t{Localizable("Beta", "ctx"), nullptr, "测试"},\n\t\t{Localizable("Ghost label"), nullptr, "幽灵"},',
		)
		report = self._analyze(index_text, CARD_SOURCE)
		self.assertEqual(report["tables"], 1)
		self.assertEqual(report["mapped_cards"], 1)
		self.assertEqual(report["exact_cards"], 1)
		self.assertEqual(report["drifted_labels"], [{"table": "s_acard", "label": "Ghost label"}])
		self.assertEqual(report["unindexed_labels"], ["Delta", "Gamma", "ShouldNotBeCollected"])
		card = report["cards"][0]
		self.assertEqual(card["stable_id"], "qm:alpha")
		self.assertEqual(card["confidence"], "function")
		self.assertEqual(card["missing"], ["Delta", "Gamma"])

	def test_action_button_labels_are_rejected_from_tables(self) -> None:
		index_text = INDEX_SAMPLE.replace('Localizable("Alpha label")', 'Localizable("Frobnicate")')
		card_text = CARD_SOURCE + '''

void CMenus::RenderButtons(CUIRect &Content)
{
	if(DoButton_Menu(&id, Localize("Frobnicate"), 0, &Button)) {}
}
'''
		report = self._analyze(index_text, card_text)
		self.assertIn("Frobnicate", report["detected_button_labels"])
		self.assertEqual(report["indexed_action_labels"], ["Frobnicate"])

	def test_ignores_shared_dialog_and_status_labels(self) -> None:
		card_text = CARD_SOURCE.replace('Localize("Gamma");', 'Localize("Gamma");\n\tLocalize("Loading…");')
		report = self._analyze(INDEX_SAMPLE, card_text)
		self.assertNotIn("Loading…", report["unindexed_labels"])
		self.assertNotIn("Loading…", report["cards"][0]["missing"])

	def test_marks_cards_without_exact_mapping(self) -> None:
		card_text = CARD_SOURCE.replace('Add(Id, "qm:alpha"', 'Add(Id, "qm:other"')
		report = self._analyze(INDEX_SAMPLE, card_text)
		card = report["cards"][0]
		self.assertEqual(card["confidence"], "none")
		self.assertEqual(card["source"], "")
		self.assertEqual(report["no_source_cards"], ["qm:alpha"])

	def test_marks_cards_without_static_labels(self) -> None:
		index_text = INDEX_SAMPLE.replace(
			'{Localizable("Alpha label"), nullptr, "别名"},',
			'{Localizable("Only config"), nullptr, "仅配置"},',
		).replace('{Localizable("Beta", "ctx"), nullptr, "测试"},', "")
		card_text = CARD_SOURCE.replace('Add(Id, "qm:alpha"', 'Add(Id, "qm:other"')
		report = self._analyze(index_text, card_text)
		self.assertEqual(report["no_source_cards"], ["qm:alpha"])
		self.assertEqual(report["mapped_cards"], 0)
		self.assertEqual(report["unindexed_labels"], ["Alpha label", "Beta", "Delta", "Gamma", "ShouldNotBeCollected"])

	def test_reads_declared_tables_from_card_sources(self) -> None:
		report = self._analyze("", DECLARED_SOURCE)
		self.assertEqual(report["tables"], 1)
		self.assertEqual(report["registered_cards"], 1)
		self.assertEqual(report["index_labels"], 1)
		self.assertEqual(report["drifted_labels"], [])
		self.assertEqual(report["mapped_cards"], 1)

	def _write_declaration_repo(self, root: Path, index_text: str, inc_text: str, defaults_text: str) -> None:
		self._write_repo(root, index_text, CARD_SOURCE)
		inc_path = root / "src/game/client/QmUi/cards/QmCardSearch/Ghost.inc"
		inc_path.parent.mkdir(parents=True, exist_ok=True)
		inc_path.write_text(inc_text, encoding="utf-8")
		registry_path = root / "src/game/client/QmUi/QmCardRegistry.cpp"
		registry_path.write_text(defaults_text, encoding="utf-8")

	def test_reports_ghost_label_only_in_inc(self) -> None:
		inc_text = """namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_alpha[] = {
		{Localizable("Alpha label"), nullptr, "别名"},
		{Localizable("Ghost search label"), nullptr, "幽灵"},
	};
	QM_CARD_SEARCH_REGISTER("qm:alpha", qm_alpha);
}
"""
		with tempfile.TemporaryDirectory() as temp_dir:
			root = Path(temp_dir)
			self._write_declaration_repo(root, '#include "QmCardSearch/Ghost.inc"\n', inc_text, DEFAULTS_SAMPLE)
			report = checker.analyze(root)
		self.assertIn({"table": "qm:alpha", "label": "Ghost search label"}, report["drifted_labels"])
		self.assertNotIn({"table": "qm:alpha", "label": "Alpha label"}, report["drifted_labels"])
		self.assertEqual(report["declaration_problems"], [])

	def test_reports_array_without_registration(self) -> None:
		inc_text = """namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_alpha[] = {
		{Localizable("Alpha label"), nullptr, "别名"},
	};
}
"""
		with tempfile.TemporaryDirectory() as temp_dir:
			root = Path(temp_dir)
			self._write_declaration_repo(root, '#include "QmCardSearch/Ghost.inc"\n', inc_text, DEFAULTS_SAMPLE)
			report = checker.analyze(root)
		self.assertIn("array_without_registration", {item["kind"] for item in report["declaration_problems"]})

	def test_strict_fails_on_declaration_problems(self) -> None:
		inc_text = """namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_alpha[] = {
		{Localizable("Alpha label"), nullptr, "别名"},
	};
}
"""
		with tempfile.TemporaryDirectory() as temp_dir:
			root = Path(temp_dir)
			self._write_declaration_repo(root, '#include "QmCardSearch/Ghost.inc"\n', inc_text, DEFAULTS_SAMPLE)
			with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
				self.assertEqual(checker.main(["--root", str(root), "--strict"]), 1)

	def test_reports_duplicate_stable_id(self) -> None:
		inc_text = """namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_alpha[] = {
		{Localizable("Alpha label"), nullptr, "别名"},
	};
	QM_CARD_SEARCH_REGISTER("qm:alpha", qm_alpha);
}
"""
		with tempfile.TemporaryDirectory() as temp_dir:
			root = Path(temp_dir)
			self._write_declaration_repo(root, '#include "QmCardSearch/Ghost.inc"\n#include "QmCardSearch/Other.inc"\n', inc_text, DEFAULTS_SAMPLE)
			other_path = root / "src/game/client/QmUi/cards/QmCardSearch/Other.inc"
			other_path.write_text(inc_text, encoding="utf-8")
			report = checker.analyze(root)
		self.assertIn("duplicate_stable_id", {item["kind"] for item in report["declaration_problems"]})

	def test_reports_unknown_card(self) -> None:
		inc_text = """namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_alpha[] = {
		{Localizable("Alpha label"), nullptr, "别名"},
	};
	QM_CARD_SEARCH_REGISTER("qm:alpha", qm_alpha);
}
"""
		with tempfile.TemporaryDirectory() as temp_dir:
			root = Path(temp_dir)
			self._write_declaration_repo(root, '#include "QmCardSearch/Ghost.inc"\n', inc_text, DEFAULTS_SAMPLE.replace("qm:alpha", "qm:other"))
			report = checker.analyze(root)
		self.assertIn("unknown_card", {item["kind"] for item in report["declaration_problems"]})

	def test_reports_missing_aggregator_include(self) -> None:
		inc_text = """namespace
{
	constexpr qm_card_registry::SCardSettingSearch s_aQmSearch_qm_alpha[] = {
		{Localizable("Alpha label"), nullptr, "别名"},
	};
	QM_CARD_SEARCH_REGISTER("qm:alpha", qm_alpha);
}
"""
		with tempfile.TemporaryDirectory() as temp_dir:
			root = Path(temp_dir)
			self._write_declaration_repo(root, "", inc_text, DEFAULTS_SAMPLE)
			report = checker.analyze(root)
		self.assertIn("missing_aggregator_include", {item["kind"] for item in report["declaration_problems"]})

	def test_counts_registration_problems(self) -> None:
		index_text = INDEX_SAMPLE.replace(
			'{"qm:alpha", s_acard, std::size(s_acard)},',
			'{"qm:alpha", s_missing, std::size(s_acard)},',
		)
		report = self._analyze(index_text, CARD_SOURCE)
		self.assertEqual(report["unregistered_tables"], ["s_acard"])
		self.assertEqual(report["missing_tables"], ["qm:alpha"])


if __name__ == "__main__":
	unittest.main()
