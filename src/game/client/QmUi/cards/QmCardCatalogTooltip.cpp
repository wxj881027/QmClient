#include "QmCardCatalogInternal.h"

#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>

void CMenus::RenderQmTooltipContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	CUIRect Row, Label, Control;
	SLabelProperties Props;
	Props.m_EllipsisAtEnd = true;
	if(Flow.Control("tooltip-animation", Metrics.m_LineHeight, Row))
		RenderQmFunctionCheckbox(&g_Config.m_QmTooltipAnimation, "Tooltip bounce animation", Localize("Tooltip bounce animation"), &g_Config.m_QmTooltipAnimation, &Row, ReadOnly);
	if(Flow.Control("tooltip-preview", std::max(Metrics.m_LineHeight, g_Config.m_QmTooltipFontSize * 1.5f), Row, ui_token::settings::RowGap(Metrics.m_LineSpacing)))
	{
		Row.Draw(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTooltipBackgroundColor, true)), IGraphics::CORNER_ALL, 5.0f);
		const ColorRGBA PreviousColor = TextRender()->GetTextColor();
		TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTooltipTextColor, true)));
		Row.HMargin(2.0f, &Row);
		Props.m_MaxWidth = Row.w;
		Ui()->DoLabel(&Row, Localize("Tooltip preview"), static_cast<float>(g_Config.m_QmTooltipFontSize), TEXTALIGN_MC, Props);
		TextRender()->TextColor(PreviousColor);
	}
	Flow.Space(ui_token::settings::RowGap(Metrics.m_LineSpacing));
	float SectionReveal = 0.0f;
	if(!qm_card_catalog::QmCardRenderHook::PersonalizationReveal(this, Content, "qm:tooltip", Metrics, &SectionReveal, ReadOnly))
		return;
	const CSettingsContentRevealScope SectionRevealScope(Ui(), Flow, SectionReveal);

	static CButtonContainer s_Background, s_Text;
	CUIRect ColorRow;
	if(Flow.Control("tooltip-background", Metrics.m_LineHeight, ColorRow))
	{
		DoLine_ColorPicker(&s_Background, Metrics, &ColorRow, Localize("Tooltip background color"), &g_Config.m_QmTooltipBackgroundColor,
			color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmTooltipBackgroundColor, true)), false, nullptr, true);
	}
	if(Flow.Control("tooltip-text", Metrics.m_LineHeight, ColorRow, ui_token::settings::RowGap(Metrics.m_LineSpacing)))
	{
		DoLine_ColorPicker(&s_Text, Metrics, &ColorRow, Localize("Tooltip text color"), &g_Config.m_QmTooltipTextColor,
			color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmTooltipTextColor, true)), false, nullptr, true);
	}
	if(Flow.Control("tooltip-font-size", Metrics.m_LineHeight, Row, ui_token::settings::RowGap(Metrics.m_LineSpacing)))
	{
		Row.VSplitMid(&Label, &Control, Metrics.m_LineSpacing);
		Props.m_MaxWidth = Label.w;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Label, Localize("Tooltip font size"), Metrics.m_BodySize, TEXTALIGN_ML, Props);
		static int s_FontSize;
		GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&s_FontSize, &Row, &g_Config.m_QmTooltipFontSize, &Label);
		RenderQmSettingsSliderWithValueInput(&s_FontSize, Control, &g_Config.m_QmTooltipFontSize, 10, 24, "", ReadOnly);
	}
	static CButtonContainer s_Reset;
	if(Flow.Control("tooltip-reset", Metrics.m_LineHeight, Row, ui_token::settings::RowGap(Metrics.m_LineSpacing)) && ui_widget::SecondaryButton(SettingsUiContext("settings_tooltip", Metrics.m_UiScale), &s_Reset, Localize("Reset tooltip appearance"), Row, PrewarmOnly) && Flow.Input() && !ReadOnly)
	{
		g_Config.m_QmTooltipBackgroundColor = DefaultConfig::QmTooltipBackgroundColor;
		g_Config.m_QmTooltipTextColor = DefaultConfig::QmTooltipTextColor;
		g_Config.m_QmTooltipFontSize = DefaultConfig::QmTooltipFontSize;
		g_Config.m_QmTooltipAnimation = DefaultConfig::QmTooltipAnimation;
	}
	Flow.Space(ui_token::settings::RowGap(Metrics.m_LineSpacing));
}

namespace qm_card_catalog
{
	bool BuildTooltipCard(const SQmCardBuildContext &Ctx, SSettingsCardDefinition &Out)
	{
		MakeModuleCard(
			Ctx,
			qm_module::EQmModuleId::Tooltip,
			"qm:tooltip",
			"Tooltips",
			"Appearance of help bubbles",
			[Ctx](CUIRect &Content) { QmCardRenderHook::RenderQmTooltipContent(Ctx.m_pMenus, Content, Ctx.m_Metrics, Ctx.m_ReadOnly); },
			Out);
		UseDeclaredModuleContent(Ctx, Out);
		return true;
	}
}
