#include "QmAppearancePreset.h"
#include "QmCardCatalogInternal.h"

#include <engine/textrender.h>

#include <game/client/QmUi/UiButtons.h>
#include <game/client/components/menus.h>
#include <game/localization.h>

#include <algorithm>
#include <string>

void CMenus::RenderQmAppearancePresetContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, bool PrewarmOnly)
{
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	IUiContext Ctx = SettingsUiContext("settings_appearance_preset", Metrics.m_UiScale);
	const auto RenderNote = [&](const char *pText) {
		const float Height = std::max(Metrics.m_LineHeight, TextRender()->TextBoundingBox(Metrics.m_SmallSize, pText, -1, std::max(1.0f, Content.w)).m_H);
		CUIRect Row;
		Content.HSplitTop(Height, &Row, &Content);
		SLabelProperties Props;
		Props.m_MaxWidth = Row.w;
		Ui()->DoLabel(&Row, pText, Metrics.m_SmallSize, TEXTALIGN_ML, Props);
		Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
	};
	RenderNote(Localize("Applying this preset changes the appearance settings listed below."));
	const auto RenderChange = [&](const char *pLabel, const char *pValue) {
		const float Gap = Metrics.m_LineSpacing;
		const float LabelWidth = std::max(1.0f, Content.w * 0.62f - Gap);
		const float ValueWidth = std::max(1.0f, Content.w - LabelWidth - Gap);
		const float Height = std::max({Metrics.m_LineHeight,
			TextRender()->TextBoundingBox(Metrics.m_BodySize, pLabel, -1, LabelWidth).m_H,
			TextRender()->TextBoundingBox(Metrics.m_BodySize, pValue, -1, ValueWidth).m_H});
		CUIRect Row, Label, Value;
		Content.HSplitTop(Height, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &Label, &Value);
		Value.VSplitLeft(Gap, nullptr, &Value);
		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Ui()->DoLabel(&Label, pLabel, Metrics.m_BodySize, TEXTALIGN_ML, Props);
		Props.m_MaxWidth = Value.w;
		Ui()->DoLabel(&Value, pValue, Metrics.m_BodySize, TEXTALIGN_ML, Props);
		Content.HSplitTop(Gap, nullptr, &Content);
	};
	RenderChange(Localize("Latin font"), "LXGW WenKai");
	RenderChange(Localize("CJK font"), "LXGW WenKai");
	for(const auto &Setting : QmAppearancePreset::IntSettings())
		RenderChange(Localize(Setting.m_pLabel), Localize(Setting.m_pValueText));
	for(const auto &Setting : QmAppearancePreset::ColorSettings())
		RenderChange(Localize(Setting.m_pLabel), Localize(Setting.m_pValueText));

	std::string FontConfig;
	const bool FontAvailable = TextRender()->QmFontFamilyDefaultConfig("LXGW WenKai", FontConfig) &&
				   FontConfig.size() < sizeof(g_Config.m_QmCustomFont) && FontConfig.size() < sizeof(g_Config.m_QmCustomFontCjk);
	const char *pAvailable = Localize("LXGW WenKai is available.");
	const char *pUnavailable = Localize("LXGW WenKai is unavailable. Current fonts and weights will be kept.");
	const float FontNoteHeight = std::max({Metrics.m_LineHeight,
		TextRender()->TextBoundingBox(Metrics.m_SmallSize, pAvailable, -1, std::max(1.0f, Content.w)).m_H,
		TextRender()->TextBoundingBox(Metrics.m_SmallSize, pUnavailable, -1, std::max(1.0f, Content.w)).m_H});
	CUIRect Row;
	Content.HSplitTop(FontNoteHeight, &Row, &Content);
	SLabelProperties Props;
	Props.m_MaxWidth = Row.w;
	Ui()->DoLabel(&Row, FontAvailable ? pAvailable : pUnavailable, Metrics.m_SmallSize, TEXTALIGN_ML, Props);
	Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
	Content.HSplitTop(Metrics.m_LineHeight, &Row, &Content);
	static CButtonContainer s_ApplyButton;
	if(ui_widget::SecondaryButton(Ctx, &s_ApplyButton, Localize("Apply Qm recommended appearance"), Row, ReadOnly))
	{
		if(QmAppearancePreset::Apply(g_Config, FontAvailable ? FontConfig.c_str() : nullptr))
		{
			TextRender()->SetCustomFace(g_Config.m_QmCustomFont);
			TextRender()->SetCustomFaceCjk(g_Config.m_QmCustomFontCjk);
			TextRender()->SetCustomFontWeight(g_Config.m_QmCustomFontWeight);
			TextRender()->SetCustomFontWeightCjk(g_Config.m_QmCustomFontWeightCjk);
		}
	}
	Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
}

namespace qm_card_catalog
{
	bool BuildAppearancePresetCard(const SQmCardBuildContext &Ctx, SSettingsCardDefinition &Out)
	{
		MakeModuleCard(Ctx, qm_module::EQmModuleId::AppearancePreset, "qm:appearance_preset", "Qm recommended appearance", "Preview and apply a fixed appearance preset", [Ctx](CUIRect &Content) { QmCardRenderHook::RenderQmAppearancePresetContent(Ctx.m_pMenus, Content, Ctx.m_Metrics, Ctx.m_ReadOnly); }, [Metrics = Ctx.m_Metrics](float) { return CardRows(Metrics, 17.0f); }, 1, {}, Out);
		return true;
	}
}
