#include "QmCardCatalogInternal.h"
#include "QmConsoleSchemeList.h"
#include "QmConsoleSettingsLayout.h"

#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/components/console.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/console_syntax.h>
#include <game/client/components/qmclient/console_text.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

void CMenus::RenderQmConsoleContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, bool PrewarmOnly)
{
	CSettingsContentLayoutFlow Flow(Content);
	// F1 面板、分类页和搜索页共用配置与控件；后台帧保留真实控件绘制，只有纯测量帧才进入 RenderOnly。
	const bool MeasureOnly = PrewarmOnly && !Ui()->BackgroundRender();
	if(MeasureOnly)
		Ui()->BeginRenderOnly();
	struct SControls
	{
		ui_widget::SNumericFieldState m_FontSize;
		ui_widget::SNumericFieldState m_Opacity;
		CButtonContainer m_Highlight;
		CButtonContainer m_aSchemeRows[qm_console_scheme_list::Count()];
		CButtonContainer m_aColorReset[QmConsoleAppearance::COLOR_COUNT];
		CButtonContainer m_Reset;
	};
	// 菜单背后仍可能绘制同一卡片；弹窗使用自己的输入状态、ID 和滚动状态。
	// 滚动区与「上次滚到的选中项」也按上下文分开，否则同帧两处渲染会共用
	// 一个偏移：一处滚动会带着另一处跳，选中项也只会在先渲染的那处滚进视野。
	const int RenderContext = Ui()->RenderingPopupMenus() ? 1 : 0;
	static SControls s_aControls[2];
	SControls &Controls = s_aControls[RenderContext];
	const IUiContext Ctx = SettingsUiContext("console_appearance", Metrics.m_UiScale);
	CUIRect Row;
	const auto TakeRow = [&](const char *pId, float Height) {
		const bool HasRow = Flow.Control(pId, Height, Row);
		Flow.Space(Metrics.m_LineSpacing);
		return HasRow;
	};
	const auto Numeric = [&](ui_widget::SNumericFieldState &State, int *pValue, const char *pId, const char *pLabel, int Min, int Max, const char *pSuffix) {
		if(!TakeRow(pId, Metrics.m_LineHeight))
			return;
		ui_widget::SNumericFieldOptions Options;
		Options.m_pLabel = pLabel;
		Options.m_pSuffix = pSuffix;
		Options.m_FontSize = Metrics.m_BodySize;
		ui_widget::NumericField(Ctx, &State, &State, pValue, Min, Max, Row, Options);
	};
	// 一行配色方案就直接画成该方案的样子：行底色是它自己的背景色、名称用它
	// 自己的前景色、左侧是它的 16 色板。这样玩家不用先切过去就能看出效果，
	// 也和 Windows Terminal 的配色列表一致（历史三项用同样的画法，不另立外观）。
	const auto DrawSchemeSwatch = [&](const CUIRect &Rect, const qm_console_scheme_list::SPreview &Preview) {
		// 8x2 ANSI 方格：格子之间留一道行底色做缝隙，圆角小一点，
		// 免得缩到十几个色块时看起来像一整条竖线。
		const float Gap = std::max(0.5f, Rect.h / 16.0f);
		const float CellWidth = (Rect.w - Gap * 7.0f) / 8.0f;
		const float CellHeight = (Rect.h - Gap) / 2.0f;
		if(CellWidth <= 0.0f || CellHeight <= 0.0f)
			return;
		for(int ColorIndex = 0; ColorIndex < 16; ++ColorIndex)
		{
			const CUIRect Cell = {Rect.x + (ColorIndex % 8) * (CellWidth + Gap),
				Rect.y + (ColorIndex / 8) * (CellHeight + Gap), CellWidth, CellHeight};
			Cell.Draw(Preview.m_aSwatch[ColorIndex].WithAlpha(1.0f), IGraphics::CORNER_ALL, std::min(2.0f, CellHeight * 0.35f));
		}
	};
	const auto RenderConsoleSchemeRow = [&](CButtonContainer &Button, const qm_console_scheme_list::SEntry &Entry, CUIRect &RowRect) {
		const bool Selected = g_Config.m_QmConsoleColorScheme == Entry.m_Value;
		// 行是列表项而不是按钮：只在悬停/选中时铺一层底色，不给每行描边，
		// 否则十几行连起来全是框，看起来比内容还重。
		const bool Hovered = !PrewarmOnly && Flow.Input() && Ui()->VisualMouseHovered(&RowRect);
		const bool Activate = Ui()->DoButtonLogic(&Button, 0, &RowRect, BUTTONFLAG_LEFT);
		// 预渲染只走几何与绘制，不消费点击，也不写配置。
		if(!PrewarmOnly && Activate && Flow.Input() && !Ui()->RenderOnly())
			g_Config.m_QmConsoleColorScheme = Entry.m_Value;
		if(!Flow.Drawing() || Ui()->DrawingSuppressed())
			return;
		// 自定义方案必须传真实取色；传空数组会让它在列表里显示成一片黑。
		const auto Preview = qm_console_scheme_list::BuildPreview(Entry, g_Config.m_QmConsoleOpacity, QmConsoleAppearance::CustomColors(g_Config));
		// 行底色就是该方案自己的背景色：每行不再借用卡片底色，
		// 浅色方案（One Half Light 等）才能真正"浅"起来。
		const ColorRGBA RowFill = Preview.m_Background;
		// 选中描边用玩家配置的强调色（qm_ui_accent_color），不是写死的 token 蓝；
		// 强调色带设置里的不透明度，作为前景反馈这里按不透明画。
		const ColorRGBA AccentColor = Ctx.m_pTheme->m_Accent.WithAlpha(1.0f);
		DrawRoundedSurface(Ui(), RowRect, RowFill, Selected ? AccentColor : ColorRGBA(),
			ui_token::radius::BASE, Selected ? 1.5f : 0.0f);
		// 悬停只叠一层极淡的前景色，不改变方案自身的底色观感。
		if(Hovered && !Selected)
			DrawRoundedSurface(Ui(), RowRect, Preview.m_Foreground.WithAlpha(0.10f), ColorRGBA(), ui_token::radius::BASE);
		CUIRect Content = RowRect;
		Content.Margin(std::min(Metrics.m_BodySize * 0.6f, Content.h * 0.3f), &Content);
		// 勾选位固定在右侧；选中与否都不移动文字起点，切换时列表不会跳。
		CUIRect CheckBox;
		Content.VSplitRight(std::min(Content.h, Metrics.m_BodySize * 1.6f), &Content, &CheckBox);
		const float SwatchHeight = std::min(Content.h * 0.86f, Metrics.m_BodySize * 1.9f);
		const float SwatchWidth = std::max(0.0f, std::min(SwatchHeight * 0.95f * 8.0f, Content.w * 0.5f));
		CUIRect Swatch = {Content.x, Content.y + (Content.h - SwatchHeight) * 0.5f, SwatchWidth, SwatchHeight};
		DrawSchemeSwatch(Swatch, Preview);
		CUIRect Label = {Swatch.x + Swatch.w + Metrics.m_BodySize * 0.9f, Content.y,
			std::max(0.0f, Content.w - Swatch.w - Metrics.m_BodySize * 0.9f), Content.h};
		// 名称用该方案自己的前景色，和行底色一起构成这对配色的实际观感。
		{
			const ColorRGBA Previous = TextRender()->GetTextColor();
			TextRender()->TextColor(Preview.m_Foreground);
			Ui()->DoLabel(&Label, qm_console_scheme_list::DisplayName(Entry), Metrics.m_BodySize, TEXTALIGN_ML);
			TextRender()->TextColor(Previous);
		}
		// 选中标记用强调色圆点：图集里的 check 字形在某些字体配置下不可用，
		// 回退成别的图标会画出"×"这种误导性符号，圆点不依赖任何图标字体。
		if(Selected)
		{
			CUIRect Dot;
			CheckBox.VSplitMid(&CheckBox, &Dot);
			const float Diameter = std::min(Dot.h, Dot.w) * 0.55f;
			Dot = {Dot.x + (Dot.w - Diameter) * 0.5f, Dot.y + (Dot.h - Diameter) * 0.5f, Diameter, Diameter};
			DrawRoundedSurface(Ui(), Dot, AccentColor, ColorRGBA(), ui_token::radius::PILL);
		}
	};
	const auto RenderHighlight = [&] {
		if(TakeRow("console-highlight", Metrics.m_LineHeight) && DoButton_CheckBox(&Controls.m_Highlight, Localize("Highlight console commands"), g_Config.m_QmConsoleHighlightCommands, &Row, Metrics.m_BodySize))
			g_Config.m_QmConsoleHighlightCommands ^= 1;
	};
	if(RenderContext == 0)
		RenderHighlight();
	// F1 弹层保持原有完整内容与高度；卡片和搜索页共用个性调整状态。
	if(RenderContext != 0 || qm_card_catalog::QmCardRenderHook::Personalization(this, Content, "qm:console", Metrics, PrewarmOnly || Ui()->RenderOnly()))
	{
		Numeric(Controls.m_FontSize, &g_Config.m_QmConsoleFontSize, "console-font-size", Localize("Console font size"), 1, 24, "");
		Numeric(Controls.m_Opacity, &g_Config.m_QmConsoleOpacity, "console-opacity", Localize("Console background opacity"), 20, 100, "%");
		if(RenderContext != 0)
			RenderHighlight();
		// 配色方案只有一个选择集合：历史三项（现代终端 / 经典 PowerShell / 自定义）
		// 与 Windows Terminal 内置配色同属一个列表，选中任意一行都能再切回其它行。
		// 原先拆成"分段选择器 + 独立列表"，选中内置配色后上半部分就再也点不动了。
		// 配色标题与重置按钮同一行：重置是整份外观的兜底动作，放在标题右侧
		// 比单独吊在列表最底部更顺，也不用为它多占一行高度。
		if(TakeRow("console-scheme-label", Metrics.m_LineHeight))
		{
			CUIRect LabelRow = Row;
			CUIRect ButtonRect;
			const float ButtonWidth = std::min(LabelRow.w * 0.42f, 132.0f * Metrics.m_UiScale);
			LabelRow.VSplitRight(ButtonWidth, &LabelRow, &ButtonRect);
			Ui()->DoLabel(&LabelRow, Localize("Console color scheme"), Metrics.m_BodySize, TEXTALIGN_ML);
			ButtonRect.HMargin(std::max(0.0f, (ButtonRect.h - Metrics.m_ListRowHeight) * 0.5f), &ButtonRect);
			if(ui_widget::SecondaryButton(Ctx, &Controls.m_Reset, Localize("Reset console appearance"), ButtonRect, PrewarmOnly) && Flow.Input() && !Ui()->RenderOnly())
				QmConsoleAppearance::Reset(g_Config);
		}
		if(TakeRow("console-scheme-list", QmConsoleSettingsLayout::SchemeListHeight(Metrics)))
		{
			CUIRect List = Row;
			vec2 Offset(0.0f, 0.0f);
			static CScrollRegion s_aSchemeScrollRegions[2];
			CScrollRegion &SchemeScrollRegion = s_aSchemeScrollRegions[RenderContext];
			// 轨道尺寸与设置页其它卡片内列表一致（SMALL 紧凑轨道）。
			// 滑块颜色必须和其它滚动条同源：CScrollRegionParams 的默认滑块色是写死的
			// 0.8 灰（m_SliderColor），只有在带动画运行时才走主题强调色那条分支；
			// 这里把主题指针和旧字段都按同一个强调色填上，两条分支都不会掉成灰条。
			auto ScrollParams = QmScrollRegionParamsForSize(EQmScrollSize::SMALL, Metrics.m_UiScale);
			ScrollParams.m_Interactive = !PrewarmOnly && Flow.Input();
			ScrollParams.m_WheelOwnerPriority = EUiWheelOwnerPriority::COMPOSITE_CONTROL;
			// 强调色本身带设置里的不透明度（默认 24%），直接拿去画滑块会淡到看不见；
			// 滑块是前景反馈，这里按不透明强调色画，轨道保持低调。
			static ColorRGBA s_SliderAccent;
			s_SliderAccent = Ctx.m_pTheme->m_Accent.WithAlpha(1.0f);
			ScrollParams.m_pSliderThemeColor = &s_SliderAccent;
			ScrollParams.m_SliderColor = s_SliderAccent;
			ScrollParams.m_SliderColorHover = s_SliderAccent;
			ScrollParams.m_SliderColorGrabbed = s_SliderAccent;
			SchemeScrollRegion.Begin(&List, &Offset, &ScrollParams);
			CUIRect Content = List;
			Content.y += Offset.y;
			// 选中行滚进可见区域只在选中项变化时做一次；每帧都请求会把玩家人工
			// 滚动的位置一直拽回去，列表就再也翻不动了。
			const int SelectedIndex = qm_console_scheme_list::IndexOf(g_Config.m_QmConsoleColorScheme);
			static int s_aLastScrolledIndex[2] = {-1, -1};
			const bool ScrollToSelected = SelectedIndex >= 0 && SelectedIndex != s_aLastScrolledIndex[RenderContext];
			if(SelectedIndex >= 0)
				s_aLastScrolledIndex[RenderContext] = SelectedIndex;
			for(int Index = 0; Index < qm_console_scheme_list::Count(); ++Index)
			{
				CUIRect ItemRect;
				Content.HSplitTop(QmConsoleSettingsLayout::SchemeRowHeight(Metrics), &ItemRect, &Content);
				ItemRect.HMargin(Metrics.m_LineSpacing * 0.5f, &ItemRect);
				if(!SchemeScrollRegion.AddRect(ItemRect, ScrollToSelected && Index == SelectedIndex))
					continue;
				RenderConsoleSchemeRow(Controls.m_aSchemeRows[Index], qm_console_scheme_list::All()[Index], ItemRect);
			}
			CUIRect EndRect = {List.x, Content.y, List.w, 0.0f};
			SchemeScrollRegion.AddRect(EndRect);
			SchemeScrollRegion.End();
			Row = List;
		}
		// 选中「自定义」时的九项取色。放在列表之后：列表是滚动区，
		// 行内控件不能塞进滚动内容里，否则会被滚动裁剪、点不到。
		if(QmConsoleAppearance::IsCustomScheme(g_Config.m_QmConsoleColorScheme))
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
			{
				char aId[32];
				str_format(aId, sizeof(aId), "console-color-%zu", i);
				if(Flow.Control(aId, ResolveSettingsColorRowLayout({}, Metrics, false).m_ConsumedHeight, Row))
				{
					CUIRect ColorRow = Row;
					DoLine_ColorPicker(&Controls.m_aColorReset[i], Metrics, &ColorRow, aColors[i].m_pLabel, aColors[i].m_pValue,
						color_cast<ColorRGBA>(ColorHSLA(aColors[i].m_Default)), false, nullptr, false, true);
				}
			}
		}
	}
	const auto Palette = QmConsoleAppearance::Palette(g_Config);
	const float FontSize = QmConsoleAppearance::FontSize(g_Config.m_QmConsoleFontSize);
	if(TakeRow("console-preview", QmConsoleSettingsLayout::PreviewHeight(g_Config.m_QmConsoleFontSize)) && Flow.Drawing() && !Ui()->DrawingSuppressed())
	{
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
	}
	if(MeasureOnly)
		Ui()->EndRenderOnly();
}

