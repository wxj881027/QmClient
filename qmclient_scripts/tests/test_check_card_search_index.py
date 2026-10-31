from __future__ import annotations

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


class AnalyzeTest(unittest.TestCase):
	def _write_repo(self, root: Path, index_text: str, card_text: str) -> None:
		index_path = root / checker.INDEX_RELATIVE
		index_path.parent.mkdir(parents=True)
		index_path.write_text(index_text, encoding="utf-8")
		card_path = root / checker.CLIENT_SOURCE_RELATIVE / "Card.cpp"
		card_path.parent.mkdir(parents=True, exist_ok=True)
		card_path.write_text(card_text, encoding="utf-8")

	def test_reports_missing_labels_and_drift(self) -> None:
		index_text = INDEX_SAMPLE.replace(
			'\t\t{Localizable("Beta", "ctx"), nullptr, "测试"},',
			'\t\t{Localizable("Beta", "ctx"), nullptr, "测试"},\n\t\t{Localizable("Ghost label"), nullptr, "幽灵"},',
		)
		with tempfile.TemporaryDirectory() as temp_dir:
			root = Path(temp_dir)
			self._write_repo(root, index_text, CARD_SOURCE)
			report = checker.analyze(root)
		self.assertEqual(report["tables"], 1)
		self.assertEqual(report["mapped_cards"], 1)
		self.assertEqual(report["drifted_labels"], [{"table": "s_acard", "label": "Ghost label"}])
		card = report["cards"][0]
		self.assertEqual(card["stable_id"], "qm:alpha")
		self.assertEqual(card["missing"], ["Delta", "Gamma"])

	def test_counts_registration_problems(self) -> None:
		index_text = INDEX_SAMPLE.replace(
			'{"qm:alpha", s_acard, std::size(s_acard)},',
			'{"qm:alpha", s_missing, std::size(s_acard)},',
		)
		with tempfile.TemporaryDirectory() as temp_dir:
			root = Path(temp_dir)
			self._write_repo(root, index_text, CARD_SOURCE)
			report = checker.analyze(root)
		self.assertEqual(report["unregistered_tables"], ["s_acard"])
		self.assertEqual(report["missing_tables"], ["qm:alpha"])


if __name__ == "__main__":
	unittest.main()
