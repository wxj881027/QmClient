#include "QmCardCatalogInternal.h"
#include "QmCardMeasureRevision.h"

#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/UiButtons.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>

void CMenus::RenderQmTooltipContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, bool PrewarmOnly)
{
	const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();
	static CButtonContainer s_Background, s_Text;
	DoLine_ColorPicker(&s_Background, Metrics, &Content, Localize("Tooltip background color"), &g_Config.m_QmTooltipBackgroundColor,
		color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmTooltipBackgroundColor, true)), false, nullptr, true);
	DoLine_ColorPicker(&s_Text, Metrics, &Content, Localize("Tooltip text color"), &g_Config.m_QmTooltipTextColor,
		color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmTooltipTextColor, true)), false, nullptr, true);
	CUIRect Row, Label, Control;
	Content.HSplitTop(Metrics.m_LineHeight, &Row, &Content);
	Row.VSplitMid(&Label, &Control, Metrics.m_LineSpacing);
	SLabelProperties Props;
	Props.m_MaxWidth = Label.w;
	Props.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&Label, Localize("Tooltip font size"), Metrics.m_BodySize, TEXTALIGN_ML, Props);
	static int s_FontSize;
	GameClient()->m_Tooltips.DoSettingsToolTipForConfig(&s_FontSize, &Row, &g_Config.m_QmTooltipFontSize, &Label);
	RenderQmSettingsSliderWithValueInput(&s_FontSize, Control, &g_Config.m_QmTooltipFontSize, 10, 24, "", ReadOnly);
	Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
	RenderQmFunctionCheckboxRow(Content, Metrics.m_LineHeight, Metrics.m_LineSpacing, &g_Config.m_QmTooltipAnimation,
		"Tooltip bounce animation", Localize("Tooltip bounce animation"), &g_Config.m_QmTooltipAnimation, ReadOnly);
	Content.HSplitTop(std::max(Metrics.m_LineHeight, g_Config.m_QmTooltipFontSize * 1.5f), &Row, &Content);
	Row.Draw(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTooltipBackgroundColor, true)), IGraphics::CORNER_ALL, 5.0f);
	const ColorRGBA PreviousColor = TextRender()->GetTextColor();
	TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmTooltipTextColor, true)));
	Row.HMargin(2.0f, &Row);
	Props.m_MaxWidth = Row.w;
	Ui()->DoLabel(&Row, Localize("Tooltip preview"), static_cast<float>(g_Config.m_QmTooltipFontSize), TEXTALIGN_MC, Props);
	TextRender()->TextColor(PreviousColor);
	Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
	Content.HSplitTop(Metrics.m_LineHeight, &Row, &Content);
	static CButtonContainer s_Reset;
	if(ui_widget::SecondaryButton(SettingsUiContext("settings_tooltip", Metrics.m_UiScale), &s_Reset, Localize("Reset tooltip appearance"), Row, ReadOnly))
	{
		g_Config.m_QmTooltipBackgroundColor = DefaultConfig::QmTooltipBackgroundColor;
		g_Config.m_QmTooltipTextColor = DefaultConfig::QmTooltipTextColor;
		g_Config.m_QmTooltipFontSize = DefaultConfig::QmTooltipFontSize;
		g_Config.m_QmTooltipAnimation = DefaultConfig::QmTooltipAnimation;
	}
	Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
}

namespace qm_card_catalog
{
	bool BuildTooltipCard(const SQmCardBuildContext &Ctx, SSettingsCardDefinition &Out)
	{
		MakeModuleCard(Ctx, qm_module::EQmModuleId::Tooltip, "qm:tooltip", "Tooltips", "Appearance of help bubbles outside settings cards", [Ctx](CUIRect &Content) { QmCardRenderHook::RenderQmTooltipContent(Ctx.m_pMenus, Content, Ctx.m_Metrics, Ctx.m_ReadOnly); }, [Metrics = Ctx.m_Metrics](float) { return CardRows(Metrics, 6.0f); }, MeasureModuleCardRevision(qm_module::EQmModuleId::Tooltip), {}, Out);
		return true;
	}
}
