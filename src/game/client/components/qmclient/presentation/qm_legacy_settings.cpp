/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include "qm_card_settings_view.h"

#include <engine/shared/config.h>

#include <game/localization.h>

namespace
{
constexpr float LINE_HEIGHT = 20.0f;
constexpr float MULTILINE_LINE_HEIGHT = 40.0f;
constexpr float COLOR_LINE_HEIGHT = 25.0f;
constexpr float ROW_GAP = 5.0f;
constexpr float SECTION_GAP = 10.0f;
constexpr float COLUMN_GAP = 20.0f;
}

void CMenus::RenderSettingsQmClient(CUIRect MainView)
{
	CQmCardSettingsView *pCardView = GameClient()->m_QmRuntime.CardSettingsView();
	if(pCardView && pCardView->Render(*this, *Ui(), *Input(), *Storage(), *Graphics(), GameClient()->m_Tooltips, MainView))
		return;
	CUIRect Header, Row, Left, Right;
	if(!pCardView)
	{
		MainView.HSplitTop(30.0f, &Header, &MainView);
		Ui()->DoLabel(&Header, Localize("QmClient"), 20.0f, TEXTALIGN_ML);
		MainView.HSplitTop(LINE_HEIGHT, &Row, &MainView);
		if(DoButton_CheckBox(&g_Config.m_QmDiagnostics, Localize("Write automatic diagnostics (restart required)"), g_Config.m_QmDiagnostics, &Row))
			g_Config.m_QmDiagnostics ^= 1;
	}

	MainView.HSplitTop(SECTION_GAP, nullptr, &MainView);
	MainView.HSplitTop(LINE_HEIGHT, &Header, &MainView);
	Ui()->DoLabel(&Header, Localize("Player indicators"), 16.0f, TEXTALIGN_ML);
	MainView.HSplitTop(ROW_GAP, nullptr, &MainView);
	CUIRect Options;
	const bool TwoColumns = MainView.w >= 500.0f;
	MainView.HSplitTop(TwoColumns ? 180.0f : 350.0f, &Options, &MainView);
	if(TwoColumns)
		Options.VSplitMid(&Left, &Right, COLUMN_GAP);
	else
		Options.HSplitTop(165.0f, &Left, &Right);

	Left.HSplitTop(LINE_HEIGHT, &Header, &Left);
	if(DoButton_CheckBox(&g_Config.m_QmPlayerIndicator, Localize("Show player indicators"), g_Config.m_QmPlayerIndicator, &Header))
		g_Config.m_QmPlayerIndicator ^= 1;

	Left.HSplitTop(ROW_GAP, nullptr, &Left);
	Left.HSplitTop(LINE_HEIGHT, &Header, &Left);
	if(DoButton_CheckBox(&g_Config.m_QmPlayerIndicatorTeamOnly, Localize("Only show teammates"), g_Config.m_QmPlayerIndicatorTeamOnly, &Header))
		g_Config.m_QmPlayerIndicatorTeamOnly ^= 1;

	Left.HSplitTop(ROW_GAP, nullptr, &Left);
	Left.HSplitTop(LINE_HEIGHT, &Header, &Left);
	if(DoButton_CheckBox(&g_Config.m_QmPlayerIndicatorFrozenOnly, Localize("Only show frozen players"), g_Config.m_QmPlayerIndicatorFrozenOnly, &Header))
		g_Config.m_QmPlayerIndicatorFrozenOnly ^= 1;

	Left.HSplitTop(ROW_GAP, nullptr, &Left);
	Left.HSplitTop(LINE_HEIGHT, &Header, &Left);
	if(DoButton_CheckBox(&g_Config.m_QmPlayerIndicatorHideVisible, Localize("Hide visible players"), g_Config.m_QmPlayerIndicatorHideVisible, &Header))
		g_Config.m_QmPlayerIndicatorHideVisible ^= 1;

	Left.HSplitTop(ROW_GAP, nullptr, &Left);
	Left.HSplitTop(LINE_HEIGHT, &Header, &Left);
	if(DoButton_CheckBox(&g_Config.m_QmPlayerIndicatorVariableDistance, Localize("Use variable distance"), g_Config.m_QmPlayerIndicatorVariableDistance, &Header))
		g_Config.m_QmPlayerIndicatorVariableDistance ^= 1;

	Left.HSplitTop(ROW_GAP, nullptr, &Left);
	Left.HSplitTop(LINE_HEIGHT, &Header, &Left);
	if(DoButton_CheckBox(&g_Config.m_QmPlayerIndicatorUseTees, Localize("Use tee icons"), g_Config.m_QmPlayerIndicatorUseTees, &Header))
		g_Config.m_QmPlayerIndicatorUseTees ^= 1;

	Right.HSplitTop(LINE_HEIGHT, &Header, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_QmPlayerIndicatorOffset, &g_Config.m_QmPlayerIndicatorOffset, &Header, Localize("Indicator offset"), 16, 200);
	Right.HSplitTop(ROW_GAP, nullptr, &Right);
	Right.HSplitTop(LINE_HEIGHT, &Header, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_QmPlayerIndicatorOffsetMax, &g_Config.m_QmPlayerIndicatorOffsetMax, &Header, Localize("Maximum offset"), 16, 200);
	Right.HSplitTop(ROW_GAP, nullptr, &Right);
	Right.HSplitTop(LINE_HEIGHT, &Header, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_QmPlayerIndicatorMaxDistance, &g_Config.m_QmPlayerIndicatorMaxDistance, &Header, Localize("Maximum distance"), 500, 7000);
	Right.HSplitTop(ROW_GAP, nullptr, &Right);
	Right.HSplitTop(LINE_HEIGHT, &Header, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_QmPlayerIndicatorRadius, &g_Config.m_QmPlayerIndicatorRadius, &Header, Localize("Indicator radius"), 1, 16);
	Right.HSplitTop(ROW_GAP, nullptr, &Right);
	Right.HSplitTop(MULTILINE_LINE_HEIGHT, &Header, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_QmPlayerIndicatorOpacity, &g_Config.m_QmPlayerIndicatorOpacity, &Header, Localize("Indicator opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, "%");

	MainView.HSplitTop(SECTION_GAP, nullptr, &MainView);
	MainView.HSplitTop(LINE_HEIGHT, &Header, &MainView);
	Ui()->DoLabel(&Header, Localize("Indicator colors"), 16.0f, TEXTALIGN_ML);
	MainView.HSplitTop(ROW_GAP, nullptr, &MainView);
	static CButtonContainer s_AliveColor, s_FrozenColor, s_UnfreezingColor;
	DoLine_ColorPicker(&s_AliveColor, COLOR_LINE_HEIGHT, 13.0f, ROW_GAP, &MainView, Localize("Alive color"), &g_Config.m_QmPlayerIndicatorAliveColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false);
	DoLine_ColorPicker(&s_FrozenColor, COLOR_LINE_HEIGHT, 13.0f, ROW_GAP, &MainView, Localize("Frozen color"), &g_Config.m_QmPlayerIndicatorFrozenColor, ColorRGBA(0.0f, 1.0f, 1.0f, 1.0f), false);
	DoLine_ColorPicker(&s_UnfreezingColor, COLOR_LINE_HEIGHT, 13.0f, ROW_GAP, &MainView, Localize("Unfreezing color"), &g_Config.m_QmPlayerIndicatorUnfreezingColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), false);
	if(pCardView)
		pCardView->EndLegacy(MainView);
}
