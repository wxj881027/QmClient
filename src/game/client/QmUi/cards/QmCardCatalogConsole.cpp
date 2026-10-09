#include "QmCardCatalogInternal.h"
#include "QmCardMeasureRevision.h"
#include "QmConsoleSettingsLayout.h"

#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/components/console.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/console_syntax.h>
#include <game/client/components/qmclient/console_text.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

void CMenus::RenderQmConsoleContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, bool PrewarmOnly)
{
	// F1 面板、分类页和搜索页共用配置与控件；测量不处理输入，失活父层由弹窗栈阻断交互。
	if(PrewarmOnly)
		Ui()->BeginRenderOnly();
	struct SControls
	{
		ui_widget::SNumericFieldState m_FontSize;
		ui_widget::SNumericFieldState m_Opacity;
		CButtonContainer m_Highlight;
		CButtonContainer m_aSchemes[3];
		CButtonContainer m_aColorReset[QmConsoleAppearance::COLOR_COUNT];
		CButtonContainer m_Reset;
	};
	// 菜单背后仍可能绘制同一卡片；弹窗使用自己的输入状态和 ID。
	static SControls s_aControls[2];
	SControls &Controls = s_aControls[Ui()->RenderingPopupMenus() ? 1 : 0];
	const IUiContext Ctx = SettingsUiContext("console_appearance", Metrics.m_UiScale);
	CUIRect Row;
	const auto TakeRow = [&]() {
		Content.HSplitTop(Metrics.m_LineHeight, &Row, &Content);
		Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
	};
	const auto Numeric = [&](ui_widget::SNumericFieldState &State, int *pValue, const char *pLabel, int Min, int Max, const char *pSuffix) {
		TakeRow();
		ui_widget::SNumericFieldOptions Options;
		Options.m_pLabel = pLabel;
		Options.m_pSuffix = pSuffix;
		Options.m_FontSize = Metrics.m_BodySize;
		ui_widget::NumericField(Ctx, &State, &State, pValue, Min, Max, Row, Options);
	};
	Numeric(Controls.m_FontSize, &g_Config.m_QmConsoleFontSize, Localize("Console font size"), 1, 24, "");
	Numeric(Controls.m_Opacity, &g_Config.m_QmConsoleOpacity, Localize("Console background opacity"), 20, 100, "%");
	TakeRow();
	if(DoButton_CheckBox(&Controls.m_Highlight, Localize("Highlight console commands"), g_Config.m_QmConsoleHighlightCommands, &Row, Metrics.m_BodySize))
		g_Config.m_QmConsoleHighlightCommands ^= 1;
	TakeRow();
	Ui()->DoLabel(&Row, Localize("Console color scheme"), Metrics.m_BodySize, TEXTALIGN_ML);
	const char *apSchemes[] = {Localize("Modern terminal"), Localize("Classic PowerShell"), Localize("Custom")};
	// 预设各占一行，窄窗口和长译文都保留完整按钮。
	for(int i = 0; i < 3; ++i)
	{
		TakeRow();
		if(DoButton_MenuTab(&Controls.m_aSchemes[i], apSchemes[i], g_Config.m_QmConsoleColorScheme == i, &Row, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, ui_token::radius::BASE, nullptr, nullptr, Metrics.m_BodySize))
			g_Config.m_QmConsoleColorScheme = i;
	}
	if(g_Config.m_QmConsoleColorScheme == 2)
	{
		struct SColorSetting
		{
			const char *m_pLabel;
			unsigned *m_pValue;
			unsigned m_Default;
		};
		const SColorSetting aColors[] = {
			{Localize("Console background color"), &g_Config.m_QmConsoleBackgroundColor, DefaultConfig::QmConsoleBackgroundColor},
			{Localize("Console text color"), &g_Config.m_QmConsoleTextColor, DefaultConfig::QmConsoleTextColor},
			{Localize("Console command color"), &g_Config.m_QmConsoleCommandColor, DefaultConfig::QmConsoleCommandColor},
			{Localize("Console parameter color"), &g_Config.m_QmConsoleParameterColor, DefaultConfig::QmConsoleParameterColor},
			{Localize("Console string color"), &g_Config.m_QmConsoleStringColor, DefaultConfig::QmConsoleStringColor},
			{Localize("Console number color"), &g_Config.m_QmConsoleNumberColor, DefaultConfig::QmConsoleNumberColor},
			{Localize("Console link color"), &g_Config.m_QmConsoleLinkColor, DefaultConfig::QmConsoleLinkColor},
			{Localize("Console search match color"), &g_Config.m_QmConsoleSearchColor, DefaultConfig::QmConsoleSearchColor},
			{Localize("Console selected search match color"), &g_Config.m_QmConsoleSearchSelectedColor, DefaultConfig::QmConsoleSearchSelectedColor}};
		for(size_t i = 0; i < std::size(aColors); ++i)
			DoLine_ColorPicker(&Controls.m_aColorReset[i], Metrics, &Content, aColors[i].m_pLabel, aColors[i].m_pValue,
				color_cast<ColorRGBA>(ColorHSLA(aColors[i].m_Default)), false, nullptr, false, false);
	}

	const auto Palette = QmConsoleAppearance::Palette(g_Config);
	const float FontSize = QmConsoleAppearance::FontSize(g_Config.m_QmConsoleFontSize);
	Content.HSplitTop(QmConsoleSettingsLayout::PreviewHeight(g_Config.m_QmConsoleFontSize), &Row, &Content);
	Row.Draw(Palette.m_aColors[QmConsoleAppearance::BACKGROUND], IGraphics::CORNER_ALL, 5.0f);
	Row.Margin(6.0f, &Row);
	const ColorRGBA Previous = TextRender()->GetTextColor();
	TextRender()->TextColor(Palette.m_aColors[QmConsoleAppearance::TEXT]);
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(Row.x, Row.y));
	Cursor.m_FontSize = FontSize;
	Cursor.m_LineWidth = Row.w;
	Cursor.m_MaxLines = 2;
	const char *pPreview = "> echo \"QmClient\"; player_color 42\nhttps://ddnet.org";
	std::vector<STextColorSplit> vColors;
	if(g_Config.m_QmConsoleHighlightCommands)
		QmConsoleSyntax::AppendColors(pPreview + 2, Palette, vColors, 2);
	const char *pLink = str_find(pPreview, "https://");
	vColors.emplace_back(static_cast<int>(pLink - pPreview), str_length(pLink), Palette.m_aColors[QmConsoleAppearance::LINK]);
	QmConsoleText::ComposeColorSplits(pPreview, vColors, Cursor.m_vColorSplits);
	TextRender()->TextEx(&Cursor, pPreview);
	TextRender()->TextColor(Previous);
	Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
	TakeRow();
	if(ui_widget::SecondaryButton(Ctx, &Controls.m_Reset, Localize("Reset console appearance"), Row, Ui()->RenderOnly()))
		QmConsoleAppearance::Reset(g_Config);
	if(PrewarmOnly)
		Ui()->EndRenderOnly();
}

