/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include <game/client/components/menus.h>

#include <engine/shared/config.h>

#include <game/localization.h>

namespace
{
constexpr float LINE_HEIGHT = 20.0f;
constexpr float COLOR_LINE_HEIGHT = 25.0f;
constexpr float COLUMN_GAP = 20.0f;
}

void CMenus::RenderSettingsQmClient(CUIRect MainView)
{
	CUIRect Header, Left, Right;
	MainView.HSplitTop(30.0f, &Header, &MainView);
	Ui()->DoLabel(&Header, Localize("QmClient"), 20.0f, TEXTALIGN_ML);
	MainView.HSplitTop(5.0f, nullptr, &MainView);
	MainView.VSplitMid(&Left, &Right, COLUMN_GAP);

	Left.HSplitTop(LINE_HEIGHT, &Header, &Left);
	if(DoButton_CheckBox(&g_Config.m_QmDiagnostics, Localize("Write automatic diagnostics (restart required)"), g_Config.m_QmDiagnostics, &Header))
		g_Config.m_QmDiagnostics ^= 1;

	Left.HSplitTop(5.0f, nullptr, &Left);
	Left.HSplitTop(LINE_HEIGHT, &Header, &Left);
	if(DoButton_CheckBox(&g_Config.m_QmPlayerIndicator, Localize("Show player indicators"), g_Config.m_QmPlayerIndicator, &Header))
		g_Config.m_QmPlayerIndicator ^= 1;

	Left.HSplitTop(5.0f, nullptr, &Left);
	Left.HSplitTop(LINE_HEIGHT, &Header, &Left);
	if(DoButton_CheckBox(&g_Config.m_QmPlayerIndicatorTeamOnly, Localize("Only show teammates"), g_Config.m_QmPlayerIndicatorTeamOnly, &Header))
		g_Config.m_QmPlayerIndicatorTeamOnly ^= 1;

	Left.HSplitTop(5.0f, nullptr, &Left);
	Left.HSplitTop(LINE_HEIGHT, &Header, &Left);
	if(DoButton_CheckBox(&g_Config.m_QmPlayerIndicatorFrozenOnly, Localize("Only show frozen players"), g_Config.m_QmPlayerIndicatorFrozenOnly, &Header))
		g_Config.m_QmPlayerIndicatorFrozenOnly ^= 1;

	Left.HSplitTop(5.0f, nullptr, &Left);
	Left.HSplitTop(LINE_HEIGHT, &Header, &Left);
	if(DoButton_CheckBox(&g_Config.m_QmPlayerIndicatorHideVisible, Localize("Hide visible players"), g_Config.m_QmPlayerIndicatorHideVisible, &Header))
		g_Config.m_QmPlayerIndicatorHideVisible ^= 1;

	Left.HSplitTop(5.0f, nullptr, &Left);
	Left.HSplitTop(LINE_HEIGHT, &Header, &Left);
	if(DoButton_CheckBox(&g_Config.m_QmPlayerIndicatorVariableDistance, Localize("Use variable distance"), g_Config.m_QmPlayerIndicatorVariableDistance, &Header))
		g_Config.m_QmPlayerIndicatorVariableDistance ^= 1;

	Right.HSplitTop(LINE_HEIGHT, &Header, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_QmPlayerIndicatorOffset, &g_Config.m_QmPlayerIndicatorOffset, &Header, Localize("Indicator offset"), 16, 200);
	Right.HSplitTop(5.0f, nullptr, &Right);
	Right.HSplitTop(LINE_HEIGHT, &Header, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_QmPlayerIndicatorOffsetMax, &g_Config.m_QmPlayerIndicatorOffsetMax, &Header, Localize("Maximum offset"), 16, 200);
	Right.HSplitTop(5.0f, nullptr, &Right);
	Right.HSplitTop(LINE_HEIGHT, &Header, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_QmPlayerIndicatorMaxDistance, &g_Config.m_QmPlayerIndicatorMaxDistance, &Header, Localize("Maximum distance"), 500, 7000);
	Right.HSplitTop(5.0f, nullptr, &Right);
	Right.HSplitTop(LINE_HEIGHT, &Header, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_QmPlayerIndicatorRadius, &g_Config.m_QmPlayerIndicatorRadius, &Header, Localize("Indicator radius"), 1, 16);
	Right.HSplitTop(5.0f, nullptr, &Right);
	Right.HSplitTop(LINE_HEIGHT, &Header, &Right);
	Ui()->DoScrollbarOption(&g_Config.m_QmPlayerIndicatorOpacity, &g_Config.m_QmPlayerIndicatorOpacity, &Header, Localize("Indicator opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, "%");
	Right.HSplitTop(5.0f, nullptr, &Right);
	Right.HSplitTop(LINE_HEIGHT, &Header, &Right);
	if(DoButton_CheckBox(&g_Config.m_QmPlayerIndicatorUseTees, Localize("Use tee icons"), g_Config.m_QmPlayerIndicatorUseTees, &Header))
		g_Config.m_QmPlayerIndicatorUseTees ^= 1;

	MainView.HSplitTop(10.0f, nullptr, &MainView);
	MainView.VSplitMid(&Left, &Right, COLUMN_GAP);
	static CButtonContainer s_AliveColor, s_FrozenColor, s_UnfreezingColor;
	DoLine_ColorPicker(&s_AliveColor, COLOR_LINE_HEIGHT, 13.0f, 5.0f, &Left, Localize("Alive color"), &g_Config.m_QmPlayerIndicatorAliveColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	DoLine_ColorPicker(&s_FrozenColor, COLOR_LINE_HEIGHT, 13.0f, 5.0f, &Left, Localize("Frozen color"), &g_Config.m_QmPlayerIndicatorFrozenColor, ColorRGBA(0.0f, 1.0f, 1.0f, 1.0f));
	DoLine_ColorPicker(&s_UnfreezingColor, COLOR_LINE_HEIGHT, 13.0f, 5.0f, &Right, Localize("Unfreezing color"), &g_Config.m_QmPlayerIndicatorUnfreezingColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
}