namespace qm_card_catalog
{
	bool BuildConsoleCard(const SQmCardBuildContext &Ctx, SSettingsCardDefinition &Out)
	{
		MakeModuleCard(
			Ctx,
			qm_module::EQmModuleId::Console,
			"qm:console",
			"Console settings",
			"Appearance and command highlighting of the local console",
			[Ctx](CUIRect &Content) { QmCardRenderHook::RenderQmConsoleContent(Ctx.m_pMenus, Content, Ctx.m_Metrics, Ctx.m_ReadOnly); },
			Out);
		UseDeclaredModuleContent(Ctx, Out);
		return true;
	}
}

void CGameConsole::OpenSettings()
{
	const CUIRect Panel = QmConsoleSettingsLayout::PanelRect(*Ui()->Screen(), QmConsoleAppearance::IsCustomScheme(g_Config.m_QmConsoleColorScheme), g_Config.m_QmConsoleFontSize);
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
		Active && QmConsoleSettingsLayout::ContentHeight(Metrics, QmConsoleAppearance::IsCustomScheme(g_Config.m_QmConsoleColorScheme), g_Config.m_QmConsoleFontSize) > View.h);
	pThis->m_SettingsScrollRegion.SetContentHeightForNextFrame(QmConsoleSettingsLayout::ContentHeight(Metrics, QmConsoleAppearance::IsCustomScheme(g_Config.m_QmConsoleColorScheme), g_Config.m_QmConsoleFontSize));
	pThis->m_SettingsScrollRegion.Begin(&View, &Offset, &Params);
	View.y += Offset.y;
	const float StartY = View.y;
	CUIRect Content = View;
	Content.h = QmConsoleSettingsLayout::ContentHeight(Metrics, QmConsoleAppearance::IsCustomScheme(g_Config.m_QmConsoleColorScheme), g_Config.m_QmConsoleFontSize);
	qm_card_catalog::QmCardRenderHook::RenderQmConsoleContent(&pThis->GameClient()->m_Menus, Content, Metrics, false);
	pThis->m_SettingsScrollRegion.AddRect({View.x, StartY, View.w, Content.y - StartY});
	pThis->m_SettingsScrollRegion.End();
	if(!Close && Active)
	{
		// 更新同一弹层的几何，保留控件输入、滚动和嵌套颜色选择器状态。
		const CUIRect Next = QmConsoleSettingsLayout::PanelRect(*pThis->Ui()->Screen(), QmConsoleAppearance::IsCustomScheme(g_Config.m_QmConsoleColorScheme), g_Config.m_QmConsoleFontSize);
		auto Props = ui_widget::SecondaryPanelProperties();
		pThis->Ui()->DoPopupMenu(&pThis->m_SettingsPopupId, Next.x, Next.y, Next.w, Next.h, pThis, PopupSettings, Props);
	}
	return Close ? CUi::POPUP_CLOSE_CURRENT : CUi::POPUP_KEEP_OPEN;
}