namespace qm_card_catalog
{
	bool BuildConsoleCard(const SQmCardBuildContext &Ctx, SSettingsCardDefinition &Out)
	{
		MakeModuleCard(Ctx, qm_module::EQmModuleId::Console, "qm:console", "Console settings", "Appearance and command highlighting of the local console", [Ctx](CUIRect &Content) { QmCardRenderHook::RenderQmConsoleContent(Ctx.m_pMenus, Content, Ctx.m_Metrics, Ctx.m_ReadOnly); }, [Metrics = Ctx.m_Metrics](float) { return QmConsoleSettingsLayout::ContentHeight(Metrics, g_Config.m_QmConsoleColorScheme == 2, g_Config.m_QmConsoleFontSize); }, MeasureModuleCardRevision(qm_module::EQmModuleId::Console), {}, Out);
		return true;
	}
}

void CGameConsole::OpenSettings()
{
	const CUIRect Panel = QmConsoleSettingsLayout::PanelRect(*Ui()->Screen(), g_Config.m_QmConsoleColorScheme == 2, g_Config.m_QmConsoleFontSize);
	m_SettingsScrollRegion.Reset();
	m_LocalConsole.m_Selection.Finish();
	m_LocalConsole.m_MouseIsPress = false;
	m_LocalConsole.m_ScrollbarDragging = false;
	m_LocalConsole.m_Input.GetMouseSelection()->m_Selecting = false;
	m_TouchState.m_ScrollAmount = vec2(0.0f, 0.0f);
	auto Props = ui_widget::SecondaryPanelProperties();
	// 入场与退场均由公共弹层栈驱动，控制台绘制入口保留退场帧。
	Ui()->DoPopupMenu(&m_SettingsPopupId, Panel.x, Panel.y, Panel.w, Panel.h, this, PopupSettings, Props);
}

