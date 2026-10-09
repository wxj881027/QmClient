#include "QmCardCatalog.h"

#include <engine/shared/config.h>

#include <game/client/QmUi/QmImeAppearance.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/menus.h>
#include <game/client/qm_ime_candidate_popup.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <array>
#include <cmath>

namespace
{
	struct SImeColorControls
	{
		unsigned *m_pColor;
		unsigned m_DefaultColor;
		char *m_pGradient;
		int m_GradientSize;
		int *m_pType;
		int *m_pAngle;
		int *m_pCenterX;
		int *m_pCenterY;
		int *m_pRange;
		int *m_pReverse;
	};

	class CImePrewarmScope
	{
		CUi *m_pUi;
	public:
		CImePrewarmScope(CUi *pUi, bool Prewarm) : m_pUi(Prewarm ? pUi : nullptr)
		{
			if(m_pUi != nullptr)
				m_pUi->BeginRenderOnly();
		}
		~CImePrewarmScope()
		{
			if(m_pUi != nullptr)
				m_pUi->EndRenderOnly();
		}
	};
}

void CMenus::RenderQmFunctionImeContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)
{
	const CImePrewarmScope Prewarm(Ui(), PrewarmOnly);
	const bool ReadOnly = Ui()->RenderOnly();
	const auto &Metrics = CurrentSettingsContentMetrics();
	const float RowHeight = std::max(LineHeight, Metrics.m_ButtonHeight);
	RenderQmFunctionCheckboxRow(Content, RowHeight, LineSpacing, &g_Config.m_QmImeAutoManage, "Auto manage IME while typing", Localize("Auto manage IME while typing"), &g_Config.m_QmImeAutoManage, ReadOnly);
	RenderQmFunctionCheckboxRow(Content, RowHeight, LineSpacing, &g_Config.m_QmNewIme, "New IME", Localize("New IME"), &g_Config.m_QmNewIme, ReadOnly);
	CUIRect Row, Label, Control;
	const auto NextRow = [&]() {
		Content.HSplitTop(RowHeight, &Row, &Content);
		Content.HSplitTop(LineSpacing, nullptr, &Content);
		Row.VSplitLeft(std::min(LabelWidth, Row.w * 0.48f), &Label, &Control);
	};
	const auto DrawLabel = [&](const char *pText) {
		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Label, pText, BodySize, TEXTALIGN_ML, Props);
	};
	NextRow();
	Ui()->DoLabel(&Row, Localize("IME appearance"), BodySize, TEXTALIGN_ML);
	CUIRect Preview;
	Content.HSplitTop(RowHeight * 4.0f, &Preview, &Content);
	Content.HSplitTop(LineSpacing, nullptr, &Content);
	if(!PrewarmOnly)
	{
		DrawRoundedSurface(Ui(), Preview, ColorRGBA(0.5f, 0.5f, 0.5f, 0.12f), ColorRGBA(), 4.0f);
		QmImeRenderStylePreview(GameClient(), Preview);
	}

	// 目录拥有控件状态，分类页与搜索页打开的是同一组编辑器。
	static int s_Role = 0;
	static CUi::SDropDownState s_RoleDropdown;
	static CScrollRegion s_RoleScroll;
	s_RoleDropdown.m_SelectionPopupContext.m_pScrollRegion = &s_RoleScroll;
	const char *apRoles[] = {Localize("IME background color"), Localize("IME text color"), Localize("IME selected text color"), Localize("IME selection background color")};
	NextRow();
	DrawLabel(Localize("Edit color"));
	const int NewRole = DoSettingsDropDown(&Control, s_Role, apRoles, std::size(apRoles), s_RoleDropdown);
	if(!ReadOnly)
		s_Role = std::clamp(NewRole, 0, 3);
	const std::array<SImeColorControls, 4> aControls = {{
		{&g_Config.m_QmImeBgColor, DefaultConfig::QmImeBgColor, g_Config.m_QmImeBgGradient, sizeof(g_Config.m_QmImeBgGradient),
			&g_Config.m_QmImeBgGradientType, &g_Config.m_QmImeBgGradientAngle, &g_Config.m_QmImeBgGradientCenterX, &g_Config.m_QmImeBgGradientCenterY, &g_Config.m_QmImeBgGradientRange, &g_Config.m_QmImeBgGradientReverse},
		{&g_Config.m_QmImeTextColor, DefaultConfig::QmImeTextColor, g_Config.m_QmImeTextGradient, sizeof(g_Config.m_QmImeTextGradient),
			&g_Config.m_QmImeTextGradientType, &g_Config.m_QmImeTextGradientAngle, &g_Config.m_QmImeTextGradientCenterX, &g_Config.m_QmImeTextGradientCenterY, &g_Config.m_QmImeTextGradientRange, &g_Config.m_QmImeTextGradientReverse},
		{&g_Config.m_QmImeSelectedTextColor, DefaultConfig::QmImeSelectedTextColor, g_Config.m_QmImeSelectedTextGradient, sizeof(g_Config.m_QmImeSelectedTextGradient),
			&g_Config.m_QmImeSelectedTextGradientType, &g_Config.m_QmImeSelectedTextGradientAngle, &g_Config.m_QmImeSelectedTextGradientCenterX, &g_Config.m_QmImeSelectedTextGradientCenterY, &g_Config.m_QmImeSelectedTextGradientRange, &g_Config.m_QmImeSelectedTextGradientReverse},
		{&g_Config.m_QmImeSelectedColor, DefaultConfig::QmImeSelectedColor, g_Config.m_QmImeSelectedGradient, sizeof(g_Config.m_QmImeSelectedGradient),
			&g_Config.m_QmImeSelectedGradientType, &g_Config.m_QmImeSelectedGradientAngle, &g_Config.m_QmImeSelectedGradientCenterX, &g_Config.m_QmImeSelectedGradientCenterY, &g_Config.m_QmImeSelectedGradientRange, &g_Config.m_QmImeSelectedGradientReverse},
	}};
	const auto &Settings = aControls[s_Role];
	static std::array<CButtonContainer, 4> s_aReset, s_aAdd, s_aRemove;
	static std::array<std::array<unsigned, CMessageGradient::MAX_COLORS>, 4> s_aColors;
	static std::array<int, 4> s_aOpacityIds;
	const bool HasAlpha = s_Role != 0;
	const auto ColorRow = ResolveSettingsColorRowLayout(Content, Metrics, false);
	Content.y += ColorRow.m_ConsumedHeight;
	Content.h = std::max(0.0f, Content.h - ColorRow.m_ConsumedHeight);
	CUIRect ColorLabel = ColorRow.m_LabelRect;
	ColorLabel.w = ColorRow.m_ColorButtonRect.x + ColorRow.m_ColorButtonRect.w - ColorLabel.x;
	SLabelProperties ColorLabelProps;
	ColorLabelProps.m_MaxWidth = ColorLabel.w;
	ColorLabelProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&ColorLabel, apRoles[s_Role], BodySize, TEXTALIGN_ML, ColorLabelProps);
	if(DoButton_Menu(&s_aReset[s_Role], Localize("Reset"), 0, &ColorRow.m_ResetButtonRect) && !ReadOnly)
	{
		*Settings.m_pColor = Settings.m_DefaultColor;
		CMessageGradient::Reset(Settings.m_pGradient, Settings.m_GradientSize);
		*Settings.m_pType = 0;
		*Settings.m_pAngle = 0;
		*Settings.m_pCenterX = *Settings.m_pCenterY = 50;
		*Settings.m_pRange = 100;
		*Settings.m_pReverse = 0;
		if(s_Role == 0)
			g_Config.m_QmImeOpacity = DefaultConfig::QmImeOpacity;
	}
	DoColorGradientPalette(&Content, Settings.m_pColor, Settings.m_pGradient, Settings.m_GradientSize,
		&s_aAdd[s_Role], &s_aRemove[s_Role], s_aColors[s_Role].data(), Metrics, false, HasAlpha);

	NextRow();
	DrawLabel(Localize("Opacity"));
	int Opacity = HasAlpha ? static_cast<int>(std::round((*Settings.m_pColor & 0xffu) * 100.0f / 255.0f)) : g_Config.m_QmImeOpacity;
	const int OldOpacity = Opacity;
	RenderQmSettingsSliderWithValueInput(&s_aOpacityIds[s_Role], Control, &Opacity, 0, 100, "%", ReadOnly);
	if(Opacity != OldOpacity && !ReadOnly)
	{
		if(HasAlpha)
			*Settings.m_pColor = (*Settings.m_pColor & 0xffffff00u) | static_cast<unsigned>(std::round(Opacity * 255.0f / 100.0f));
		else
			g_Config.m_QmImeOpacity = Opacity;
	}
	NextRow();
	DrawLabel(Localize("Gradient type"));
	const char *apTypes[] = {Localize("Linear gradient"), Localize("Radial gradient"), Localize("Angular gradient"), Localize("Reflected gradient"), Localize("Diamond gradient")};
	static std::array<CUi::SDropDownState, 4> s_aTypeDropdown;
	static std::array<CScrollRegion, 4> s_aTypeScroll;
	s_aTypeDropdown[s_Role].m_SelectionPopupContext.m_pScrollRegion = &s_aTypeScroll[s_Role];
	const int Type = DoSettingsDropDown(&Control, std::clamp(*Settings.m_pType, 0, 4), apTypes, std::size(apTypes), s_aTypeDropdown[s_Role]);
	if(!ReadOnly)
		*Settings.m_pType = Type;
	const auto Slider = [&](const char *pText, int *pValue, int Min, int Max, const char *pUnit) {
		NextRow();
		DrawLabel(pText);
		RenderQmSettingsSliderWithValueInput(pValue, Control, pValue, Min, Max, pUnit, ReadOnly);
	};
	if(*Settings.m_pType == 1)
	{
		NextRow();
		SLabelProperties Props;
		Props.m_MaxWidth = Row.w;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Row, Localize("Radial gradients spread from the center"), BodySize, TEXTALIGN_ML, Props);
	}
	else
		Slider(Localize("Gradient direction"), Settings.m_pAngle, 0, 360, "°");
	Slider(Localize("Gradient horizontal position"), Settings.m_pCenterX, 0, 100, "%");
	Slider(Localize("Gradient vertical position"), Settings.m_pCenterY, 0, 100, "%");
	Slider(Localize("Gradient range"), Settings.m_pRange, 10, 200, "%");
	RenderQmFunctionCheckboxRow(Content, RowHeight, LineSpacing, Settings.m_pReverse, "Reverse gradient colors", Localize("Reverse gradient colors"), Settings.m_pReverse, ReadOnly);
	Slider(Localize("IME font size"), &g_Config.m_QmImeFontSize, 75, 200, "%");
	NextRow();
	static CButtonContainer s_ResetAll;
	if(DoButton_Menu(&s_ResetAll, Localize("Reset IME appearance"), 0, &Row) && !ReadOnly)
	{
		for(const auto &Entry : aControls)
		{
			*Entry.m_pColor = Entry.m_DefaultColor;
			CMessageGradient::Reset(Entry.m_pGradient, Entry.m_GradientSize);
			*Entry.m_pType = 0;
			*Entry.m_pAngle = 0;
			*Entry.m_pCenterX = *Entry.m_pCenterY = 50;
			*Entry.m_pRange = 100;
			*Entry.m_pReverse = 0;
		}
		g_Config.m_QmImeOpacity = DefaultConfig::QmImeOpacity;
		g_Config.m_QmImeFontSize = DefaultConfig::QmImeFontSize;
	}
}
