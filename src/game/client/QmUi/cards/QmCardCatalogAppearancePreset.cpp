#include "QmAppearancePreset.h"
#include "QmCardCatalogInternal.h"

#include <engine/textrender.h>

#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/components/menus.h>
#include <game/localization.h>

#include <algorithm>
#include <string>

void CMenus::RenderQmAppearancePresetContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	IUiContext Ctx = SettingsUiContext("settings_appearance_preset", Metrics.m_UiScale);
	const auto Measure = [this](const char *pValue, float Width, float Size) { return TextRender()->TextBoundingBox(Size, pValue, -1, Width).m_H + Size * 0.25f; };
	const auto DrawText = [this](const CUIRect &Row, const char *pText, float Size) {
		SLabelProperties Props;
		Props.m_MaxWidth = Row.w;
		Ui()->DoLabel(&Row, pText, Size, TEXTALIGN_ML, Props);
	};
	const auto RenderNote = [&](const char *pId, const char *pText) {
		Flow.Text(pId, pText, 0.0f, Metrics.m_SmallSize, ui_token::settings::RowGap(Metrics.m_LineSpacing), Measure, [&](const CUIRect &Row, const char *pValue) { DrawText(Row, pValue, Metrics.m_SmallSize); }, Metrics.m_LineHeight);
	};
	RenderNote("preset-summary", Localize("Applying this preset changes the appearance settings listed below."));
	const auto RenderChange = [&](const char *pId, const char *pLabel, const char *pValue) {
		Flow.TextColumns(pId, pLabel, pValue, Metrics.m_BodySize, Metrics.m_LineHeight, 0.62f, Metrics.m_LineSpacing, ui_token::settings::RowGap(Metrics.m_LineSpacing), Measure,
			[&](const CUIRect &Row, const char *pText) { DrawText(Row, pText, Metrics.m_BodySize); });
	};
	RenderChange("preset-latin-font", Localize("Latin font"), "LXGW WenKai");
	RenderChange("preset-cjk-font", Localize("CJK font"), "LXGW WenKai");
	for(const auto &Setting : QmAppearancePreset::IntSettings())
		RenderChange(Setting.m_pLabel, Localize(Setting.m_pLabel), Localize(Setting.m_pValueText));
	for(const auto &Setting : QmAppearancePreset::ColorSettings())
		RenderChange(Setting.m_pLabel, Localize(Setting.m_pLabel), Localize(Setting.m_pValueText));

	std::string FontConfig;
	const bool FontAvailable = TextRender()->QmFontFamilyDefaultConfig("LXGW WenKai", FontConfig) &&
				   FontConfig.size() < sizeof(g_Config.m_QmCustomFont) && FontConfig.size() < sizeof(g_Config.m_QmCustomFontCjk);
	const char *pAvailable = Localize("LXGW WenKai is available.");
	const char *pUnavailable = Localize("LXGW WenKai is unavailable. Current fonts and weights will be kept.");
	RenderNote("preset-font-status", FontAvailable ? pAvailable : pUnavailable);
	CUIRect Row;
	static CButtonContainer s_ApplyButton;
	if(Flow.Control("preset-apply", Metrics.m_LineHeight, Row) && ui_widget::SecondaryButton(Ctx, &s_ApplyButton, Localize("Apply Qm recommended appearance"), Row, PrewarmOnly) && Flow.Input() && !ReadOnly)
	{
		if(QmAppearancePreset::Apply(g_Config, FontAvailable ? FontConfig.c_str() : nullptr))
		{
			TextRender()->SetCustomFace(g_Config.m_QmCustomFont);
			TextRender()->SetCustomFaceCjk(g_Config.m_QmCustomFontCjk);
			TextRender()->SetCustomFontWeight(g_Config.m_QmCustomFontWeight);
			TextRender()->SetCustomFontWeightCjk(g_Config.m_QmCustomFontWeightCjk);
		}
	}
	Flow.Space(ui_token::settings::RowGap(Metrics.m_LineSpacing));
}

namespace qm_card_catalog
{
	bool BuildAppearancePresetCard(const SQmCardBuildContext &Ctx, SSettingsCardDefinition &Out)
	{
		MakeModuleCard(
			Ctx,
			qm_module::EQmModuleId::AppearancePreset,
			"qm:appearance_preset",
			"Qm recommended appearance",
			"Preview and apply a fixed appearance preset",
			[Ctx](CUIRect &Content) { QmCardRenderHook::RenderQmAppearancePresetContent(Ctx.m_pMenus, Content, Ctx.m_Metrics, Ctx.m_ReadOnly); },
			Out);
		UseDeclaredModuleContent(Ctx, Out);
		return true;
	}
}