CUi::EPopupMenuFunctionResult CGameConsole::PopupSettings(void *pContext, CUIRect View, bool Active)
{
	auto *pThis = static_cast<CGameConsole *>(pContext);
	const auto HeaderMetrics = ui_widget::ResolveSecondaryPanelMetrics(View.w);
	static ui_widget::SSecondaryPanelLabel s_Title;
	ui_widget::CSecondaryPanel Panel(ui_widget::ControlContext(pThis->Ui()), View, Active, HeaderMetrics, {});
	const bool Close = Panel.Header(s_Title, pThis->m_SettingsCloseButton, Localize("Console settings"));
	View = Panel.ContentRect();
	View.h = std::max(0.0f, View.h);
	const auto Metrics = ResolveSettingsContentMetrics(View.w);
	vec2 Offset;
	auto Params = QmScrollRegionParamsForSize(EQmScrollSize::MEDIUM, Metrics.m_UiScale);
	Params.m_Interactive = Active;
	Params.m_WheelOwnerPriority = EUiWheelOwnerPriority::POPUP;
	// 弹层显式登记滚轮所有权，不依赖底层控制台的热滚动区。
	Params.m_pWheelOwnerId = &pThis->m_SettingsPopupId;
	Params.m_WheelOwnerPreRegistered = true;
	pThis->Ui()->RegisterWheelOwner(Params.m_pWheelOwnerId, Params.m_WheelOwnerPriority, View,
		Active && QmConsoleSettingsLayout::ContentHeight(Metrics, g_Config.m_QmConsoleColorScheme == 2, g_Config.m_QmConsoleFontSize) > View.h);
	pThis->m_SettingsScrollRegion.Begin(&View, &Offset, &Params);
	View.y += Offset.y;
	const float StartY = View.y;
	CUIRect Content = View;
	Content.h = QmConsoleSettingsLayout::ContentHeight(Metrics, g_Config.m_QmConsoleColorScheme == 2, g_Config.m_QmConsoleFontSize);
	qm_card_catalog::QmCardRenderHook::RenderQmConsoleContent(&pThis->GameClient()->m_Menus, Content, Metrics, false);
	pThis->m_SettingsScrollRegion.AddRect({View.x, StartY, View.w, Content.y - StartY});
	pThis->m_SettingsScrollRegion.End();
	if(!Close && Active)
	{
		// 更新同一弹层的几何，保留控件输入、滚动和嵌套颜色选择器状态。
		const CUIRect Next = QmConsoleSettingsLayout::PanelRect(*pThis->Ui()->Screen(), g_Config.m_QmConsoleColorScheme == 2, g_Config.m_QmConsoleFontSize);
		auto Props = ui_widget::SecondaryPanelProperties();
		pThis->Ui()->DoPopupMenu(&pThis->m_SettingsPopupId, Next.x, Next.y, Next.w, Next.h, pThis, PopupSettings, Props);
	}
	return Close ? CUi::POPUP_CLOSE_CURRENT : CUi::POPUP_KEEP_OPEN;
}
