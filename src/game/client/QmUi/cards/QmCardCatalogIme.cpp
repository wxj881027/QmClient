#include "QmCardCatalog.h"

#include <engine/shared/config.h>

#include <game/client/components/menus.h>
#include <game/client/ui.h>
#include <game/localization.h>

void CMenus::RenderQmFunctionImeContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	RenderQmFunctionCheckboxRow(Content, LineHeight, LineSpacing, &g_Config.m_QmImeAutoManage, "Auto manage IME while typing", Localize("Auto manage IME while typing"), &g_Config.m_QmImeAutoManage, PrewarmOnly);
	RenderQmFunctionCheckboxRow(Content, LineHeight, LineSpacing, &g_Config.m_QmNewIme, "New IME", Localize("New IME"), &g_Config.m_QmNewIme, PrewarmOnly);
	const auto &Metrics = CurrentSettingsContentMetrics();
	static CButtonContainer s_Background, s_Text, s_SelectedText, s_Selection;
	DoLine_AlphaColorPicker(&s_Background, Metrics, &Content, Localize("IME background color"), &g_Config.m_QmImeBgColor, &g_Config.m_QmImeOpacity, DefaultConfig::QmImeBgColor, DefaultConfig::QmImeOpacity);
	DoLine_ColorPicker(&s_Text, Metrics, &Content, Localize("IME text color"), &g_Config.m_QmImeTextColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmImeTextColor, true)), false, nullptr, true);
	DoLine_ColorPicker(&s_SelectedText, Metrics, &Content, Localize("IME selected text color"), &g_Config.m_QmImeSelectedTextColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmImeSelectedTextColor, true)), false, nullptr, true);
	DoLine_ColorPicker(&s_Selection, Metrics, &Content, Localize("IME selection background color"), &g_Config.m_QmImeSelectedColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmImeSelectedColor, true)), false, nullptr, true);
	CUIRect Row, Label, Control;
	Content.HSplitTop(LineHeight, &Row, &Content);
	Row.VSplitLeft(LabelWidth, &Label, &Control);
	SLabelProperties Props;
	Props.m_MaxWidth = Label.w;
	Props.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&Label, Localize("IME font size"), BodySize, TEXTALIGN_ML, Props);
	static int s_FontSize;
	RenderQmSettingsSliderWithValueInput(&s_FontSize, Control, &g_Config.m_QmImeFontSize, 75, 200, "%", PrewarmOnly);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	Content.HSplitTop(LineHeight, &Row, &Content);
	static CButtonContainer s_Reset;
	if(DoButton_Menu(&s_Reset, Localize("Reset IME appearance"), 0, &Row) && !PrewarmOnly && !Ui()->RenderOnly())
	{
		g_Config.m_QmImeBgColor = DefaultConfig::QmImeBgColor;
		g_Config.m_QmImeOpacity = DefaultConfig::QmImeOpacity;
		g_Config.m_QmImeTextColor = DefaultConfig::QmImeTextColor;
		g_Config.m_QmImeSelectedTextColor = DefaultConfig::QmImeSelectedTextColor;
		g_Config.m_QmImeSelectedColor = DefaultConfig::QmImeSelectedColor;
		g_Config.m_QmImeFontSize = DefaultConfig::QmImeFontSize;
	}
	Content.HSplitTop(LineSpacing, nullptr, &Content);
}
