#include <engine/client/backend/vulkan/backend_vulkan.h>
#include <engine/client/backend_sdl.h>
#include <engine/client/plausible_sizes.h>
#include <engine/client/rounded_rect_geometry.h>
#include <engine/storage.h>

#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/camera.h>
#include <game/client/components/controls.h>
#include <game/client/components/menus.h>
#include <game/client/components/nameplate_text_effects.h>
#include <game/client/components/nameplates.h>
#include <game/client/components/qmclient/axiom_auto_login.h>
#include <game/client/components/qmclient/demo_cut.h>
#include <game/client/components/qmclient/demo_display.h>
#include <game/client/components/qmclient/demo_ui.h>
#include <game/client/components/qmclient/friend_heart_icon.h>
#include <game/client/components/qmclient/friends_category_drag.h>
#include <game/client/components/qmclient/nameplate_layout.h>
#include <game/client/components/qmclient/spectator_friend_priority.h>
#include <game/client/components/qmclient/spectator_tele_search.h>
#include <game/client/components/tclient/statusbar.h>
#include <game/client/components/tooltips.h>
#include <game/client/prediction/gameworld.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <algorithm>
#include <regex>

namespace
{

	std::string ReadTextFile(const char *pPath)
	{
		std::string Content = ReadTestSourceFile(pPath);
		// menus_settings.cpp ships with CRLF line terminators; normalize so the
		// multi-line BlockBodyAfter anchors below match regardless of source EOL.
		Content.erase(std::remove(Content.begin(), Content.end(), '\r'), Content.end());
		return Content;
	}

	std::string FunctionBody(const std::string &Source, const std::string &Signature)
	{
		const size_t FunctionStart = Source.find(Signature);
		EXPECT_NE(FunctionStart, std::string::npos) << Signature;
		const size_t BodyStart = Source.find("{", FunctionStart);
		EXPECT_NE(BodyStart, std::string::npos) << Signature;
		int Depth = 0;
		for(size_t Index = BodyStart; Index < Source.size(); ++Index)
		{
			if(Source[Index] == '{')
				++Depth;
			else if(Source[Index] == '}')
			{
				--Depth;
				if(Depth == 0)
					return Source.substr(BodyStart, Index - BodyStart);
			}
		}
		ADD_FAILURE() << Signature;
		return {};
	}

	std::string BlockBodyAfter(const std::string &Source, const std::string &Anchor)
	{
		const size_t AnchorPos = Source.find(Anchor);
		EXPECT_NE(AnchorPos, std::string::npos) << Anchor;
		const size_t BodyStart = Source.find("{", AnchorPos);
		EXPECT_NE(BodyStart, std::string::npos) << Anchor;
		int Depth = 0;
		for(size_t Index = BodyStart; Index < Source.size(); ++Index)
		{
			if(Source[Index] == '{')
				++Depth;
			else if(Source[Index] == '}')
			{
				--Depth;
				if(Depth == 0)
					return Source.substr(BodyStart, Index - BodyStart);
			}
		}
		ADD_FAILURE() << Anchor;
		return {};
	}

	size_t MatchingBrace(const std::string &Source, size_t BodyStart)
	{
		int Depth = 0;
		for(size_t Index = BodyStart; Index < Source.size(); ++Index)
		{
			if(Source[Index] == '{')
				++Depth;
			else if(Source[Index] == '}')
			{
				--Depth;
				if(Depth == 0)
					return Index;
			}
		}
		return std::string::npos;
	}

	size_t CountRoundedRectDirectCalls(const std::string &Source)
	{
		const std::regex CallRegex("Graphics\\(\\)->DrawRect(Ext|Ext4|4)?\\([^;]{0,260}IGraphics::CORNER_(ALL|TL|TR|BL|BR|L|R|T|B)");
		size_t Count = 0;
		for(std::sregex_iterator It(Source.begin(), Source.end(), CallRegex), End; It != End; ++It)
			++Count;
		return Count;
	}

	size_t CountOccurrences(const std::string &Text, const char *pNeedle)
	{
		size_t Count = 0;
		for(size_t Position = Text.find(pNeedle); Position != std::string::npos; Position = Text.find(pNeedle, Position + 1))
			++Count;
		return Count;
	}

} // namespace

TEST(QmTooltips, OwnsCallerTextAndBoundsFriendNotes)
{
	char aCallerText[] = "rabbit";
	CTooltip Tooltip{nullptr, CUIRect{}, aCallerText, -1.0f, false};
	aCallerText[0] = 'R';
	EXPECT_EQ(Tooltip.m_Text, "rabbit");

	const std::string BrowserSource = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string FriendsRender = FunctionBody(BrowserSource, "void CMenus::RenderServerbrowserFriends(CUIRect View)");
	EXPECT_NE(FriendsRender.find("DoToolTip(pListItemId, &Rect, TooltipText.c_str(), 320.0f);"), std::string::npos);
	EXPECT_NE(FriendsRender.find("DoToolTip(pSkinTooltipId, &Skin, Friend.Skin());"), std::string::npos);
}

TEST(TClientStatusBarScore, RegistersUniqueScoreSchemeCode)
{
	const std::string Header = ReadTextFile("src/game/client/components/tclient/statusbar.h");
	const std::string Source = ReadTextFile("src/game/client/components/tclient/statusbar.cpp");
	const std::string ApplyScheme = FunctionBody(Source, "void CStatusBar::ApplyStatusBarScheme(const char *pScheme)");
	const std::string UpdateScheme = FunctionBody(Source, "void CStatusBar::UpdateStatusBarScheme(char *pScheme)");
	const std::string ScoreRegistration = "\"s\", \"Points\", \"Points\", \"Displays the DDNet Points of the current player\"";

	const size_t RegistrationPos = Header.find(ScoreRegistration);
	ASSERT_NE(RegistrationPos, std::string::npos);
	EXPECT_EQ(Header.find(ScoreRegistration, RegistrationPos + 1), std::string::npos);
	EXPECT_NE(Header.find("m_Zoom, m_Score, m_Downstream"), std::string::npos);
	EXPECT_NE(ApplyScheme.find("for(char ItemLetter : ItemType.m_aLetters)"), std::string::npos);
	EXPECT_NE(ApplyScheme.find("m_StatusBarItems.push_back(&ItemType);"), std::string::npos);
	EXPECT_NE(UpdateScheme.find("pScheme[Index++] = pItem->m_aLetters[0];"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsShellAndOuterScrollbarUseStableContracts)
{
	const std::string ShellSource = ReadTextFile("src/game/client/QmUi/SettingsPageLayout.h");
	const std::string TokenSource = ReadTextFile("src/game/client/QmUi/UiTokens.h");
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string ScrollSource = ReadTextFile("src/game/client/QmUi/QmScroll.cpp");
	const std::string AssetsSource = ReadTextFile("src/game/client/components/menus_settings_assets.cpp");
	EXPECT_NE(TokenSource.find("MAX_CONTENT_WIDTH = 1000.0f"), std::string::npos);
	EXPECT_NE(ShellSource.find("ResolveSettingsShellLayout"), std::string::npos);
	EXPECT_NE(ShellSource.find("Frame.m_ShellRect.VSplitRight"), std::string::npos);
	EXPECT_NE(ShellSource.find("Frame.m_ContentRect.Margin(PanelMargin"), std::string::npos);
	EXPECT_NE(SettingsSource.find("ResolveSettingsShellLayout(MainView, NeedRestart ? 30.0f : 0.0f)"), std::string::npos);
	EXPECT_NE(ScrollSource.find("SETTINGS_OUTER"), std::string::npos);
	EXPECT_NE(ScrollSource.find("QmScrollContainerStyleForSize(EQmScrollSize::MEDIUM, 1.0f)"), std::string::npos);
	EXPECT_NE(AssetsSource.find("s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_OUTER);"), std::string::npos);
	EXPECT_NE(AssetsSource.find("s_WorkshopAssetsListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_OUTER);"), std::string::npos);
	EXPECT_NE(AssetsSource.find("StableCustomList.w / (Margin + TextureWidth)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, CapsuleTabBarRowRectSpansSlotsAndGaps)
{
	// 意图：胶囊容器覆盖整排 Tab（含 Tab 之间的间隙），而不是只包住第一个槽位。
	const CUIRect aSlots[] = {
		{10.0f, 4.0f, 60.0f, 20.0f},
		{74.0f, 4.0f, 60.0f, 20.0f},
		{138.0f, 4.0f, 100.0f, 20.0f},
	};
	const CUIRect Row = ui_widget::CapsuleTabBarRowRect(aSlots, 3);
	EXPECT_FLOAT_EQ(Row.x, 10.0f);
	EXPECT_FLOAT_EQ(Row.y, 4.0f);
	EXPECT_FLOAT_EQ(Row.w, 228.0f);
	EXPECT_FLOAT_EQ(Row.h, 20.0f);

	EXPECT_FLOAT_EQ(ui_widget::CapsuleTabBarRowRect(aSlots, 1).w, 60.0f);
	EXPECT_FLOAT_EQ(ui_widget::CapsuleTabBarRowRect(nullptr, 3).w, 0.0f);
	EXPECT_FLOAT_EQ(ui_widget::CapsuleTabBarRowRect(aSlots, 0).h, 0.0f);
}

TEST(QmNewUiMenuBranches, NestedRadioRowKeepsLabelAboveFixedHeightContainer)
{
	// 意图：两级分段行固定「标签一行 + 控件一行」，一级选项带不带子级都不改行高，
	// 否则切换「显示昵称」会让卡片高度跳动、预布局与绘制阶段消耗的行高对不上。
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(420.0f);
	const SSettingsNestedRadioRowLayout Row = ResolveSettingsNestedRadioRowLayout({10.0f, 20.0f, 420.0f, Metrics.m_LineHeight}, Metrics);
	EXPECT_FLOAT_EQ(Row.m_LabelRect.x, 10.0f);
	EXPECT_FLOAT_EQ(Row.m_LabelRect.y, 20.0f);
	EXPECT_FLOAT_EQ(Row.m_LabelRect.w, 420.0f);
	EXPECT_FLOAT_EQ(Row.m_LabelRect.h, Metrics.m_LineHeight);
	EXPECT_FLOAT_EQ(Row.m_ContainerRect.x, 10.0f);
	EXPECT_FLOAT_EQ(Row.m_ContainerRect.y, 20.0f + Metrics.m_LineHeight + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(Row.m_ContainerRect.w, 420.0f);
	EXPECT_FLOAT_EQ(Row.m_ContainerRect.h, Metrics.m_ButtonHeight);
	EXPECT_FLOAT_EQ(Row.m_Height, Metrics.m_LineHeight + Metrics.m_LineSpacing + Metrics.m_ButtonHeight);

	EXPECT_FLOAT_EQ(ResolveSettingsNestedRadioRowLayout({0.0f, 0.0f, 0.0f, Metrics.m_LineHeight}, Metrics).m_Height, 0.0f);
}

TEST(QmNewUiMenuBranches, NestedRadioSlotsReplaceActiveMainLabelWithSubMenu)
{
	// 意图：一级项带子级时，它的一级槽位整段让给子级菜单（一级标签被替换掉），
	// 主滑块因此盖住整段子级区域；否则一级标签会和子级并排、滑块只盖住标签。
	const CUIRect Container = {0.0f, 0.0f, 600.0f, 24.0f};
	const SSettingsNestedRadioSlots Plain = ResolveSettingsNestedRadioSlots(Container, 4, 0, 0, 0.0f);
	ASSERT_EQ(Plain.m_MainCount, 4);
	EXPECT_EQ(Plain.m_SubCount, 0);
	for(int i = 0; i < 4; ++i)
	{
		EXPECT_FLOAT_EQ(Plain.m_aMain[i].x, 150.0f * (float)i);
		EXPECT_FLOAT_EQ(Plain.m_aMain[i].w, 150.0f);
	}

	// 5 个单位宽 120：一级 0 / 一级 1（被替换，主滑块占 2 个单位）/ 一级 2 / 一级 3。
	const SSettingsNestedRadioSlots Nested = ResolveSettingsNestedRadioSlots(Container, 4, 1, 2, 0.0f);
	ASSERT_EQ(Nested.m_MainCount, 4);
	ASSERT_EQ(Nested.m_SubCount, 2);
	EXPECT_FLOAT_EQ(Nested.m_aMain[0].x, 0.0f);
	EXPECT_FLOAT_EQ(Nested.m_aMain[0].w, 120.0f);
	EXPECT_FLOAT_EQ(Nested.m_aMain[1].x, 120.0f);
	EXPECT_FLOAT_EQ(Nested.m_aMain[1].w, 240.0f);
	EXPECT_FLOAT_EQ(Nested.m_aSub[0].x, 120.0f);
	EXPECT_FLOAT_EQ(Nested.m_aSub[0].w, 120.0f);
	EXPECT_FLOAT_EQ(Nested.m_aSub[1].x, 240.0f);
	EXPECT_FLOAT_EQ(Nested.m_aSub[1].w, 120.0f);
	EXPECT_FLOAT_EQ(Nested.m_aMain[2].x, 360.0f);
	EXPECT_FLOAT_EQ(Nested.m_aMain[3].x, 480.0f);
	// 槽位内缩由调用方给：容器与槽位之间留出胶囊边距。
	const SSettingsNestedRadioSlots Inset = ResolveSettingsNestedRadioSlots(Container, 4, 1, 2, 2.0f);
	EXPECT_FLOAT_EQ(Inset.m_aMain[0].x, 2.0f);
	EXPECT_FLOAT_EQ(Inset.m_aMain[0].h, 20.0f);
}

TEST(QmNewUiMenuBranches, NestedSegmentChromeDrawsContainerAndTwoSpringTracks)
{
	// 意图：容器与两枚滑块必须先于分段文字绘制（滑块压在文字之下），
	// 主滑块与次级滑块各占一条弹簧轨道，切换时带速度续接地滑过去。
	const std::string Source = ReadTextFile("src/game/client/QmUi/UiNavigation.cpp");
	const std::string Header = ReadTextFile("src/game/client/QmUi/UiNavigation.h");
	ASSERT_NE(Source.find("void NestedSegmentChrome("), std::string::npos);
	ASSERT_NE(Header.find("struct SNestedSegmentStyle"), std::string::npos);
	EXPECT_NE(Header.find("void NestedSegmentChrome(const IUiContext &Ctx, uint64_t GroupId, const CUIRect &ContainerRect, const CUIRect *pMainSlot, const CUIRect *pSubSlot, const SNestedSegmentStyle &Style);"), std::string::npos);

	const std::string Body = FunctionBody(Source, "void NestedSegmentChrome(");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("if(Ctx.m_pUi->RenderOnly())\n\t\t\treturn;"), std::string::npos);
	const size_t ContainerDraw = Body.find("DrawRoundedSurface(Ctx, ContainerRect, Style.m_ContainerColor, ColorRGBA(), ui_token::radius::PILL);");
	const size_t MainDraw = Body.find("DrawIndicator(pMainSlot, Style.m_IndicatorInset, Style.m_MainIndicatorColor, ColorRGBA(), 0);");
	const size_t SubDraw = Body.find("DrawIndicator(pSubSlot, Style.m_SubIndicatorInset, Style.m_SubIndicatorColor, Style.m_SubIndicatorBorderColor, 1);");
	ASSERT_NE(ContainerDraw, std::string::npos);
	ASSERT_NE(MainDraw, std::string::npos);
	ASSERT_NE(SubDraw, std::string::npos);
	EXPECT_LT(ContainerDraw, MainDraw);
	EXPECT_LT(MainDraw, SubDraw);
	// 次级滑块带描边，才和主滑块区分得开。
	EXPECT_NE(Body.find("DrawRoundedSurface(Ctx, Indicator, Fill, Border, ui_token::radius::PILL, HasBorder ? Ctx.m_pUi->PixelSize() : 0.0f);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NamePlateShowRowUsesFlatSixSegmentCapsule)
{
	// 意图：昵称板块的「显示昵称」重整为六档互斥范围（无 / 当前 / 当前+本地 / 他人 / 本地+他人 / 全体），
	// 宽够时是单行六段胶囊（只有主滑块），旧 UI 与窄窗口用同一份选项走可换行分段行，语义一致。
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const size_t RowPos = Source.find("const SSettingsSegmentedRowLayout ShowNameRow = ResolveShowNameRowLayout(LeftView);");
	ASSERT_NE(RowPos, std::string::npos);
	const size_t ChromePos = Source.find("ui_widget::NestedSegmentChrome(TabBarUiContext(), ShowNameGroup, ShowNameRow.m_ContainerRect,", RowPos);
	const size_t MainDrawPos = Source.find("if(DoSettingsButton_CapsuleSegment(SETTINGS_APPEARANCE, APPEARANCE_TAB_NAME_PLATE, APPEARANCE_TAB_NAME_PLATE, &m_vButtonContainersNamePlateShow[i]", RowPos);
	ASSERT_NE(ChromePos, std::string::npos);
	ASSERT_NE(MainDrawPos, std::string::npos);
	EXPECT_LT(RowPos, ChromePos);
	EXPECT_LT(ChromePos, MainDrawPos);
	// 六档选项按顺序绑定档位常量（逐条检查，避免把缩进写进断言）。
	EXPECT_NE(Source.find("const int aShowScopeValues[] = {"), std::string::npos);
	const size_t ValuesPos = Source.find("const int aShowScopeValues[] = {");
	ASSERT_NE(ValuesPos, std::string::npos);
	const size_t ValuesEnd = Source.find("};", ValuesPos);
	ASSERT_NE(ValuesEnd, std::string::npos);
	const std::string ValuesBlock = Source.substr(ValuesPos, ValuesEnd - ValuesPos);
	const char *apExpectedScopeOrder[] = {"QM_NAMEPLATE_SHOW_SCOPE_OFF", "QM_NAMEPLATE_SHOW_SCOPE_CURRENT", "QM_NAMEPLATE_SHOW_SCOPE_LOCAL", "QM_NAMEPLATE_SHOW_SCOPE_OTHERS", "QM_NAMEPLATE_SHOW_SCOPE_OTHERS_LOCAL", "QM_NAMEPLATE_SHOW_SCOPE_ALL"};
	size_t PreviousEntry = 0;
	for(const char *pEntry : apExpectedScopeOrder)
	{
		const size_t EntryPos = ValuesBlock.find(pEntry);
		ASSERT_NE(EntryPos, std::string::npos) << pEntry;
		EXPECT_LT(PreviousEntry, EntryPos) << pEntry;
		PreviousEntry = EntryPos;
	}
	// 档位个数常量与档位数组同源，不允许再多写魔法数字 6。
	EXPECT_NE(Source.find("const int ShowScopeCount = (int)std::size(aShowScopeValues);"), std::string::npos);
	EXPECT_NE(Source.find("const int Pressed = std::clamp(g_Config.m_QmNameplateShowScope, 0, ShowScopeCount - 1);"), std::string::npos);
	EXPECT_NE(Source.find("if(DoSettingsButton_CapsuleSegment(SETTINGS_APPEARANCE, APPEARANCE_TAB_NAME_PLATE, APPEARANCE_TAB_NAME_PLATE, &m_vButtonContainersNamePlateShow[i], apShowScopeTextIds[i], apShowScopeLabels[i], Pressed == aShowScopeValues[i], &ShowNameSlots.m_aMain[i], AppearanceMetrics.m_SmallSize))"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmNameplateShowScope = aShowScopeValues[i];"), std::string::npos);
	// 单行六段：没有子级菜单，滑块只标主槽位。
	EXPECT_NE(Source.find("const SSettingsNestedRadioSlots ShowNameSlots = ResolveSettingsNestedRadioSlots(ShowNameRow.m_ContainerRect, ShowScopeCount, Pressed, 0, CapsuleSegmentInset);"), std::string::npos);
	EXPECT_NE(Source.find("ui_widget::NestedSegmentChrome(TabBarUiContext(), ShowNameGroup, ShowNameRow.m_ContainerRect, &ShowNameSlots.m_aMain[Pressed], nullptr, SettingsNestedSegmentStyle());"), std::string::npos);
	EXPECT_EQ(Source.find("if(ShowSubMenu && i == Pressed)"), std::string::npos);
	EXPECT_EQ(Source.find("const int SubCount = (OwnRow || OthersRow) ? 2 : 0;"), std::string::npos);
	EXPECT_EQ(Source.find("m_vButtonContainersNamePlateOwnScope"), std::string::npos);
	EXPECT_EQ(Source.find("m_vButtonContainersNamePlateOthersScope"), std::string::npos);
	// 窄窗口兜底：放不下六段胶囊时退回可换行分段行，两条路径都写同一个配置项。
	EXPECT_NE(Source.find("if(g_Config.m_QmNewUi != 0 && ShowNameRow.m_Capsule)"), std::string::npos);
	EXPECT_NE(Source.find("// 旧 UI，以及新 UI 下主内容区太窄放不下六段胶囊时：走可换行分段行。"), std::string::npos);
	EXPECT_NE(Source.find("DoShowScopeRadioMenu();"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmNameplateShowScope = Selected;"), std::string::npos);
	// 测量、预布局与绘制三个阶段必须共用 ResolveShowNameRowLayout 这一个行高解析结果。
	// 该 lambda 会被预布局回调按值拷走，所以标签表必须是静态的，不能指向栈上数组。
	EXPECT_NE(Source.find("const auto ResolveShowNameRowLayout = [=](const CUIRect &View) {"), std::string::npos);
	EXPECT_NE(Source.find("static const char *const apShowScopeLabels[] = {"), std::string::npos);
	EXPECT_NE(Source.find("return ResolveSettingsSegmentedRowLayout(View, ShowScopeCount, AppearanceMetrics, ShowNameOptionMinWidth());"), std::string::npos);
	EXPECT_NE(Source.find("const float GeneralContentHeight = ResolveShowNameRowLayout({0.0f, 0.0f, ContentWidth, LineSize * 2.0f + MarginSmall}).m_Height"), std::string::npos);
	EXPECT_NE(Source.find("LeftView.HSplitTop(ResolveShowNameRowLayout(LeftView).m_Height, nullptr, &LeftView);"), std::string::npos);
	EXPECT_NE(Source.find("const SSettingsSegmentedRowLayout ShowNameRow = ResolveShowNameRowLayout(LeftView);"), std::string::npos);
	// 分段宽度阈值按最长档位标签的真实宽度算，否则正常窗口宽度下胶囊分段永远不生效。
	EXPECT_NE(Source.find("const auto ShowNameOptionMinWidth = [this, AppearanceMetrics]() {"), std::string::npos);
	EXPECT_NE(Source.find("Widest = maximum(Widest, pTextRender->TextWidth(AppearanceMetrics.m_SmallSize, pLabel, -1));"), std::string::npos);
	EXPECT_EQ(Source.find("return RadioHeight(6);"), std::string::npos);
	EXPECT_EQ(Source.find("ConsumeRadio(6);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsSegmentedRowFallsBackToWrappingRadioWhenNarrow)
{
	// 意图：多档分段行的共用落位解析。够宽给胶囊分段（标签一行 + 控件一行），
	// 窄到分段放不下时给可换行分段行的标签/控件矩形，两者都由同一函数给出。
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	const CUIRect Wide{0.0f, 0.0f, 600.0f, Metrics.m_LineHeight};
	const CUIRect Narrow{0.0f, 0.0f, 300.0f, Metrics.m_LineHeight};
	const float Measured = 52.0f; // 小字号下「本地+他人」的量级

	const SSettingsSegmentedRowLayout WideLayout = ResolveSettingsSegmentedRowLayout(Wide, 6, Metrics, Measured);
	EXPECT_TRUE(WideLayout.m_Capsule);
	EXPECT_FLOAT_EQ(WideLayout.m_Height, Metrics.m_LineHeight + Metrics.m_LineSpacing + Metrics.m_ButtonHeight);
	EXPECT_FLOAT_EQ(WideLayout.m_ContainerRect.y, Metrics.m_LineHeight + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(WideLayout.m_ContainerRect.w, Wide.w);

	// 窄窗口必须退回可换行分段行，且行高/控件矩形与普通分段行一致。
	const SSettingsSegmentedRowLayout NarrowLayout = ResolveSettingsSegmentedRowLayout(Narrow, 6, Metrics, Measured);
	EXPECT_FALSE(NarrowLayout.m_Capsule);
	const SSettingsRadioRowLayout NarrowRadio = ResolveSettingsRadioRowLayout(Narrow, 6, Metrics);
	EXPECT_FLOAT_EQ(NarrowLayout.m_Height, NarrowRadio.m_Height);
	EXPECT_FLOAT_EQ(NarrowLayout.m_ContainerRect.w, NarrowRadio.m_ButtonsRect.w);

	// 不给量测宽度时退回保守估算（72px 下限），不能因此永远选胶囊：
	// 400 放不下六档，600 按胶囊真实几何够（旧阈值多扣一列标签宽时会误判）。
	EXPECT_FALSE(ResolveSettingsSegmentedRowLayout({0.0f, 0.0f, 400.0f, Metrics.m_LineHeight}, 6, Metrics, 0.0f).m_Capsule);
	EXPECT_TRUE(ResolveSettingsSegmentedRowLayout(Wide, 6, Metrics, 0.0f).m_Capsule);
	EXPECT_TRUE(ResolveSettingsSegmentedRowLayout({0.0f, 0.0f, 1000.0f, Metrics.m_LineHeight}, 6, Metrics, 0.0f).m_Capsule);

	// 宽度为 0 不能出现半成品布局；档位少时宽窗口仍走胶囊。
	const SSettingsSegmentedRowLayout Zero = ResolveSettingsSegmentedRowLayout({0.0f, 0.0f, 0.0f, 0.0f}, 6, Metrics, Measured);
	EXPECT_FLOAT_EQ(Zero.m_Height, 0.0f);
	EXPECT_FALSE(Zero.m_Capsule);
	EXPECT_FLOAT_EQ(ResolveSettingsSegmentedRowLayout(Wide, 2, Metrics, Measured).m_ContainerRect.w, Wide.w);

	// 阈值单调：宽度递增时只从「可换行」切到「胶囊」一次。
	bool SeenCapsule = false;
	for(float Width = 100.0f; Width <= 900.0f; Width += 4.0f)
	{
		const SSettingsSegmentedRowLayout Layout = ResolveSettingsSegmentedRowLayout({0.0f, 0.0f, Width, Metrics.m_LineHeight}, 6, Metrics, Measured);
		if(Layout.m_Capsule)
			SeenCapsule = true;
		else
			EXPECT_FALSE(SeenCapsule) << "宽度 " << Width << " 又退回可换行分段行";
	}
}

TEST(QmNewUiMenuBranches, SegmentedRowCapsuleFitsTwoColumnCardWidth)
{
	// 意图：胶囊分段的标签独占上一行、控件行用满整行宽度，阈值里不能再预扣一列标签宽。
	// 1920 宽窗口的新 UI 用两列卡片，卡片内容宽约 464：旧阈值（约 148 + 5 + 6×60 ≈ 513）
	// 会让「显示昵称」白白退回旧分段行，与同页「钩索强度范围」的胶囊外观对不上。
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	const CUIRect CardContent{0.0f, 0.0f, 464.0f, Metrics.m_LineHeight};
	// 与调用方一致：MinOptionWidth 含每档 8px 间隙（ShowNameOptionMinWidth / Widest + 8.0f）。
	const float ShowScopeOptionWidth = 52.0f + 8.0f; // 小字号下「本地+他人」+ 间隙
	const float HookScopeOptionWidth = 28.0f + 8.0f; // 小字号下「强钩」+ 间隙

	const SSettingsSegmentedRowLayout ShowScope = ResolveSettingsSegmentedRowLayout(CardContent, 6, Metrics, ShowScopeOptionWidth);
	EXPECT_TRUE(ShowScope.m_Capsule);
	EXPECT_FLOAT_EQ(ShowScope.m_ContainerRect.w, CardContent.w);
	EXPECT_TRUE(ResolveSettingsSegmentedRowLayout(CardContent, 5, Metrics, HookScopeOptionWidth).m_Capsule);

	// 真放不下的判定仍然保留：六档各要 52+8 时，356 退回可换行分段行，360 才够。
	EXPECT_FALSE(ResolveSettingsSegmentedRowLayout({0.0f, 0.0f, 356.0f, Metrics.m_LineHeight}, 6, Metrics, ShowScopeOptionWidth).m_Capsule);
	EXPECT_TRUE(ResolveSettingsSegmentedRowLayout({0.0f, 0.0f, 360.0f, Metrics.m_LineHeight}, 6, Metrics, ShowScopeOptionWidth).m_Capsule);
}

TEST(QmNewUiMenuBranches, CapsuleSegmentKeepsBudgetedTextPipeline)
{
	// 意图：胶囊分段只有外观换了，文字仍必须走设置页文本缓存，不能退化成直绘标签。
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Body = FunctionBody(Source, "int CMenus::DoSettingsButton_CapsuleSegment(");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("CollectMenuTextPlanItem(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, Subtab, pResolvedTextId, pText, &Text, ResolvedBodySize, TEXTALIGN_MC, Props, StyleKey);"), std::string::npos);
	EXPECT_NE(Body.find("CUIElement *pTextElement = pTextId != nullptr ? &MenuTextElement(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, Subtab, pResolvedTextId, StyleKey) : nullptr;"), std::string::npos);
	EXPECT_NE(Body.find("DoButton_MenuTab(pBC, pText, Checked, pRect, IGraphics::CORNER_ALL, nullptr, &Transparent, &Transparent, pHoverColor, Rounding, nullptr, pTextElement, ResolvedBodySize, CapsuleTab);"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoLabel("), std::string::npos);
	// 压在主滑块上的次级分段用调用方给的实色字，画完必须还原本帧字色。
	const size_t SetColorPos = Body.find("TextRender()->TextColor(*pLabelColor);");
	const size_t RestorePos = Body.find("TextRender()->TextColor(PreviousLabelColor);");
	ASSERT_NE(SetColorPos, std::string::npos);
	ASSERT_NE(RestorePos, std::string::npos);
	EXPECT_LT(SetColorPos, RestorePos);
	EXPECT_NE(Body.find("const bool CapsuleTab = !ExplicitLabelColor;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NestedSegmentStyleColorsSubMenuAgainstMainIndicator)
{
	// 意图：子级菜单压在主滑块上，字色/次级滑块都必须按「主滑块明暗」推导 ——
	// 按容器明暗推导会让子级文字在主滑块上糊成一片。
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Body = FunctionBody(Source, "ui_widget::SNestedSegmentStyle CMenus::SettingsNestedSegmentStyle() const");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("const bool MainIndicatorIsDark = ui_widget::CapsuleTabBarSurfaceIsLight(SurfaceColor);"), std::string::npos);
	EXPECT_NE(Body.find("Style.m_SubActiveLabelColor = ui_widget::CapsuleTabBarActiveLabelColor(SurfaceColor);"), std::string::npos);
	EXPECT_NE(Body.find("Style.m_SubInactiveLabelColor = MainIndicatorIsDark ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.55f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.50f);"), std::string::npos);
	EXPECT_NE(Body.find("Style.m_SubIndicatorColor = MainIndicatorIsDark ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.18f);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, CapsuleTabBarChromeDrawsContainerThenSpringIndicatorUnderLabels)
{
	// 意图：滑块胶囊必须由容器的同一入口画在文字之前，并且由弹簧驱动、可被打断续接，
	// 否则切换 Tab 时会瞬移或盖住经过的页签文字。
	const std::string Source = ReadTextFile("src/game/client/QmUi/UiNavigation.cpp");
	const std::string Header = ReadTextFile("src/game/client/QmUi/UiNavigation.h");
	ASSERT_NE(Source.find("void CapsuleTabBarChrome("), std::string::npos);

	// ui_widget::TabBar（UiDogfood 调试页）同样走胶囊，不再保留下划线小块。
	const std::string DogfoodTabBar = FunctionBody(Source, "int TabBar(");
	ASSERT_FALSE(DogfoodTabBar.empty());
	EXPECT_EQ(DogfoodTabBar.find("Underline"), std::string::npos);
	EXPECT_EQ(DogfoodTabBar.find("Indicator.Draw("), std::string::npos);
	EXPECT_NE(DogfoodTabBar.find("std::vector<CUIRect> vTabSlots(static_cast<std::size_t>(Count));"), std::string::npos);
	EXPECT_LT(DogfoodTabBar.find("CapsuleTabBarChrome(Ctx, BuildUiAnimNodeKey(MakeUiScopeHash(\"ui_widget_tabbar_capsule\")"), DogfoodTabBar.find("DoButton_MenuTab(&ButtonPool[i], ppLabels[i], Checked, &vTabSlots[static_cast<std::size_t>(i)]"));

	EXPECT_NE(Source.find("if(Ctx.m_pUi->RenderOnly())\n\t\t\treturn;"), std::string::npos);
	const size_t CapsuleDraw = Source.find("DrawRoundedSurface(Ctx, Capsule, Style.m_CapsuleColor, ColorRGBA(), ui_token::radius::PILL);");
	const size_t IndicatorDraw = Source.find("DrawRoundedSurface(Ctx, Indicator, Style.m_IndicatorColor, ColorRGBA(), ui_token::radius::PILL);");
	ASSERT_NE(CapsuleDraw, std::string::npos);
	ASSERT_NE(IndicatorDraw, std::string::npos);
	EXPECT_LT(CapsuleDraw, IndicatorDraw);
	EXPECT_NE(Header.find("inline CUIRect CapsuleTabBarRowRect(const CUIRect *pSlots, int Count)"), std::string::npos);
	// 配色自适应由 QmUi 统一提供，各 Tabbar 只传自己的容器表面色。
	EXPECT_NE(Header.find("inline bool CapsuleTabBarSurfaceIsLight(const ColorRGBA &SurfaceColor)"), std::string::npos);
	EXPECT_NE(Header.find("inline ColorRGBA CapsuleTabBarIndicatorColor(const ColorRGBA &SurfaceColor)"), std::string::npos);
	EXPECT_NE(Header.find("inline ColorRGBA CapsuleTabBarActiveLabelColor(const ColorRGBA &SurfaceColor)"), std::string::npos);
	EXPECT_NE(Header.find("inline ColorRGBA CapsuleTabBarInactiveLabelColor(const ColorRGBA &SurfaceColor)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsSubTabRowsUseCapsuleTabBar)
{
	// 意图：设置页各子 Tab 行（外观 / Assets / TClient / QmClient）统一走
	// 「槽位预布局 → 容器与滑块 → 页签文字」，旧 UI 分支保留原来的分段外观。
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string MenusHeader = ReadTextFile("src/game/client/components/menus.h");
	const std::string Settings = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Assets = ReadTextFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string TClient = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string QmClient = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	// 公共零件：绘制上下文与设置页胶囊配色。
	EXPECT_NE(MenusHeader.find("IUiContext TabBarUiContext() const;"), std::string::npos);
	EXPECT_NE(MenusHeader.find("ui_widget::SCapsuleTabBarStyle SettingsCapsuleTabBarStyle() const;"), std::string::npos);
	EXPECT_NE(MenusSource.find("IUiContext CMenus::TabBarUiContext() const"), std::string::npos);
	EXPECT_NE(MenusSource.find("ui_widget::SCapsuleTabBarStyle CMenus::SettingsCapsuleTabBarStyle() const"), std::string::npos);

	const std::string RenderAppearance = FunctionBody(Settings, "void CMenus::RenderSettingsAppearance(CUIRect MainView)");
	ASSERT_FALSE(RenderAppearance.empty());
	const size_t AppearanceGrid = RenderAppearance.find("AppearanceTabsRemainder.VSplitLeft(TabWidth, &aAppearanceTabSlots[Tab], &AppearanceTabsRemainder);");
	const size_t AppearanceChrome = RenderAppearance.find("ui_widget::CapsuleTabBarChrome(AppearanceTabBarCtx, MakeUiScopeHash(\"settings_appearance_tabs_capsule\")");
	const size_t AppearanceDraw = RenderAppearance.find("DoButton_MenuTab(&s_aPageTabs[Tab], s_apAppearanceTabNames[Tab], m_AppearanceSettingsTab == Tab, &aAppearanceTabSlots[Tab]");
	ASSERT_NE(AppearanceGrid, std::string::npos);
	ASSERT_NE(AppearanceChrome, std::string::npos);
	ASSERT_NE(AppearanceDraw, std::string::npos);
	EXPECT_LT(AppearanceGrid, AppearanceChrome);
	EXPECT_LT(AppearanceChrome, AppearanceDraw);
	EXPECT_NE(RenderAppearance.find("nullptr, nullptr, -1.0f, true))"), std::string::npos);
	EXPECT_NE(RenderAppearance.find("IGraphics::CORNER_L"), std::string::npos);

	const std::string RenderAssets = FunctionBody(Assets, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(RenderAssets.empty());
	const size_t AssetsGrid = RenderAssets.find("AssetsTabsRemainder.VSplitLeft(TabWidth, &aAssetsTabSlots[Tab], &AssetsTabsRemainder);");
	const size_t AssetsChrome = RenderAssets.find("ui_widget::CapsuleTabBarChrome(AssetsTabBarCtx, MakeUiScopeHash(\"settings_assets_tabs_capsule\")");
	const size_t AssetsDraw = RenderAssets.find("DoButton_MenuTab(&s_aPageTabs[Tab], s_apAssetsTabNames[Tab], s_CurCustomTab == Tab, &aAssetsTabSlots[Tab]");
	ASSERT_NE(AssetsGrid, std::string::npos);
	ASSERT_NE(AssetsChrome, std::string::npos);
	ASSERT_NE(AssetsDraw, std::string::npos);
	EXPECT_LT(AssetsGrid, AssetsChrome);
	EXPECT_LT(AssetsChrome, AssetsDraw);
	EXPECT_NE(RenderAssets.find("IGraphics::CORNER_L"), std::string::npos);

	const std::string RenderTClient = FunctionBody(TClient, "void CMenus::RenderSettingsTClient(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(RenderTClient.empty());
	const size_t TClientGrid = RenderTClient.find("TabsRemainder.VSplitLeft(TabWidth, &aTClientTabSlots[NumTClientTabs], &TabsRemainder);");
	const size_t TClientChrome = RenderTClient.find("ui_widget::CapsuleTabBarChrome(TClientTabBarCtx, MakeUiScopeHash(\"settings_tclient_tabs_capsule\")");
	const size_t TClientDraw = RenderTClient.find("DoButton_MenuTab(&s_aPageTabs[Tab], s_apTClientTabNames[Tab], ActiveTab == Tab, &aTClientTabSlots[TabIndex]");
	ASSERT_NE(TClientGrid, std::string::npos);
	ASSERT_NE(TClientChrome, std::string::npos);
	ASSERT_NE(TClientDraw, std::string::npos);
	EXPECT_LT(TClientGrid, TClientChrome);
	EXPECT_LT(TClientChrome, TClientDraw);
	// 旧 UI 仍按 CORNER_L/R/NONE 的分段外观逐段切分。
	EXPECT_NE(RenderTClient.find("ActiveTab == Tab, &Button, Corners"), std::string::npos);

	const std::string RenderQmClient = FunctionBody(QmClient, "void CMenus::RenderSettingsQmClientContent(CUIRect MainView, bool ContributorsPage, bool PrewarmOnly)");
	ASSERT_FALSE(RenderQmClient.empty());
	const size_t QmGrid = RenderQmClient.find("QmTabsRemainder.VSplitLeft(TabWidth, &aQmTabSlots[Tab], &QmTabsRemainder);");
	const size_t QmChrome = RenderQmClient.find("ui_widget::CapsuleTabBarChrome(QmTabBarCtx, MakeUiScopeHash(\"settings_qmclient_tabs_capsule\")");
	const size_t QmDraw = RenderQmClient.find("DoButton_MenuTab(&s_aPageTabs[Tab], apQmTabNames[Tab], m_QmClientSettingsTab == Tab, &aQmTabSlots[Tab]");
	ASSERT_NE(QmGrid, std::string::npos);
	ASSERT_NE(QmChrome, std::string::npos);
	ASSERT_NE(QmDraw, std::string::npos);
	EXPECT_LT(QmGrid, QmChrome);
	EXPECT_LT(QmChrome, QmDraw);
	// 页签计时段仍然覆盖两条分支。
	EXPECT_LT(QmDraw, RenderQmClient.find("LogQmPerfStage(Client(), \"tabbar\", StageTimer.ElapsedMs(), false, aTabExtra);"));

	// 玩家/Dummy 行与皮肤（Player/Dummy/Profiles）行同样先画胶囊再画文字。
	const std::string RenderPlayer = FunctionBody(Settings, "void CMenus::RenderSettingsPlayer(CUIRect MainView)");
	const std::string RenderTee = FunctionBody(Settings, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_FALSE(RenderPlayer.empty());
	ASSERT_FALSE(RenderTee.empty());
	const size_t PlayerChrome = RenderPlayer.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash(\"settings_player_dummy_tabs_capsule\"), aPlayerTabSlots, std::size(aPlayerTabSlots), m_Dummy ? 1 : 0, SettingsCapsuleTabBarStyle());");
	const size_t PlayerDraw = RenderPlayer.find("if(DoButton_MenuTab(&s_PlayerTabButton, Localize(\"Player\"), !m_Dummy, &PlayerTab, IGraphics::CORNER_ALL");
	ASSERT_NE(PlayerChrome, std::string::npos);
	ASSERT_NE(PlayerDraw, std::string::npos);
	EXPECT_LT(PlayerChrome, PlayerDraw);
	EXPECT_NE(RenderPlayer.find("&PlayerTab, IGraphics::CORNER_L"), std::string::npos);
	const size_t TeeChrome = RenderTee.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash(\"settings_tee_sub_tabs_capsule\"), aTeeTabSlots, std::size(aTeeTabSlots), ActiveTeeTab, SettingsCapsuleTabBarStyle());");
	const size_t TeeDraw = RenderTee.find("if(DoButton_MenuTab(&s_PlayerTabButton, pPlayerTabLabel, s_TeeSubTab == 0, &PlayerTab, IGraphics::CORNER_ALL");
	ASSERT_NE(TeeChrome, std::string::npos);
	ASSERT_NE(TeeDraw, std::string::npos);
	EXPECT_LT(TeeChrome, TeeDraw);
	EXPECT_NE(RenderTee.find("SeparateProfilesTab ? IGraphics::CORNER_R : IGraphics::CORNER_NONE"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ServerBrowserToolboxUsesCapsuleTabBar)
{
	// 意图：服务器浏览器工具箱页签（过滤器 / 信息 / 好友）在新 UI 下同样是胶囊，
	// 旧 UI 保留原来的分段底色。
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderServerbrowserTabBar(CUIRect TabBar)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiNavigation.h>"), std::string::npos);
	const size_t Chrome = Body.find("ui_widget::CapsuleTabBarChrome(ToolboxTabBarCtx, MakeUiScopeHash(\"browser_toolbox_tabs_capsule\")");
	const size_t Draw = Body.find("if(DoButton_MenuTab(&s_FilterTabButton, FONT_ICON_LIST_UL, g_Config.m_UiToolboxPage == UI_TOOLBOX_PAGE_FILTERS, &FilterTabButton, IGraphics::CORNER_ALL, &m_aAnimatorsSmallPage[SMALL_TAB_BROWSER_FILTER], nullptr, nullptr, nullptr, 10.0f, nullptr, nullptr, -1.0f, true))");
	ASSERT_NE(Chrome, std::string::npos);
	ASSERT_NE(Draw, std::string::npos);
	EXPECT_LT(Chrome, Draw);
	EXPECT_NE(Body.find("CapsuleTabBarStyleFor(BrowserPanelColor(1.0f))"), std::string::npos);
	EXPECT_NE(Body.find("const ColorRGBA ColorActive = UseNewUi ? BrowserPanelElevatedColor(0.92f) : ms_ColorTabbarActive;"), std::string::npos);
	EXPECT_EQ(Source.find("UI_TOOLBOX_PAGE_QM"), std::string::npos);
	EXPECT_EQ(Source.find("RenderServerbrowserQm"), std::string::npos);

	// 通用配色零件：轨道压暗 + 滑块/文字自适应。
	EXPECT_NE(ReadTextFile("src/game/client/components/menus.h").find("ui_widget::SCapsuleTabBarStyle CapsuleTabBarStyleFor(const ColorRGBA &SurfaceColor) const;"), std::string::npos);
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	EXPECT_NE(MenusSource.find("ui_widget::SCapsuleTabBarStyle CMenus::CapsuleTabBarStyleFor(const ColorRGBA &SurfaceColor) const"), std::string::npos);
	EXPECT_NE(MenusSource.find("return CapsuleTabBarStyleFor(SettingsTabbarColor());"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ServerControlTabsUseCapsuleTabBarInNewUi)
{
	// 意图：游戏中"服务器控制"页的三个页签（改设置 / 踢人 / 移到观察者）在新 UI 下
	// 同样先画胶囊容器与滑块，再画页签文字；旧 UI 保留贴边的分段外观。
	const std::string Source = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderServerControl(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiNavigation.h>"), std::string::npos);
	const size_t Chrome = Body.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash(\"ingame_server_control_tabs_capsule\"), aControlTabSlots, 3, ActiveControlTab, CapsuleTabBarStyleFor(ms_ColorTabbarActive));");
	const size_t Draw = Body.find("if(DoButton_MenuTab(&s_Button0, Localize(\"Change settings\"), s_ControlPage == EServerControlTab::SETTINGS, &aControlTabSlots[0], IGraphics::CORNER_ALL");
	ASSERT_NE(Chrome, std::string::npos);
	ASSERT_NE(Draw, std::string::npos);
	EXPECT_LT(Chrome, Draw);
	EXPECT_NE(Body.find("ControlTabsRemainder.VSplitLeft(ControlTabsRemainder.w / 3.0f, &aControlTabSlots[0], &ControlTabsRemainder);"), std::string::npos);
	EXPECT_NE(Body.find("ControlTabsRemainder.VSplitMid(&aControlTabSlots[1], &aControlTabSlots[2]);"), std::string::npos);
	EXPECT_NE(Body.find("&Button, IGraphics::CORNER_NONE"), std::string::npos);
}

TEST(QmNewUiMenuBranches, Tee7SubTabsUseCapsuleTabBar)
{
	// 意图：Tee7 皮肤的「玩家/Dummy」「Basic/Custom」「皮肤部位」三行子 Tab 同样
	// 先画胶囊容器与滑块、再画文字；旧 UI 保留贴边分段外观。
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings7.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsTee7Content(CUIRect MainView, const SSettingsContentMetrics &Metrics)");
	ASSERT_FALSE(Body.empty());

	// 三个锚点在整份源码里各只出现一次，直接按文件位置比较先后。
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiNavigation.h>"), std::string::npos);
	const size_t PlayerDummyChrome = Source.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash(\"settings_tee7_player_dummy_tabs_capsule\"), aPlayerDummySlots, std::size(aPlayerDummySlots), m_Dummy ? 1 : 0, SettingsCapsuleTabBarStyle());");
	const size_t PlayerDummyDraw = Source.find("if(DoButton_MenuTab(&s_PlayerTabButton, Localize(\"Player\"), !m_Dummy, &LeftTab, IGraphics::CORNER_ALL");
	const size_t ModeChrome = Source.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash(\"settings_tee7_mode_tabs_capsule\"), aModeTabSlots, std::size(aModeTabSlots), m_CustomSkinMenu ? 1 : 0, SettingsCapsuleTabBarStyle());");
	const size_t ModeDraw = Source.find("ClickedBasicTab = DoButton_MenuTab(&s_BasicTabButton");
	const size_t SkinPartChrome = Source.find("ui_widget::CapsuleTabBarChrome(TabBarUiContext(), MakeUiScopeHash(\"settings_tee7_skin_part_tabs_capsule\"), aSkinPartSlots, protocol7::NUM_SKINPARTS, ActiveSkinPart, SettingsCapsuleTabBarStyle());");
	const size_t SkinPartDraw = Source.find("if(DoButton_MenuTab(&s_aSkinPartButtons[i], Localize(CSkins7::ms_apSkinPartNamesLocalized[i], \"skins\"), m_TeePartSelected == i, &aSkinPartSlots[i], IGraphics::CORNER_ALL");
	ASSERT_NE(PlayerDummyChrome, std::string::npos);
	ASSERT_NE(PlayerDummyDraw, std::string::npos);
	ASSERT_NE(ModeChrome, std::string::npos);
	ASSERT_NE(ModeDraw, std::string::npos);
	ASSERT_NE(SkinPartChrome, std::string::npos);
	ASSERT_NE(SkinPartDraw, std::string::npos);
	EXPECT_LT(PlayerDummyChrome, PlayerDummyDraw);
	EXPECT_LT(ModeChrome, ModeDraw);
	EXPECT_LT(SkinPartChrome, SkinPartDraw);
	// 旧 UI 的贴边分段外观与圆角分支仍在。
	EXPECT_NE(Source.find("!m_Dummy, &LeftTab, IGraphics::CORNER_L"), std::string::npos);
	EXPECT_NE(Source.find("!m_CustomSkinMenu, &LeftTab, IGraphics::CORNER_L"), std::string::npos);
	EXPECT_NE(Source.find("Button, Corners, nullptr, nullptr, nullptr, nullptr, ui_token::radius::BASE"), std::string::npos);
}

TEST(QmNewUiMenuBranches, IngameGameButtonBarRoundsAllCornersOnlyInNewUi)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const std::string RenderGame = FunctionBody(Source, "void CMenus::RenderGame(CUIRect MainView)");

	EXPECT_NE(RenderGame.find("const int ButtonBarsCorners = g_Config.m_QmNewUi != 0 ? IGraphics::CORNER_ALL : IGraphics::CORNER_B;"), std::string::npos);
	EXPECT_NE(RenderGame.find("ButtonBars.Draw(ms_ColorTabbarActive, ButtonBarsCorners, 10.0f);"), std::string::npos);
	EXPECT_EQ(RenderGame.find("ButtonBars.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);"), std::string::npos);
}

TEST(QmCameraEffects, DynamicFovRemovalKeepsBaseZoomStable)
{
	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomWithoutDynamicFov(2.0f, 1.25f), 1.6f);
	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomWithoutDynamicFov(1.6f, 1.0f), 1.6f);
	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomWithoutDynamicFov(1.6f, 0.0f), 1.6f);
}

TEST(QmCameraEffects, CinematicFreeviewSmoothingIsFrameRateIndependent)
{
	const vec2 Start(10.0f, 20.0f);
	const vec2 Target(30.0f, 60.0f);
	vec2 At30Fps = Start;
	vec2 At60Fps = Start;
	for(int Frame = 0; Frame < 30; ++Frame)
		At30Fps = QmCameraEffects::SmoothCinematicPosition(At30Fps, Target, 1.0f / 30.0f);
	for(int Frame = 0; Frame < 60; ++Frame)
		At60Fps = QmCameraEffects::SmoothCinematicPosition(At60Fps, Target, 1.0f / 60.0f);

	EXPECT_FLOAT_EQ(QmCameraEffects::SmoothCinematicPosition(Start, Target, 0.0f).x, Start.x);
	EXPECT_NEAR(At30Fps.x, At60Fps.x, 0.0001f);
	EXPECT_NEAR(At30Fps.y, At60Fps.y, 0.0001f);
	EXPECT_GT(At30Fps.x, Start.x);
	EXPECT_LT(At30Fps.x, Target.x);
}

TEST(QmCameraEffectsSource, CinematicCameraAndDynamicFovKeepScopedState)
{
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Header = ReadTextFile("src/game/client/components/camera.h");
	const std::string Source = ReadTextFile("src/game/client/components/camera.cpp");
	const std::string OnRender = FunctionBody(Source, "void CCamera::OnRender()");
	const std::string ScaleZoom = FunctionBody(Source, "void CCamera::ScaleZoom(");
	const std::string ChangeZoom = FunctionBody(Source, "void CCamera::ChangeZoom(");
	const std::string UpdateCamera = FunctionBody(Source, "void CCamera::UpdateCamera()");
	const std::string OnReset = FunctionBody(Source, "void CCamera::OnReset()");
	const std::string GameClient = ReadTextFile("src/game/client/gameclient.cpp");

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmCinematicCamera, qm_cinematic_camera"), std::string::npos);
	EXPECT_NE(Header.find("m_CinematicCameraSmoothing"), std::string::npos);
	EXPECT_NE(OnRender.find("GameClient()->m_Snap.m_SpecInfo.m_Active && !GameClient()->m_Snap.m_SpecInfo.m_UsePosition"), std::string::npos);
	EXPECT_NE(OnRender.find("if(g_Config.m_QmCinematicCamera)"), std::string::npos);
	EXPECT_NE(OnRender.find("m_CinematicCameraSmoothing = false;"), std::string::npos);
	EXPECT_NE(ScaleZoom.find("RemoveDynamicFovZoom();"), std::string::npos);
	EXPECT_NE(ChangeZoom.find("RemoveDynamicFovZoom();"), std::string::npos);
	// 反向按键立即生效已移除：步进基准回到上游「缩放中沿用旧目标」的行为
	EXPECT_NE(ScaleZoom.find("float CurrentTarget = m_Zooming ? m_ZoomSmoothingTarget : m_Zoom;"), std::string::npos);
	EXPECT_NE(UpdateCamera.find("RemoveDynamicFovZoom();"), std::string::npos);
	EXPECT_NE(OnReset.find("m_DynamicFovAppliedFactor = 1.0f;"), std::string::npos);
	EXPECT_EQ(UpdateCamera.find("m_aDyncamCurrentCameraOffset[g_Config.m_ClDummy] += m_DriftCurrentOffset;"), std::string::npos);
	EXPECT_NE(OnRender.find("m_aDyncamCurrentCameraOffset[g_Config.m_ClDummy] + m_DriftCurrentOffset"), std::string::npos);
	EXPECT_NE(Header.find("float BaseZoom() const"), std::string::npos);
	EXPECT_NE(GameClient.find("m_Camera.BaseZoom()"), std::string::npos);
	EXPECT_EQ(GameClient.find("float ShowDistanceZoom = m_Camera.m_Zoom;"), std::string::npos);
}

TEST(QmCameraEffectsSource, CameraViewCardHeightTracksRowCount)
{
	const std::string QmMenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string CameraView = FunctionBody(QmMenusSource, "void CMenus::RenderQmVisualCameraViewContent(");
	// 卡片高度测量已搬到视觉卡片目录（MeasureVisualCardHeight）。
	const std::string VisualCatalog = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogVisual.cpp");
	const std::string VisualMeasure = FunctionBody(VisualCatalog, "float MeasureVisualCardHeight(");
	ASSERT_FALSE(VisualMeasure.empty());

	// 「反向缩放立即生效」已连同 qm_zoom_instant_reverse 一起移除，渲染与预布局输入不得再残留该行
	EXPECT_EQ(CameraView.find("m_QmZoomInstantReverse"), std::string::npos);
	EXPECT_EQ(QmMenusSource.find("m_QmZoomInstantReverse"), std::string::npos);
	EXPECT_EQ(VisualCatalog.find("m_QmZoomInstantReverse"), std::string::npos);
	// 基础行数去掉该行后为 5；行数与渲染行数不一致会让卡片高度错位
	EXPECT_NE(VisualMeasure.find("return Rows(5.0f + (g_Config.m_QmCameraDrift ? 3.0f : 0.0f) + (g_Config.m_QmDynamicFov ? 2.0f : 0.0f) + (g_Config.m_QmAspectPreset == 6 ? 1.0f : 0.0f)) + Metrics.m_BodySize;"), std::string::npos);
}

TEST(QmStoragePath, BuildsCandidatesRelativeToExecutable)
{
	char aPath[IO_MAX_PATH_LENGTH];
	EXPECT_TRUE(StoragePathFromExecutable("C:\\QmClient\\DDNet.exe", "data/mapres", aPath, sizeof(aPath)));
	EXPECT_STREQ(aPath, "C:\\QmClient/data/mapres");
	EXPECT_TRUE(StoragePathFromExecutable("/opt/qmclient/DDNet", "storage.cfg", aPath, sizeof(aPath)));
	EXPECT_STREQ(aPath, "/opt/qmclient/storage.cfg");
	EXPECT_FALSE(StoragePathFromExecutable("DDNet.exe", "data", aPath, sizeof(aPath)));
}

TEST(QmLocalization, ContextRequiresOpeningAndClosingBrackets)
{
	EXPECT_TRUE(LocalizationIsContextLine("[menu]"));
	EXPECT_FALSE(LocalizationIsContextLine("[%s] %s (Map: %s, Time: %s)"));
	EXPECT_FALSE(LocalizationIsContextLine("[broken"));
	EXPECT_FALSE(LocalizationIsContextLine("plain"));
}

TEST(QmLocalizationSource, LocalizedDropdownNamesAreNotStaticHeapPointers)
{
	const std::string Chat = ReadTextFile("src/game/client/components/chat.cpp");
	const std::string Menus = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Localization = ReadTextFile("src/game/localization.cpp");

	EXPECT_EQ(Chat.find("static const char *s_apBackendNames[] = {Localize"), std::string::npos);
	EXPECT_EQ(Menus.find("static std::vector<const char *> s_LlmProviderDropDownNames ="), std::string::npos);
	EXPECT_EQ(Menus.find("static const char *s_apSourceNames[] ="), std::string::npos);
	EXPECT_EQ(Menus.find("static const char *s_apOutgoingModeNames[] ="), std::string::npos);
	EXPECT_NE(Localization.find("if(LocalizationIsContextLine(pLine))"), std::string::npos);
	EXPECT_NE(Localization.find("Couldn't open language file '%s'"), std::string::npos);
}

TEST(QmUiScaleSource, ScaleChangesResetContainersAndInvalidateScaleKeys)
{
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string TClientMenusSource = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string QmMenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Update = FunctionBody(UiSource, "void CUi::Update(");
	const std::string Screen = FunctionBody(UiSource, "const CUIRect *CUi::Screen()");
	const std::string QmUiScaleHelper = FunctionBody(QmMenusSource, "void CMenus::RenderQmSettingsSliderWithValueInput(");
	const std::string CameraView = FunctionBody(QmMenusSource, "void CMenus::RenderQmVisualCameraViewContent(");

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmUiScale, qm_ui_scale, 100, 50, 200"), std::string::npos);
	EXPECT_NE(Update.find("Client()->OnWindowResize();"), std::string::npos);
	EXPECT_NE(Screen.find("QmUiVirtualScreenHeight(g_Config.m_QmUiScale)"), std::string::npos);
	EXPECT_NE(MenusSource.find("StyleKey.m_UiScaleBucket = std::clamp(g_Config.m_QmUiScale, 50, 200);"), std::string::npos);
	EXPECT_NE(TClientMenusSource.find("std::clamp(g_Config.m_QmUiScale, 50, 200)"), std::string::npos);
	EXPECT_NE(QmUiScaleHelper.find("Options.m_Flags = Flags;"), std::string::npos);
	EXPECT_NE(QmUiScaleHelper.find("Options.m_CommitPolicy = (Flags & CUi::SCROLLBAR_OPTION_DELAYUPDATE) != 0 ?"), std::string::npos);
	EXPECT_NE(CameraView.find("RenderValue(\"qmclient-ui-scale\", \"UI scale\", &s_QmUiScaleInputId, &g_Config.m_QmUiScale, 50, 200, \"%\", CUi::SCROLLBAR_OPTION_DELAYUPDATE);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientPreLayoutUsesDeckContentCoordinates)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string PreLayout = FunctionBody(Source, "const auto BuildTClientConditionalRowsPreLayoutInput =");

	EXPECT_EQ(PreLayout.find("StartRows"), std::string::npos);
	EXPECT_NE(PreLayout.find("CTClientSettingsRowAllocator Rows(Content)"), std::string::npos);
	EXPECT_NE(PreLayout.find("s_vTinyTeeModeButtons"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsInputFieldsReserveTrailingActionsAndKeepQueueUnitsInline)
{
	const std::string FormsSource = ReadTextFile("src/game/client/QmUi/UiForms.cpp");
	const std::string ThemeSource = ReadTextFile("src/game/client/QmUi/UiTheme.h");
	const std::string Forms = FunctionBody(FormsSource, "SInputFieldResult InputField(");
	EXPECT_NE(Forms.find("CUIRect InputHitRect = Layout.m_ShellRect;"), std::string::npos);
	EXPECT_NE(Forms.find("RenderOptions.m_pHitRect = &InputHitRect;"), std::string::npos);
	EXPECT_NE(ThemeSource.find("ColorHSLA(g_Config.m_QmUiColor), g_Config.m_QmUiOpacity / 100.0f"), std::string::npos);

	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string SkinQueueSection = FunctionBody(SettingsSource, "const auto RenderSkinQueue =");
	EXPECT_NE(SkinQueueSection.find("QueueIntervalOptions.m_pSuffix = \"ms\";"), std::string::npos);
	EXPECT_EQ(SkinQueueSection.find("Ui()->DoLabel(&IntervalUnit"), std::string::npos);
}

TEST(QmUiScaleSource, TouchMenusRespectCallerProvidedScaledHeight)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_ingame_touch_controls.cpp");
	EXPECT_EQ(Source.find("MainView.h = 600.0f - 40.0f - MainView.y;"), std::string::npos);
	EXPECT_NE(Source.find("void CMenusIngameTouchControls::RenderTouchButtonEditor(CUIRect MainView)"), std::string::npos);
	EXPECT_NE(Source.find("void CMenusIngameTouchControls::RenderTouchButtonBrowser(CUIRect MainView)"), std::string::npos);
	EXPECT_NE(Source.find("void CMenusIngameTouchControls::RenderPreviewSettings(CUIRect MainView)"), std::string::npos);
}

TEST(QmDemoCutRender, UsesExportedCutAsRenderSource)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_demo.cpp");
	const std::string SlicePopup = FunctionBody(Source, "void CMenus::RenderDemoPlayerSliceSavePopup(CUIRect MainView)");

	EXPECT_NE(SlicePopup.find("str_format(m_aPendingDemoRenderSelectionName, sizeof(m_aPendingDemoRenderSelectionName), \"%s.demo\", m_DemoSliceInput.GetString());"), std::string::npos);
	EXPECT_EQ(SlicePopup.find("str_copy(m_aPendingDemoRenderSelectionName, m_aCurrentDemoSelectionName"), std::string::npos);
}

TEST(QmNewUiMenuBranches, AppearanceNamePlateContainsNameplateTextControlsWithoutInternalScrollRegion)
{
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string NamePlateBranch = BlockBodyAfter(SettingsSource, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_NAME_PLATE)");
	ASSERT_FALSE(NamePlateBranch.empty());

	const size_t TextSettingsPos = NamePlateBranch.find("Localize(\"Nameplate text\")");
	const size_t HookStrengthPos = NamePlateBranch.find("Localize(\"Hook Strength\")");
	ASSERT_NE(TextSettingsPos, std::string::npos);
	ASSERT_NE(HookStrengthPos, std::string::npos);
	EXPECT_LT(TextSettingsPos, HookStrengthPos);
	EXPECT_EQ(NamePlateBranch.find("appearance-name-plate-title"), std::string::npos);

	EXPECT_NE(NamePlateBranch.find("g_Config.m_QmNameplateTextEffects"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("QM_TEXT_EFFECT_BORDER"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("QM_TEXT_EFFECT_GRADIENT"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("QM_TEXT_EFFECT_RAINBOW"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("QM_TEXT_EFFECT_GLOW"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Localize(\"Playing effects\")"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Localize(\"Spectate effects\")"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Localize(\"Demo effects\")"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Localize(\"Demo target\")"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Localize(\"Border range\")"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Localize(\"Glow range\")"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("DoLine_ColorPicker(&s_NameplateTextBorderColorId"), std::string::npos);

	EXPECT_EQ(NamePlateBranch.find("static CScrollRegion s_NameplateTextCardScrollRegion;"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("BeginSettingsScrollRegion(s_NameplateTextCardScrollRegion"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("FinishSettingsScrollRegion(s_NameplateTextCardScrollRegion"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("static CScrollRegion s_NameplateTextPlayingDropDownScrollRegion;"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("static CScrollRegion s_NameplateTextSpectateDropDownScrollRegion;"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("static CScrollRegion s_NameplateTextDemoDropDownScrollRegion;"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("static CScrollRegion s_NameplateTextDemoTargetDropDownScrollRegion;"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("State.m_SelectionPopupContext.m_pScrollRegion = &ScrollRegion;"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("s_NameplateTextDemoTargetDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_NameplateTextDemoTargetDropDownScrollRegion;"), std::string::npos);

	const std::string QmSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	EXPECT_EQ(QmSource.find("auto RenderNameplateTextSettings = [&](CUIRect &CardContent)"), std::string::npos);
	EXPECT_EQ(QmSource.find("RenderNameplateTextSettings(CardContent);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DemoBrowserUsesExplicitLegacyShellBranches)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_demo.cpp");
	const std::string RenderDemoBrowser = FunctionBody(Source, "void CMenus::RenderDemoBrowser(CUIRect MainView)");
	const std::string RenderDemoBrowserList = FunctionBody(Source, "void CMenus::RenderDemoBrowserList(CUIRect ListView, bool &WasListboxItemActivated)");
	const std::string RenderDemoBrowserDetails = FunctionBody(Source, "void CMenus::RenderDemoBrowserDetails(CUIRect DetailsView)");
	const std::string RenderDemoBrowserButtons = FunctionBody(Source, "void CMenus::RenderDemoBrowserButtons(CUIRect ButtonsView, bool WasListboxItemActivated)");
	const size_t UseNewUiButtonsPos = RenderDemoBrowserButtons.find("if(UseNewUi)");
	ASSERT_NE(UseNewUiButtonsPos, std::string::npos);
	const size_t UseNewUiButtonsBodyStart = RenderDemoBrowserButtons.find("{", UseNewUiButtonsPos);
	ASSERT_NE(UseNewUiButtonsBodyStart, std::string::npos);
	const size_t UseNewUiButtonsBodyEnd = MatchingBrace(RenderDemoBrowserButtons, UseNewUiButtonsBodyStart);
	ASSERT_NE(UseNewUiButtonsBodyEnd, std::string::npos);
	const std::string UseNewUiButtonsBranch = RenderDemoBrowserButtons.substr(UseNewUiButtonsBodyStart, UseNewUiButtonsBodyEnd - UseNewUiButtonsBodyStart);
	const size_t LegacyButtonsElsePos = RenderDemoBrowserButtons.find("CUIRect ButtonBarTop, ButtonBarBottom;", UseNewUiButtonsBodyEnd);
	ASSERT_NE(LegacyButtonsElsePos, std::string::npos);
	const std::string LegacyButtonsBranch = RenderDemoBrowserButtons.substr(LegacyButtonsElsePos);

	EXPECT_NE(Source.find("const bool UseNewUi = g_Config.m_QmNewUi != 0;"), std::string::npos);
	EXPECT_NE(RenderDemoBrowser.find("if(UseNewUi)"), std::string::npos);
	EXPECT_NE(RenderDemoBrowser.find("MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowser.find("MainView.Margin(10.0f, &MainView);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowser.find("MainView.HSplitBottom(44.0f, &ListView, &ButtonsView);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowser.find("MainView.HSplitBottom(22.0f * 2.0f + 5.0f, &ListView, &ButtonsView);"), std::string::npos);
	EXPECT_EQ(RenderDemoBrowser.find("MainView.HSplitBottom(22.0f * 2.0f + 10.0f, &ListView, &ButtonsView);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("Headers.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_T, 5.0f);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("ListBox.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_B, 5.0f);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("const float HeaderGap = UseNewUi ? 4.0f : 2.0f;"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("const float RowHeight = UseNewUi ? ms_ListheaderHeight + 1.0f : ms_ListheaderHeight;"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("CColumn aCols[] = {"), std::string::npos);
	EXPECT_EQ(RenderDemoBrowserList.find("static CColumn s_aCols[] = {"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("{COL_MARKERS, SORT_MARKERS, FONT_ICON_BOOKMARK, 1, true, UseNewUi ? 34.0f : 30.0f, {0}, Localizable(\"Markers\")}"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("{COL_LENGTH, SORT_LENGTH, Localizable(\"Length\"), 1, false, UseNewUi ? 84.0f : 75.0f, {0}, nullptr}"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("{COL_DATE, SORT_DATE, Localizable(\"Date\"), 1, false, UseNewUi ? 156.0f : 150.0f, {0}, nullptr}"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("aCols[9].m_Width = BrowsingScreenshots ? (UseNewUi ? 176.0f : 170.0f) : (UseNewUi ? 156.0f : 150.0f);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserList.find("s_ListBox.DoStart(UseNewUi ? RowHeight : ms_ListheaderHeight, m_vpFilteredDemos.size(), 1, 3, m_DemolistSelectedIndex, &ListBox, false, IGraphics::CORNER_ALL);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserDetails.find("Header.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_T, 5.0f);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserDetails.find("Contents.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.15f), IGraphics::CORNER_B, 5.0f);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserDetails.find("Contents.Margin(5.0f, &Contents);"), std::string::npos);
	EXPECT_NE(RenderDemoBrowserButtons.find("if(UseNewUi)"), std::string::npos);
	EXPECT_NE(UseNewUiButtonsBranch.find("CUIRect MainRow = ButtonsView;"), std::string::npos);
	EXPECT_NE(UseNewUiButtonsBranch.find("const float ButtonWidth = MainRow.h * 1.55f;"), std::string::npos);
	EXPECT_NE(UseNewUiButtonsBranch.find("const float RowHeight = minimum(22.0f, ButtonsView.h);"), std::string::npos);
	EXPECT_NE(UseNewUiButtonsBranch.find("ButtonsView.HSplitTop(3.0f, nullptr, &ButtonsView);"), std::string::npos);
	EXPECT_NE(UseNewUiButtonsBranch.find("ButtonsView.HSplitBottom(3.0f, &ButtonsView, nullptr);"), std::string::npos);
	EXPECT_NE(LegacyButtonsBranch.find("ButtonsView.HSplitMid(&ButtonBarTop, &ButtonBarBottom, 5.0f);"), std::string::npos);
	EXPECT_EQ(RenderDemoBrowser.find("MainView.Draw(MenuPanelColor()"), std::string::npos);
}

TEST(QmNewUiMenuBranches, MapHistoryUsesFullHeightTabbedResponsiveCardGrid)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string RenderFavoriteMaps = FunctionBody(Source, "void CMenus::RenderServerbrowserFavoriteMaps(");

	EXPECT_EQ(RenderFavoriteMaps.find("SplitHistoryPanel"), std::string::npos);
	EXPECT_EQ(RenderFavoriteMaps.find("SplitHistoryColumns"), std::string::npos);
	EXPECT_NE(RenderFavoriteMaps.find("s_aFavoriteMapsWorkspaceTabButtons"), std::string::npos);
	EXPECT_NE(RenderFavoriteMaps.find("QmMapHistoryUi::GridColumns(HistoryPanel.w - QmMapHistoryUi::LIST_SCROLLBAR_WIDTH, CardRowHeight)"), std::string::npos);
	EXPECT_NE(RenderFavoriteMaps.find("QmMapHistoryUi::StackControls(HistoryPanel.w, Layout.m_ControlHeight)"), std::string::npos);
	EXPECT_NE(RenderFavoriteMaps.find("s_MapHistoryListBox.SetScrollbarAlwaysReserved(true);"), std::string::npos);
	EXPECT_NE(RenderFavoriteMaps.find("s_MapHistoryListBox.DoStart(CardRowHeight"), std::string::npos);
}

TEST(QmNewUiMenuBranches, QmLocalizationEnglishOverlayUsesExplicitEnglishFile)
{
	const std::string Source = ReadTextFile("src/game/client/gameclient.cpp");

	EXPECT_EQ(Source.find("str_format(aBuf, sizeof(aBuf), \"qmclient/%s\", g_Config.m_ClLanguagefile);"), std::string::npos);
	EXPECT_EQ(Source.find("static void LoadQmClientLanguageOverlay("), std::string::npos);
	EXPECT_EQ(Source.find("const char *pQmLanguageFile = g_Config.m_ClLanguagefile[0] != '\\0' ? g_Config.m_ClLanguagefile : \"english.txt\";"), std::string::npos);
	EXPECT_EQ(Source.find("const char *pQmLanguageFile = pLanguageFile[0] != '\\0' ? pLanguageFile : \"english.txt\";"), std::string::npos);
	EXPECT_EQ(Source.find("if(str_comp(pLanguageFile, \"languages/simplified_chinese.txt\") == 0)"), std::string::npos);
	EXPECT_EQ(Source.find("const char *pQmLanguageFile = pLanguageFile[0] != '\\0' ? pLanguageFile : \"languages/english.txt\";"), std::string::npos);
	EXPECT_EQ(Source.find("str_format(aBuf, sizeof(aBuf), \"qmclient/%s\", pQmLanguageFile);"), std::string::npos);
	EXPECT_EQ(Source.find("LoadQmClientLanguageOverlay(g_Localization, g_Config.m_ClLanguagefile, Storage(), Console());"), std::string::npos);
	EXPECT_NE(Source.find("g_Localization.Load(g_Config.m_ClLanguagefile, Storage(), Console());"), std::string::npos);
}

TEST(QmNewUiMenuBranches, QmClientTabLabelsDoNotCacheLocalizedPointers)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	EXPECT_EQ(Source.find("static const char *s_apQmTabNames"), std::string::npos);
	EXPECT_EQ(Source.find("s_aQmLanguageFile"), std::string::npos);
	EXPECT_NE(Source.find("const char *apQmTabNames[NUMBER_OF_QMCLIENT_SETTINGS_TABS] = {};"), std::string::npos);
	EXPECT_NE(Source.find("apQmTabNames[QMCLIENT_SETTINGS_TAB_VISUAL] = Localize(\"Visuals\");"), std::string::npos);
	EXPECT_NE(Source.find("apQmTabNames[QMCLIENT_SETTINGS_TAB_FUNCTION] = Localize(\"Functions\");"), std::string::npos);
	EXPECT_NE(Source.find("apQmTabNames[QMCLIENT_SETTINGS_TAB_HUD] = Localize(\"HUD\");"), std::string::npos);
	EXPECT_NE(Source.find("apQmTabNames[QMCLIENT_SETTINGS_TAB_CONTRIBUTORS] = Localize(\"Contributors\");"), std::string::npos);
	EXPECT_NE(Source.find("apQmTabNames[QMCLIENT_SETTINGS_TAB_CONFIG] = Localize(\"Config\");"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TranslateTargetRatioDoesNotRenderSkipNotes)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string TranslateModule = FunctionBody(Source, "void CMenus::RenderQmFunctionTranslateContent(");

	EXPECT_NE(TranslateModule.find("RenderSliderWithNumberInput(&s_LocalDetectRatioSelectorId"), std::string::npos);
	EXPECT_EQ(TranslateModule.find("qmclient-translate-skip-target-language-note"), std::string::npos);
	EXPECT_EQ(TranslateModule.find("qmclient-translate-skip-numeric-note"), std::string::npos);
}

TEST(QmNewUiMenuBranches, QmClientUpdateFlowUsesQmClientNamingAndComparisonHelper)
{
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/tclient.cpp");
	const std::string TClientHeader = ReadTextFile("src/game/client/components/tclient/tclient.h");
	const std::string MenusStartSource = ReadTextFile("src/game/client/components/menus_start.cpp");
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string QmMenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string OnUpdate = FunctionBody(TClientSource, "void CTClient::OnUpdate()");

	EXPECT_NE(TClientSource.find("#include <game/client/components/qmclient/update_version.h>"), std::string::npos);
	EXPECT_NE(TClientSource.find("static constexpr const char *QMCLIENT_INFO_URL"), std::string::npos);
	EXPECT_NE(TClientSource.find("QMCLIENT_UPDATE_PACKAGE_NAME = \"QmClient-windows.zip\""), std::string::npos);
	EXPECT_NE(TClientSource.find("qm_update_verify_manifest_package"), std::string::npos);
	EXPECT_NE(TClientSource.find("qm_update_verify_package_digest"), std::string::npos);
	EXPECT_NE(TClientSource.find("qm_update_extract_bootstrap_updater"), std::string::npos);
	EXPECT_NE(TClientSource.find("ResultSha256()"), std::string::npos);
	EXPECT_NE(TClientSource.find("FetchQmClientUpdateInfo();"), std::string::npos);
	EXPECT_NE(TClientSource.find("FinishQmClientUpdateInfo();"), std::string::npos);
	EXPECT_NE(TClientSource.find("ResetQmClientUpdateInfoTask();"), std::string::npos);
	EXPECT_NE(TClientSource.find("NeedQmClientUpdate()"), std::string::npos);
	EXPECT_NE(TClientSource.find("RequestQmClientUpdateCheckAndUpdate()"), std::string::npos);
	EXPECT_NE(TClientSource.find("ParseQmClientUpdateRelease"), std::string::npos);
	EXPECT_EQ(TClientSource.find("NeedUpdate()"), std::string::npos);
	EXPECT_EQ(TClientSource.find("FetchTClientInfo()"), std::string::npos);
	EXPECT_EQ(TClientSource.find("FinishTClientInfo()"), std::string::npos);
	EXPECT_EQ(TClientSource.find("ResetTClientInfoTask()"), std::string::npos);
	EXPECT_EQ(TClientSource.find("TCLIENT_INFO_URL"), std::string::npos);
	EXPECT_EQ(TClientSource.find("TCLIENT_UPDATE_EXE_URL"), std::string::npos);
	EXPECT_EQ(TClientSource.find("CalculateHashes(m_aUpdatePackageTmp"), std::string::npos);
	EXPECT_LT(OnUpdate.find("FinishUpdateDownloads();"), OnUpdate.find("!IsUpdateChecking() && !IsUpdateDownloading() && !m_UpdateReady"));
	EXPECT_NE(TClientSource.find("Force && m_UpdateShutdownRequested"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmAutoUpdate, qm_auto_update, 0"), std::string::npos);
	EXPECT_NE(ConfigSource.find("QmShowOutdatedVersionWarning"), std::string::npos);
	const std::string FunctionMetrics = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogFunctionMetrics.h");
	EXPECT_NE(FunctionMetrics.find("Localizable(\"Automatic updates\")"), std::string::npos);
	EXPECT_NE(FunctionMetrics.find("Localizable(\"Show outdated version warning\")"), std::string::npos);
	EXPECT_NE(FunctionMetrics.find("{&g_Config.m_QmAutoUpdate,"), std::string::npos);
	// 卡片目录重构后该行迁到 metrics 表，标签与配置绑定的双重强度在这里继续钉住。
	EXPECT_NE(FunctionMetrics.find("{&g_Config.m_QmShowOutdatedVersionWarning, Localizable(\"Show outdated version warning\")"), std::string::npos);
	// 菜单页骨架里不该再有这条文案的生产者。
	EXPECT_EQ(QmMenusSource.find("Show outdated version warning"), std::string::npos);

	EXPECT_NE(TClientHeader.find("m_pQmClientUpdateInfoTask"), std::string::npos);
	EXPECT_NE(TClientHeader.find("m_FetchedQmClientUpdateInfo"), std::string::npos);
	EXPECT_NE(TClientHeader.find("m_QmClientAutoUpdateAfterCheck"), std::string::npos);
	EXPECT_NE(TClientHeader.find("m_aQmClientLatestVersionStr"), std::string::npos);
	EXPECT_NE(TClientHeader.find("m_pUpdatePackageTask"), std::string::npos);
	EXPECT_NE(TClientHeader.find("m_UpdateShutdownRequested"), std::string::npos);
	EXPECT_EQ(TClientHeader.find("m_pTClientInfoTask"), std::string::npos);
	EXPECT_EQ(TClientHeader.find("m_FetchedTClientInfo"), std::string::npos);
	EXPECT_EQ(TClientHeader.find("m_AutoUpdateAfterCheck"), std::string::npos);
	EXPECT_EQ(TClientHeader.find("m_aVersionStr"), std::string::npos);

	EXPECT_NE(MenusStartSource.find("m_FetchedQmClientUpdateInfo"), std::string::npos);
	EXPECT_NE(MenusStartSource.find("NeedQmClientUpdate()"), std::string::npos);
	EXPECT_NE(MenusStartSource.find("defined(CONF_AUTOUPDATE) && defined(CONF_FAMILY_WINDOWS)"), std::string::npos);
	EXPECT_NE(MenusStartSource.find("if(g_Config.m_QmAutoUpdate)"), std::string::npos);
	EXPECT_EQ(MenusStartSource.find("m_FetchedTClientInfo"), std::string::npos);
	EXPECT_EQ(MenusStartSource.find("NeedUpdate()"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientHeaderIncludesGeneratedProtocolForWeaponDefaults)
{
	const std::string TClientHeader = ReadTextFile("src/game/client/components/tclient/tclient.h");

	EXPECT_NE(TClientHeader.find("#include <generated/protocol.h>"), std::string::npos);
	EXPECT_NE(TClientHeader.find("m_aGoresPreHammerWeapon[NUM_DUMMIES] = {WEAPON_GUN, WEAPON_GUN};"), std::string::npos);
}

TEST(QmNewUiMenuBranches, StartMenuKeepsExplicitUseV2AndLegacyButtonPaths)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_start.cpp");
	const std::string RenderStartMenuImpl = FunctionBody(Source, "void CMenusStart::RenderStartMenuImpl(");
	const std::string UseV2Block = BlockBodyAfter(RenderStartMenuImpl, "if(UseV2Layout)");

	EXPECT_NE(Source.find("void CMenusStart::RenderStartMenu(CUIRect MainView)"), std::string::npos);
	EXPECT_NE(Source.find("RenderStartMenuImpl(MainView, false);"), std::string::npos);
	EXPECT_NE(Source.find("void CMenusStart::RenderStartMenuV2(CUIRect MainView)"), std::string::npos);
	EXPECT_NE(Source.find("RenderStartMenuImpl(MainView, true);"), std::string::npos);
	EXPECT_NE(RenderStartMenuImpl.find("if(UseV2Layout)"), std::string::npos);
	EXPECT_NE(UseV2Block.find("ui_widget::PrimaryButton"), std::string::npos);
	EXPECT_NE(UseV2Block.find("ui_widget::SecondaryButton"), std::string::npos);
	EXPECT_EQ(UseV2Block.find("DoButton_Menu("), std::string::npos);
	EXPECT_NE(RenderStartMenuImpl.find("static float s_aMenuButtonScale[MenuButtonCount] = {};"), std::string::npos);
	EXPECT_NE(RenderStartMenuImpl.find("const auto ScaleButtonRect = [](const CUIRect &Base, float Scale) {"), std::string::npos);
	EXPECT_NE(RenderStartMenuImpl.find("GameClient()->m_Menus.DoButton_Menu(&s_QuitButton"), std::string::npos);
	EXPECT_NE(RenderStartMenuImpl.find("GameClient()->m_Menus.DoButton_Menu(&s_PlayButton"), std::string::npos);
}

TEST(QmNewUiMenuBranches, StartMenuEntryKeepsLegacyStartPageWithQmNewUi)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	EXPECT_NE(Source.find("else if(m_ShowStart)"), std::string::npos);
	const std::string Render = FunctionBody(Source, "void CMenus::Render()");
	const size_t StartMenuPos = Render.find("else if(m_ShowStart)");
	ASSERT_NE(StartMenuPos, std::string::npos);
	const size_t StartMenuBodyStart = Render.find("{", StartMenuPos);
	ASSERT_NE(StartMenuBodyStart, std::string::npos);
	const size_t StartMenuBodyEnd = MatchingBrace(Render, StartMenuBodyStart);
	ASSERT_NE(StartMenuBodyEnd, std::string::npos);
	const std::string StartMenuBlock = Render.substr(StartMenuBodyStart, StartMenuBodyEnd - StartMenuBodyStart);
	EXPECT_NE(StartMenuBlock.find("m_MenusStart.RenderStartMenu(Screen);"), std::string::npos);
	EXPECT_EQ(StartMenuBlock.find("m_MenusStart.RenderStartMenuV2(Screen);"), std::string::npos);
	EXPECT_EQ(StartMenuBlock.find("g_Config.m_QmNewUi"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsShellKeepsExplicitQmNewUiContainerBranch)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettings = FunctionBody(Source, "void CMenus::RenderSettings(CUIRect MainView)");
	const size_t UseNewSettingsUiIfPos = RenderSettings.find("if(UseNewSettingsUi)");
	ASSERT_NE(UseNewSettingsUiIfPos, std::string::npos);
	const size_t UseNewSettingsUiBodyStart = RenderSettings.find("{", UseNewSettingsUiIfPos);
	ASSERT_NE(UseNewSettingsUiBodyStart, std::string::npos);
	const size_t UseNewSettingsUiBodyEnd = MatchingBrace(RenderSettings, UseNewSettingsUiBodyStart);
	ASSERT_NE(UseNewSettingsUiBodyEnd, std::string::npos);
	const std::string UseNewSettingsUiBlock = RenderSettings.substr(UseNewSettingsUiBodyStart, UseNewSettingsUiBodyEnd - UseNewSettingsUiBodyStart);
	const size_t OldSettingsUiElsePos = RenderSettings.find("else", UseNewSettingsUiBodyEnd);
	ASSERT_NE(OldSettingsUiElsePos, std::string::npos);
	const size_t OldSettingsUiBodyStart = RenderSettings.find("{", OldSettingsUiElsePos);
	ASSERT_NE(OldSettingsUiBodyStart, std::string::npos);
	const size_t OldSettingsUiBodyEnd = MatchingBrace(RenderSettings, OldSettingsUiBodyStart);
	ASSERT_NE(OldSettingsUiBodyEnd, std::string::npos);
	const std::string OldSettingsUiBlock = RenderSettings.substr(OldSettingsUiBodyStart, OldSettingsUiBodyEnd - OldSettingsUiBodyStart);
	const std::string SettingsHeaderBranch = BlockBodyAfter(RenderSettings, "if(UseNewSettingsUi)\n\t{\n\t\tTabBar.Margin(10.0f, &TabBar);");
	const std::string SettingsHeaderLegacyBranch = BlockBodyAfter(RenderSettings, "else\n\t{\n\t\tTabBar.HSplitTop(50.0f, &Button, &TabBar);");

	EXPECT_NE(Source.find("const bool UseNewSettingsUi = g_Config.m_QmNewUi != 0;"), std::string::npos);
	EXPECT_NE(UseNewSettingsUiBlock.find("TabBar.Draw(SettingsTabbarColor()"), std::string::npos);
	EXPECT_NE(UseNewSettingsUiBlock.find("Shell.m_ContentPanelRect.Draw(MenuPanelColor()"), std::string::npos);
	EXPECT_EQ(UseNewSettingsUiBlock.find("MainView.Draw(ms_ColorTabbarActive"), std::string::npos);
	EXPECT_NE(OldSettingsUiBlock.find("MainView.Draw(ms_ColorTabbarActive"), std::string::npos);
	EXPECT_EQ(OldSettingsUiBlock.find("SettingsTabbarColor()"), std::string::npos);
	EXPECT_EQ(OldSettingsUiBlock.find("MenuPanelColor()"), std::string::npos);
	EXPECT_EQ(SettingsHeaderBranch.find("Button.Draw(ms_ColorTabbarActive"), std::string::npos);
	EXPECT_NE(SettingsHeaderLegacyBranch.find("Button.Draw(ms_ColorTabbarActive"), std::string::npos);
}

TEST(QmNewUiMenuBranches, LegacyMenusKeepTabAndPanelShellConnected)
{
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string MenuShellSplit = "const bool UseNewUi = g_Config.m_QmNewUi != 0;\n\t\t\tScreen.HSplitTop(MenuMenubarHeight(UseNewUi), &TabBar, &MainView);\n\t\t\tif(UseNewUi)\n\t\t\t\tMainView.HSplitTop(6.0f, nullptr, &MainView);";
	EXPECT_NE(MenusSource.find("constexpr float MENU_MENUBAR_HEIGHT_NEW = 24.0f;"), std::string::npos);
	EXPECT_NE(MenusSource.find("constexpr float MENU_MENUBAR_HEIGHT_LEGACY = 30.0f;"), std::string::npos);
	EXPECT_NE(MenusSource.find("constexpr float MenuMenubarHeight(bool UseNewUi)"), std::string::npos);
	EXPECT_NE(MenusSource.find(MenuShellSplit), std::string::npos);
	EXPECT_NE(MenusSource.find("case IClient::STATE_ONLINE:"), std::string::npos);
	EXPECT_NE(MenusSource.find(MenuShellSplit, MenusSource.find("case IClient::STATE_ONLINE:")), std::string::npos);

	const std::string QmClientSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	EXPECT_NE(QmClientSource.find("const bool UseNewUi = g_Config.m_QmNewUi != 0;"), std::string::npos);
	EXPECT_EQ(QmClientSource.find("if(UseNewUi)\n\t\t\tMainView.HSplitTop(Margin, nullptr, &MainView);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, AssetsPreviewUsesInnerFrameRectForPreviewImage)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings_assets.cpp");

	EXPECT_NE(Source.find("auto DrawPreviewFrame = [&](const CUIRect &TextureRect) -> CUIRect {"), std::string::npos);
	EXPECT_NE(Source.find("PreviewFrame.Margin(3.0f, &PreviewFrame);"), std::string::npos);
	EXPECT_NE(Source.find("return PreviewFrame;"), std::string::npos);
	EXPECT_NE(Source.find("auto ComputeAssetPreviewContentSize = [&](bool WorkshopCard)"), std::string::npos);
	EXPECT_NE(Source.find("CUIRect PreviewFrameRect = DrawPreviewFrame(Shell.m_TextureRect);"), std::string::npos);
	EXPECT_NE(Source.find("const auto [PreviewContentWidth, PreviewContentHeight] = ComputeAssetPreviewContentSize(WorkshopCard);"), std::string::npos);
	EXPECT_NE(Source.find("const auto [PreviewContentWidth, PreviewContentHeight] = ComputeAssetPreviewContentSize(true);"), std::string::npos);
	EXPECT_NE(Source.find("const CUIRect PreviewRect = ComputePreviewDrawRect(PreviewFrameRect, PreviewContentWidth, PreviewContentHeight);"), std::string::npos);
	EXPECT_EQ(Source.find("const CUIRect PreviewRect = ComputePreviewDrawRect(HeaderLayout.m_TextureRect, TextureWidth, TextureHeight);"), std::string::npos);
	EXPECT_EQ(Source.find("const CUIRect PreviewRect = ComputePreviewDrawRect(HeaderLayout.m_TextureRect, TextureWidth, TextureWidth);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsColorLabelsUseQmLocalizedKeys)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string MenusToml = ReadTextFile("qmclient_scripts/languages_qmclient/translations/i18n/menus.toml");

	EXPECT_EQ(Source.find("Localize(\"UI Color\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"Menu panel color\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"Menu panel opacity\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"Menu panel elevated opacity\")"), std::string::npos);
	EXPECT_EQ(Source.find("s_MenuPanelColorResetId"), std::string::npos);
	EXPECT_EQ(Source.find("g_Config.m_ClMenuPanelColor"), std::string::npos);
	EXPECT_EQ(Source.find("g_Config.m_UiColor"), std::string::npos);
	EXPECT_NE(Source.find("CSettingsContentRowFlow Rows(ContentRect, GraphicsMetrics);"), std::string::npos);
	EXPECT_NE(Source.find("CUIRect UiColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Source.find("DoLine_AlphaColorPicker(&s_UiColorResetId, ColorMetrics, &UiColorRow, Localize(\"Interface surface\"), &g_Config.m_QmUiColor, &g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_NE(Source.find("CUIRect MapBrowserColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Source.find("DoLine_AlphaColorPicker(&s_MapBrowserColorResetId, ColorMetrics, &MapBrowserColorRow, Localize(\"Map browser surface\"), &g_Config.m_QmMapBrowserColor, &g_Config.m_QmMapBrowserOpacity"), std::string::npos);
	EXPECT_NE(Source.find("CUIRect ScoreboardColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Source.find("DoLine_AlphaColorPicker(&s_ScoreboardColorResetId, ColorMetrics, &ScoreboardColorRow, Localize(\"Scoreboard surface\"), &g_Config.m_QmScoreboardColor, &g_Config.m_QmScoreboardOpacity"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmUiColor"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmMapBrowserColor"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmScoreboardColor"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmMapBrowserOpacity"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmScoreboardOpacity"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Interface surface\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Map browser surface\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Scoreboard surface\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"UI opacity\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"Map browser opacity\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"Scoreboard opacity\")"), std::string::npos);
	EXPECT_NE(MenusToml.find("key = \"UI opacity\""), std::string::npos);
	EXPECT_NE(MenusToml.find("simplified_chinese = \"界面不透明度\""), std::string::npos);
	EXPECT_NE(MenusToml.find("key = \"Map browser opacity\""), std::string::npos);
	EXPECT_NE(MenusToml.find("simplified_chinese = \"地图浏览器不透明度\""), std::string::npos);
	EXPECT_NE(MenusToml.find("key = \"Scoreboard opacity\""), std::string::npos);
	EXPECT_NE(MenusToml.find("simplified_chinese = \"计分板不透明度\""), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsGraphicsColorPickersExposeIndependentAlphaDomains)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Graphics = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(Graphics.empty());

	EXPECT_NE(Graphics.find("CSettingsContentRowFlow Rows(ContentRect, GraphicsMetrics);"), std::string::npos);
	EXPECT_NE(Graphics.find("CUIRect UiColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Graphics.find("DoLine_AlphaColorPicker(&s_UiColorResetId, ColorMetrics, &UiColorRow, Localize(\"Interface surface\"), &g_Config.m_QmUiColor, &g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_NE(Graphics.find("CUIRect MapBrowserColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Graphics.find("DoLine_AlphaColorPicker(&s_MapBrowserColorResetId, ColorMetrics, &MapBrowserColorRow, Localize(\"Map browser surface\"), &g_Config.m_QmMapBrowserColor, &g_Config.m_QmMapBrowserOpacity"), std::string::npos);
	EXPECT_NE(Graphics.find("CUIRect ScoreboardColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Graphics.find("DoLine_AlphaColorPicker(&s_ScoreboardColorResetId, ColorMetrics, &ScoreboardColorRow, Localize(\"Scoreboard surface\"), &g_Config.m_QmScoreboardColor, &g_Config.m_QmScoreboardOpacity"), std::string::npos);
	EXPECT_EQ(Graphics.find("graphics-ui-opacity"), std::string::npos);
	EXPECT_EQ(Graphics.find("graphics-map-browser-opacity"), std::string::npos);
	EXPECT_EQ(Graphics.find("graphics-scoreboard-opacity"), std::string::npos);
	EXPECT_EQ(Graphics.find("DoSliderWithValueInput("), std::string::npos);
}

TEST(QmNewUiMenuBranches, DynamicIslandColorPickerOwnsExistingOpacitySetting)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderQmHudDynamicIslandContent(");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("DoLine_AlphaColorPicker(&s_DynamicIslandBgColorId, CurrentSettingsContentMetrics(), &Content, Localize(\"Background color\"), &g_Config.m_QmHudIslandBgColor, &g_Config.m_QmHudIslandBgOpacity, 0x9C460E, 80)"), std::string::npos);
	EXPECT_EQ(Body.find("s_QmHudIslandBgOpacityInputId"), std::string::npos);
	EXPECT_EQ(Body.find("RenderQmSettingsSliderWithValueInput(&s_QmHudIslandBgOpacityInputId"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_COL(QmHudIslandBgColor, qm_hud_island_bg_color, 0x9C460E, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmHudIslandBgOpacity, qm_hud_island_bg_opacity, 80"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TranslateUiColorsPreserveConfiguredAlpha)
{
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	for(const char *pKey : {
		    "MACRO_CONFIG_COL(QmTranslateBtnColorDisabled, qm_translate_btn_color_disabled, 0xD1000029, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA",
		    "MACRO_CONFIG_COL(QmTranslateBtnColorEnabled, qm_translate_btn_color_enabled, 0xE69E5E86, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA",
		    "MACRO_CONFIG_COL(QmTranslateMenuBgColor, qm_translate_menu_bg_color, 0xF200001F, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA",
		    "MACRO_CONFIG_COL(QmTranslateMenuOptionSelected, qm_translate_menu_option_selected, 0xE69E5E86, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA",
		    "MACRO_CONFIG_COL(QmTranslateMenuOptionNormal, qm_translate_menu_option_normal, 0xE6000033, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA",
	    })
		EXPECT_NE(Config.find(pKey), std::string::npos) << pKey;
}

TEST(QmNewUiMenuBranches, DynamicIslandSettingsOmitsEdgeMarginControl)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderQmHudDynamicIslandContent(");
	ASSERT_FALSE(Body.empty());

	EXPECT_EQ(Body.find("QmHudIslandEdgeMargin"), std::string::npos);
	EXPECT_EQ(Body.find("Localize(\"Edge margin\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DynamicIslandOwnsHookCountdownToggleNextToSwitchCountdown)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderQmHudDynamicIslandContent(");
	// 预布局输入（命中判定）已搬到卡片目录的 HUD 卡片模块 BuildHudPreLayoutInput。
	const std::string HudCatalog = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogHud.cpp");
	const std::string PreLayout = FunctionBody(HudCatalog, "FSettingsCardPreLayoutInput BuildHudPreLayoutInput(");
	ASSERT_FALSE(Body.empty());
	ASSERT_FALSE(PreLayout.empty());

	const std::string Toggle =
		"RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmHookCountdown, \"Enable hook countdown\", Localize(\"Enable hook countdown\"), &g_Config.m_QmHookCountdown);";
	const size_t HookRow = Body.find(Toggle);
	ASSERT_NE(HookRow, std::string::npos);
	// 必须排在开关倒计时的展开组之前：那组末尾有 `if(!g_Config.m_QmSwitchCountdown) return;`，
	// 放到后面会被一起藏掉，用户就再也打不开钩子倒计时。
	const size_t SwitchRow = Body.find("&g_Config.m_QmSwitchCountdown, \"Enable switch countdown\"");
	ASSERT_NE(SwitchRow, std::string::npos);
	EXPECT_LT(HookRow, SwitchRow);
	// 常驻单行开关，不跟着开关倒计时的展开收起变化。
	EXPECT_NE(Body.find("if(!g_Config.m_QmSwitchCountdown)\n\t\treturn;"), std::string::npos);

	// 预布局命中判定必须与渲染路径同序，否则点击会落到错位的行上。
	// 卡片目录里的桥接调用多带一个 pMenus（卡片模块不持有 CMenus 成员）。
	EXPECT_NE(PreLayout.find("HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmHookCountdown, &g_Config.m_QmHookCountdown)"), std::string::npos);
	const size_t HookInput = PreLayout.find("&g_Config.m_QmHookCountdown, &g_Config.m_QmHookCountdown");
	const size_t SwitchInput = PreLayout.find("&g_Config.m_QmSwitchCountdown, &g_Config.m_QmSwitchCountdown");
	ASSERT_NE(SwitchInput, std::string::npos);
	EXPECT_LT(HookInput, SwitchInput);

	// 默认关闭，避免升级后凭空多出一个跟随 Tee 的环。
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmHookCountdown, qm_hook_countdown, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DynamicIslandOwnsSwitchCountdownLocationChooser)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderQmHudDynamicIslandContent(");
	ASSERT_FALSE(Body.empty());

	// 总开关与两个位置开关成组；移除位置标题，关闭总开关时位置开关整组消失。
	EXPECT_NE(Body.find("RenderQmHudCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmSwitchCountdown, \"Enable switch countdown\", Localize(\"Enable switch countdown\"), &g_Config.m_QmSwitchCountdown)"), std::string::npos);
	EXPECT_EQ(Body.find("qmclient-switch-countdown-location-label"), std::string::npos);
	EXPECT_NE(Body.find("RenderQmHudCheckbox(Content, LineHeight, LineSpacing, qm_card_catalog::SwitchCountdownFollowTeeId(), \"qmclient-switch-countdown-follow-tee\", Localize(\"Follow Tee\"), &FollowTee)"), std::string::npos);
	EXPECT_NE(Body.find("RenderQmHudCheckbox(Content, LineHeight, LineSpacing, qm_card_catalog::SwitchCountdownMediaIslandId(), \"qmclient-switch-countdown-media-island\", Localize(\"Show in Dynamic Island\"), &MediaIsland)"), std::string::npos);
	EXPECT_NE(Body.find("if(!g_Config.m_QmSwitchCountdown)\n\t\treturn;"), std::string::npos);
	// 两个位置都不勾时回落到关闭总开关，不留下界面与渲染互相打架的非法组合。
	EXPECT_NE(Body.find("if(FollowTee == 0 && MediaIsland == 0)"), std::string::npos);
	EXPECT_NE(Body.find("g_Config.m_QmSwitchCountdownMode = QmHudSwitchCountdownModeFromLocations(FollowTee != 0, MediaIsland != 0, CurrentMode);"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmSwitchCountdown, qm_switch_countdown, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmSwitchCountdownMode, qm_switch_countdown_mode, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DynamicIslandEdgeMarginIsOnlyAnIgnoredLegacyCommand)
{
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string GameClient = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string Callback = FunctionBody(GameClient, "void ConDiscardLegacyHudIslandEdgeMargin(");
	const std::string OnConsoleInit = FunctionBody(GameClient, "void CGameClient::OnConsoleInit()");

	EXPECT_EQ(Config.find("qm_hud_island_edge_margin"), std::string::npos);
	ASSERT_FALSE(Callback.empty());
	EXPECT_EQ(Callback.find("g_Config"), std::string::npos);
	EXPECT_NE(OnConsoleInit.find("pConsole->Register(\"qm_hud_island_edge_margin\", \"?i[value]\", CFGFLAG_CLIENT, ConDiscardLegacyHudIslandEdgeMargin, nullptr"), std::string::npos);
}

TEST(QmNewUiMenuBranches, LineInputRendersActiveTextOnlyOnce)
{
	const std::string Source = ReadTextFile("src/game/client/lineinput.cpp");
	const std::string Header = ReadTextFile("src/engine/textrender.h");
	const std::string TextSource = ReadTextFile("src/engine/client/text.cpp");
	const std::string Render = FunctionBody(Source, "STextBoundingBox CLineInput::Render(");
	const std::string RenderSelection = FunctionBody(Source, "void CLineInput::RenderSelection(");
	const std::string RenderCaret = FunctionBody(Source, "void CLineInput::RenderCaret(");
	ASSERT_FALSE(Render.empty());
	ASSERT_FALSE(RenderSelection.empty());
	ASSERT_FALSE(RenderCaret.empty());
	const auto CountOccurrences = [](const std::string &Text, const char *pNeedle) {
		size_t Count = 0;
		for(size_t Position = Text.find(pNeedle); Position != std::string::npos; Position = Text.find(pNeedle, Position + 1))
			++Count;
		return Count;
	};

	EXPECT_NE(Render.find("m_CaretPosition = Cursor.m_CursorRenderedPosition;"), std::string::npos);
	EXPECT_NE(Render.find("SetCompositionWindowPosition(m_CaretPosition + vec2"), std::string::npos);
	EXPECT_NE(Render.find("Cursor.m_RenderCursor = false;"), std::string::npos);
	EXPECT_NE(Render.find("Cursor.m_RenderSelection = false;"), std::string::npos);
	const size_t SelectionPrepass = Render.find("CTextCursor SelectionCursor = Cursor;");
	const size_t SelectionUnderlay = Render.find("RenderSelection(SelectionCursor, TextRender()->GetTextSelectionColor());", SelectionPrepass);
	const size_t TextPass = Render.find("TextRender()->TextEx(&Cursor, pDisplayStr);", SelectionPrepass);
	ASSERT_NE(SelectionPrepass, std::string::npos);
	ASSERT_NE(SelectionUnderlay, std::string::npos);
	ASSERT_NE(TextPass, std::string::npos);
	EXPECT_LT(SelectionPrepass, SelectionUnderlay);
	EXPECT_LT(SelectionUnderlay, TextPass);
	EXPECT_NE(Render.find("if(Cursor.m_HasCursorRenderedPosition)"), std::string::npos);
	EXPECT_NE(Render.find("RenderCaret(Cursor, Cursor.m_ForceCursorRendering"), std::string::npos);
	EXPECT_EQ(Render.find("CTextCursor CaretCursor;"), std::string::npos);
	EXPECT_EQ(Render.find("TextRender()->TextEx(&CaretCursor, pDisplayStr);"), std::string::npos);
	EXPECT_EQ(CountOccurrences(Render, "TextRender()->TextEx(&Cursor, pDisplayStr);"), 2u);
	EXPECT_NE(Header.find("bool m_RenderCursor = true;"), std::string::npos);
	EXPECT_NE(Header.find("bool m_RenderSelection = true;"), std::string::npos);
	EXPECT_NE(Header.find("bool m_HasCursorRenderedPosition = false;"), std::string::npos);
	EXPECT_NE(TextSource.find("const bool HasRenderedCursor = HasCursor && pCursor->m_RenderCursor;"), std::string::npos);
	EXPECT_NE(TextSource.find("const bool HasRenderedSelection = HasSelection && pCursor->m_RenderSelection;"), std::string::npos);
	const size_t SelectionRenderPos = TextSource.find("if(TextContainer.m_HasSelection)");
	const size_t TextRenderPos = TextSource.find("if(!TextContainer.m_StringInfo.m_vCharacterQuads.empty())");
	ASSERT_NE(SelectionRenderPos, std::string::npos);
	ASSERT_NE(TextRenderPos, std::string::npos);
	EXPECT_GT(SelectionRenderPos, TextRenderPos);
	EXPECT_NE(TextSource.find("if(SelectionStarted)"), std::string::npos);
	EXPECT_NE(TextSource.find("pCursor->m_HasCursorRenderedPosition = true;"), std::string::npos);
	const size_t TextExPos = TextSource.find("void TextEx(CTextCursor *pCursor, const char *pText, int Length = -1) override");
	const size_t LayoutOnlyGuard = TextSource.find("if((pCursor->m_Flags & TEXTFLAG_RENDER) == 0)", TextExPos);
	const size_t LayoutContainer = TextSource.find("STextContainer LayoutContainer;", LayoutOnlyGuard);
	const size_t LayoutPass = TextSource.find("AppendTextContainerImpl(LayoutContainer, pCursor, pText, Length);", LayoutContainer);
	const size_t RenderContainer = TextSource.find("STextContainerIndex TextCont;", LayoutPass);
	ASSERT_NE(TextExPos, std::string::npos);
	ASSERT_NE(LayoutOnlyGuard, std::string::npos);
	ASSERT_NE(LayoutContainer, std::string::npos);
	ASSERT_NE(LayoutPass, std::string::npos);
	ASSERT_NE(RenderContainer, std::string::npos);
	EXPECT_LT(LayoutOnlyGuard, LayoutContainer);
	EXPECT_LT(LayoutContainer, LayoutPass);
	EXPECT_LT(LayoutPass, RenderContainer);
	EXPECT_EQ(TextSource.find("CreateTextContainer(LayoutContainer", LayoutOnlyGuard), std::string::npos);
	EXPECT_NE(RenderSelection.find("Graphics()->TextureClear();"), std::string::npos);
	EXPECT_NE(RenderSelection.find("Graphics()->QuadsBegin();"), std::string::npos);
	EXPECT_NE(RenderSelection.find("Graphics()->QuadsEnd();"), std::string::npos);
	EXPECT_NE(RenderSelection.find("Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);"), std::string::npos);
	EXPECT_EQ(RenderSelection.find("RenderQuadContainerEx"), std::string::npos);
	EXPECT_NE(RenderCaret.find("Graphics()->QuadsBegin();"), std::string::npos);
	EXPECT_NE(RenderCaret.find("Graphics()->QuadsEnd();"), std::string::npos);
	EXPECT_EQ(RenderCaret.find("RenderQuadContainerEx"), std::string::npos);
	EXPECT_NE(RenderCaret.find("if(!Cursor.m_HasCursorRenderedPosition)"), std::string::npos);
	EXPECT_EQ(RenderCaret.find("m_CursorRenderedPosition.x < 0.0f"), std::string::npos);
	EXPECT_EQ(RenderCaret.find("m_CursorRenderedPosition.y < 0.0f"), std::string::npos);
	const size_t TextureClear = RenderCaret.find("Graphics()->TextureClear();");
	const size_t HiddenReturn = RenderCaret.find("if(!ForceVisible && !qm_lineinput::CaretVisibleForElapsed");
	ASSERT_NE(TextureClear, std::string::npos);
	ASSERT_NE(HiddenReturn, std::string::npos);
	EXPECT_LT(TextureClear, HiddenReturn);
}

TEST(QmNewUiMenuBranches, BufferedTextUploadsMissingGpuContainerWithoutUsingImmediateQuads)
{
	const std::string TextSource = ReadTextFile("src/engine/client/text.cpp");
	const std::string Render = FunctionBody(TextSource, "void RenderTextContainer(STextContainerIndex TextContainerIndex, const ColorRGBA &TextColor, const ColorRGBA &TextOutlineColor) override");
	const std::string Upload = FunctionBody(TextSource, "void UploadTextContainer(STextContainerIndex TextContainerIndex) override");

	ASSERT_FALSE(Render.empty());
	ASSERT_FALSE(Upload.empty());
	const size_t BufferedPath = Render.find("if(Graphics()->IsTextBufferingEnabled())");
	const size_t MissingContainer = Render.find("if(TextContainer.m_StringInfo.m_QuadBufferContainerIndex == -1)", BufferedPath);
	const size_t UploadMissingContainer = Render.find("UploadTextContainer(TextContainerIndex);", MissingContainer);
	const size_t BufferedRender = Render.find("Graphics()->RenderText(", UploadMissingContainer);
	const size_t ImmediateFallback = Render.find("else\n\t\t\t{\n\t\t\t\t// render tiles", BufferedPath);
	ASSERT_NE(BufferedPath, std::string::npos);
	ASSERT_NE(MissingContainer, std::string::npos);
	ASSERT_NE(UploadMissingContainer, std::string::npos);
	ASSERT_NE(BufferedRender, std::string::npos);
	ASSERT_NE(ImmediateFallback, std::string::npos);
	EXPECT_LT(BufferedPath, MissingContainer);
	EXPECT_LT(MissingContainer, UploadMissingContainer);
	EXPECT_LT(UploadMissingContainer, BufferedRender);
	EXPECT_LT(BufferedRender, ImmediateFallback);
	EXPECT_EQ(Render.find("Graphics()->IsTextBufferingEnabled() &&"), std::string::npos);
	EXPECT_NE(Render.find("Graphics()->QuadsBegin();"), std::string::npos);
	const size_t CreateBuffer = Upload.find("Graphics()->CreateBufferObject(");
	const size_t RecreateBuffer = Upload.find("Graphics()->RecreateBufferObject(");
	const size_t CreateContainer = Upload.find("Graphics()->CreateBufferContainer(&m_DefaultTextContainerInfo);");
	const size_t EmptyTextReturn = Upload.find("if(TextContainer.m_StringInfo.m_vCharacterQuads.empty())");
	ASSERT_NE(CreateBuffer, std::string::npos);
	ASSERT_NE(RecreateBuffer, std::string::npos);
	ASSERT_NE(CreateContainer, std::string::npos);
	ASSERT_NE(EmptyTextReturn, std::string::npos);
	EXPECT_LT(EmptyTextReturn, CreateBuffer);
	EXPECT_LT(CreateBuffer, RecreateBuffer);
	EXPECT_LT(RecreateBuffer, CreateContainer);
	EXPECT_EQ(Upload.find("Graphics()->DeleteBufferContainer("), std::string::npos);
}

TEST(QmNewUiMenuBranches, TextRendererKeepsInternalCaretStateSelfContained)
{
	const std::string Source = ReadTextFile("src/engine/client/text.cpp");
	const std::string Render = FunctionBody(Source, "void RenderTextContainer(STextContainerIndex TextContainerIndex, const ColorRGBA &TextColor, const ColorRGBA &TextOutlineColor) override");
	ASSERT_FALSE(Render.empty());

	const size_t CursorBlock = Render.find("if(TextContainer.m_HasCursor)");
	ASSERT_NE(CursorBlock, std::string::npos);
	EXPECT_NE(Render.find("Graphics()->TextureClear();", CursorBlock), std::string::npos);
	EXPECT_NE(Render.find("Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);", CursorBlock), std::string::npos);
}

TEST(QmNewUiMenuBranches, ColorPickerUsesIndependentPointerCapture)
{
	const std::string Source = ReadTextFile("src/game/client/ui.cpp");
	const std::string Picker = FunctionBody(Source, "EEditState CUi::DoPickerLogic(");
	ASSERT_FALSE(Picker.empty());

	EXPECT_NE(Picker.find("const bool Inside = MouseHovered(pRect);"), std::string::npos);
	EXPECT_NE(Picker.find("if(Inside && MouseButtonClicked(0))"), std::string::npos);
	EXPECT_NE(Picker.find("if(!CheckActiveItem(pId))"), std::string::npos);
	EXPECT_NE(Picker.find("if(!MouseButton(0))"), std::string::npos);
	EXPECT_EQ(Picker.find("m_pLastEditingItem"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ConsoleRestoresCompleteTextRenderState)
{
	const std::string Source = ReadTextFile("src/game/client/components/console.cpp");
	const std::string Render = FunctionBody(Source, "void CGameConsole::OnRender()");
	ASSERT_FALSE(Render.empty());

	for(const char *pState : {
		    "const ColorRGBA PreviousTextColor = TextRender()->GetTextColor();",
		    "const ColorRGBA PreviousTextOutlineColor = TextRender()->GetTextOutlineColor();",
		    "const ColorRGBA PreviousTextSelectionColor = TextRender()->GetTextSelectionColor();",
		    "const unsigned PreviousRenderFlags = TextRender()->GetRenderFlags();",
		    "const EFontPreset PreviousFontPreset = TextRender()->GetFontPreset();",
		    "TextRender()->SetRenderFlags(PreviousRenderFlags);",
		    "TextRender()->SetFontPreset(PreviousFontPreset);",
		    "TextRender()->TextOutlineColor(PreviousTextOutlineColor);",
		    "TextRender()->TextSelectionColor(PreviousTextSelectionColor);",
		    "TextRender()->TextColor(PreviousTextColor);",
	    })
		EXPECT_NE(Render.find(pState), std::string::npos) << pState;

	EXPECT_LT(Render.find("Ui()->SetEnabled(false);"), Render.find("TextRender()->SetRenderFlags(PreviousRenderFlags);"));
}

TEST(QmNewUiMenuBranches, DynamicIslandPreLayoutConsumesTheSameConditionalRows)
{
	const std::string Source = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogHud.cpp");
	const size_t FactoryPos = Source.find("FSettingsCardPreLayoutInput BuildHudPreLayoutInput(");
	ASSERT_NE(FactoryPos, std::string::npos);
	const std::string PreLayoutSource = Source.substr(FactoryPos);
	const size_t DynamicIslandPos = PreLayoutSource.find("case EQmModuleId::DynamicIsland:");
	const size_t PlayerStatsPos = PreLayoutSource.find("case EQmModuleId::PlayerStats:", DynamicIslandPos);
	ASSERT_NE(DynamicIslandPos, std::string::npos);
	ASSERT_NE(PlayerStatsPos, std::string::npos);
	const std::string DynamicIsland = PreLayoutSource.substr(DynamicIslandPos, PlayerStatsPos - DynamicIslandPos);

	EXPECT_NE(DynamicIsland.find("g_Config.m_QmHudIslandUseOriginalStyle"), std::string::npos);
	EXPECT_NE(DynamicIsland.find("g_Config.m_QmHudIslandShowTeam"), std::string::npos);
	EXPECT_EQ(DynamicIsland.find("ConsumeQmHudRow(Content); // edge margin"), std::string::npos);
	EXPECT_NE(DynamicIsland.find("ResolveSettingsColorRowLayout(Content, Metrics, false)"), std::string::npos);
	EXPECT_NE(DynamicIsland.find("if(!g_Config.m_QmHudIslandUseOriginalStyle)"), std::string::npos);
	// 开关倒计时总开关关闭时不再消耗位置行，与 ResolveQmHudDynamicIslandHeight 的行数保持一致。
	EXPECT_NE(DynamicIsland.find("&g_Config.m_QmSwitchCountdown, &g_Config.m_QmSwitchCountdown"), std::string::npos);
	EXPECT_NE(DynamicIsland.find("if(!g_Config.m_QmSwitchCountdown)"), std::string::npos);
	// 位置开关的行消耗与渲染路径共用卡片目录权威定义的同一组复选框 id（原先的独立位置标题行已移除）。
	EXPECT_NE(DynamicIsland.find("ToggleQmHudCountdownLocation(pMenus, Content, LineHeight, LineSpacing, SwitchCountdownFollowTeeId(), &FollowTee)"), std::string::npos);
	EXPECT_NE(DynamicIsland.find("ToggleQmHudCountdownLocation(pMenus, Content, LineHeight, LineSpacing, SwitchCountdownMediaIslandId(), &MediaIsland)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, GeneralSettingsListsShareSelectedAndHoveredBackgroundTokens)
{
	const std::string Tokens = ReadTextFile("src/game/client/QmUi/UiTokens.h");
	const std::string ListboxHeader = ReadTextFile("src/game/client/ui_listbox.h");
	const std::string Listbox = ReadTextFile("src/game/client/ui_listbox.cpp");
	const std::string Menus = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Settings = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Theme = FunctionBody(Menus, "void CMenus::RenderThemeSelection(");
	const std::string Language = FunctionBody(Settings, "bool CMenus::RenderLanguageSelection(");
	ASSERT_FALSE(Theme.empty());
	ASSERT_FALSE(Language.empty());

	EXPECT_NE(Tokens.find("LIST_ITEM_SELECTED{1.0f, 1.0f, 1.0f, 0.14f}"), std::string::npos);
	EXPECT_NE(Tokens.find("LIST_ITEM_HOVER{1.0f, 1.0f, 1.0f, 0.08f}"), std::string::npos);
	EXPECT_NE(ListboxHeader.find("void SetItemColors(ColorRGBA SelectedActive, ColorRGBA SelectedInactive, ColorRGBA Hovered)"), std::string::npos);
	EXPECT_NE(Listbox.find("m_SelectedItemActiveColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f);"), std::string::npos);
	EXPECT_NE(Listbox.find("Ui()->ScaleBackgroundAlpha(m_Active ? m_SelectedItemActiveColor : m_SelectedItemInactiveColor)"), std::string::npos);
	EXPECT_NE(Theme.find("s_ListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_HOVER);"), std::string::npos);
	const size_t LanguageSelectedCondition = Language.find("if(Selected)");
	const size_t LanguageSelected = Language.find("DrawRoundedSurface(Ui(), ItemRect, Ui()->ScaleBackgroundAlpha(ui_token::color::LIST_ITEM_SELECTED)");
	const size_t LanguageHoveredCondition = Language.find("if(Ui()->HotItem() == pRowId)");
	const size_t LanguageHovered = Language.find("DrawRoundedSurface(Ui(), ItemRect, Ui()->ScaleBackgroundAlpha(ui_token::color::LIST_ITEM_HOVER)");
	ASSERT_NE(LanguageSelectedCondition, std::string::npos);
	ASSERT_NE(LanguageSelected, std::string::npos);
	ASSERT_NE(LanguageHoveredCondition, std::string::npos);
	ASSERT_NE(LanguageHovered, std::string::npos);
	EXPECT_LT(LanguageSelectedCondition, LanguageSelected);
	EXPECT_LT(LanguageHoveredCondition, LanguageHovered);
	EXPECT_LT(LanguageSelected, LanguageHovered);
}

TEST(QmNewUiMenuBranches, DefaultUiSurfacesUseBlackThirtyPercent)
{
	const std::string QmConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables.h");

	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_COL(QmUiColor, qm_ui_color, 0x000000"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_COL(QmMapBrowserColor, qm_map_browser_color, 0x000000"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_COL(QmScoreboardColor, qm_scoreboard_color, 0x000000"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_INT(QmUiOpacity, qm_ui_opacity, 30"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_INT(QmUiCardOpacity, qm_ui_card_opacity, 30"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_INT(QmMapBrowserOpacity, qm_map_browser_opacity, 30"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_INT(QmScoreboardOpacity, qm_scoreboard_opacity, 30"), std::string::npos);

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_COL(UiColor, ui_color, 0x4D000000"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_COL(ClMenuPanelColor, cl_menu_panel_color, 0x000000"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(ClMenuPanelOpacity, cl_menu_panel_opacity, 30"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(ClMenuPanelElevatedOpacity, cl_menu_panel_elevated_opacity, 30"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(ClSettingsTabbarOpacity, cl_settings_tabbar_opacity, 30"), std::string::npos);
}

TEST(QmNewUiMenuBranches, WeaponTrajectoryExposesDefaultOnPistolGuideToggle)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string MenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string CardRegistrySource = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string WeaponTrajectoryBody = FunctionBody(MenusSource, "void CMenus::RenderQmFunctionWeaponTrajectoryContent(");
	// 卡片高度测量已搬到功能卡片目录的 MeasureFunctionCardHeight。
	const std::string FunctionCatalog = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogFunction.cpp");
	const std::string FunctionMeasure = FunctionBody(FunctionCatalog, "float MeasureFunctionCardHeight(");

	ASSERT_FALSE(WeaponTrajectoryBody.empty());
	ASSERT_FALSE(FunctionMeasure.empty());
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponTrajectoryGun, qm_weapon_trajectory_gun, 1, 0, 1"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponTrajectoryNinja, qm_weapon_trajectory_ninja, 0, 0, 1"), std::string::npos);
	EXPECT_NE(WeaponTrajectoryBody.find("RenderQmFunctionCheckbox(&g_Config.m_QmWeaponTrajectoryGun, \"qmclient-weapon-trajectory-gun\", Localize(\"Pistol guide line\")"), std::string::npos);
	EXPECT_NE(WeaponTrajectoryBody.find("RenderQmFunctionCheckbox(&g_Config.m_QmWeaponTrajectoryNinja, \"qmclient-weapon-trajectory-ninja\", Localize(\"Predict ninja path\")"), std::string::npos);
	EXPECT_NE(FunctionMeasure.find("case EQmModuleId::WeaponTrajectory: return g_Config.m_QmWeaponTrajectory == 0 ? Row() : Row() * 6.0f;"), std::string::npos);
	EXPECT_NE(CardRegistrySource.find("手枪辅助线 shouqiang fuzhuxian pistol guide line"), std::string::npos);
	EXPECT_NE(CardRegistrySource.find("预测忍者路径 yuce renzhe lujing predict ninja path"), std::string::npos);
}

TEST(QmNewUiMenuBranches, QmDefaultOffMigrationKeepsExplicitLegacyValues)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config.cpp");
	const std::string ClientSource = ReadTextFile("src/engine/client/client.cpp");
	const std::string DomainSource = ReadTextFile("src/engine/shared/config_domains.h");
	const std::string IncludeSource = ReadTextFile("src/engine/shared/config_includes.h");

	EXPECT_NE(IncludeSource.find("SET_CONFIG_DOMAIN(ConfigDomain::QMCLIENT)\n#include \"config_variables_qmclient.h\""), std::string::npos);
	EXPECT_NE(DomainSource.find("CONFIG_DOMAIN(QMCLIENT, \"qmclient/settings.cfg\", nullptr, nullptr, true)"), std::string::npos);
	EXPECT_NE(ClientSource.find("pConfigManager->Init();"), std::string::npos);
	EXPECT_NE(ClientSource.find("if(!pConsole->ExecuteFile(pConfigPath, IConsole::CLIENT_ID_UNSPECIFIED))"), std::string::npos);
	EXPECT_LT(ClientSource.find("pConfigManager->Init();"), ClientSource.find("if(!pConsole->ExecuteFile(pConfigPath, IConsole::CLIENT_ID_UNSPECIFIED))"));
	EXPECT_NE(ConfigSource.find("pVariable->m_ConfigDomain == ConfigDomain && (pVariable->m_Flags & CFGFLAG_SAVE) != 0 && !pVariable->IsDefault()"), std::string::npos);
	EXPECT_NE(ConfigSource.find("std::vector<char> vLineBuf(pVariable->MaxSerializedSize());"), std::string::npos);
	EXPECT_NE(ConfigSource.find("pVariable->Serialize(vLineBuf.data(), vLineBuf.size());"), std::string::npos);
	EXPECT_NE(ConfigSource.find("WriteLine(vLineBuf.data(), ConfigDomain);"), std::string::npos);
	EXPECT_EQ(ConfigSource.find("Reset(\"qm_"), std::string::npos);
}

TEST(QmNewUiMenuBranches, WeaponAnimationAdvancedControlsAreConfigurable)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string PlayersSource = ReadTextFile("src/game/client/components/players.cpp");
	const std::string MenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string RegistrySource = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponSwitchAnimDurationMs, qm_weapon_switch_anim_duration_ms, 300"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponSwitchAnimDistance, qm_weapon_switch_anim_distance, 40"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponSwitchAnimRotation, qm_weapon_switch_anim_rotation, 360"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponSwitchAnimEasing, qm_weapon_switch_anim_easing"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponReloadAnim, qm_weapon_reload_anim"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponReloadAnimProbability, qm_weapon_reload_anim_probability"), std::string::npos);

	EXPECT_NE(PlayersSource.find("g_Config.m_QmWeaponSwitchAnimDurationMs"), std::string::npos);
	EXPECT_NE(PlayersSource.find("g_Config.m_QmWeaponSwitchAnimDistance"), std::string::npos);
	EXPECT_NE(PlayersSource.find("g_Config.m_QmWeaponSwitchAnimRotation"), std::string::npos);
	EXPECT_NE(PlayersSource.find("g_Config.m_QmWeaponSwitchAnimEasing"), std::string::npos);

	const std::string WeaponAnimationContent = FunctionBody(MenusSource, "void CMenus::RenderQmVisualWeaponAnimationContent(");
	const size_t SwitchToggle = WeaponAnimationContent.find("RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponSwitchAnim");
	const size_t ReloadToggle = WeaponAnimationContent.find("RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponReloadAnim");
	const size_t ReloadProbability = WeaponAnimationContent.find("RenderValue(\"qmclient-weapon-reload-animation-probability\", \"Weapon reload animation probability\"");
	const size_t SharedControls = WeaponAnimationContent.find("if(!g_Config.m_QmWeaponSwitchAnim && !g_Config.m_QmWeaponReloadAnim)");
	const size_t SwitchControls = WeaponAnimationContent.find("if(!g_Config.m_QmWeaponSwitchAnim)", SharedControls);
	ASSERT_NE(SwitchToggle, std::string::npos);
	ASSERT_NE(ReloadToggle, std::string::npos);
	ASSERT_NE(ReloadProbability, std::string::npos);
	ASSERT_NE(SharedControls, std::string::npos);
	ASSERT_NE(SwitchControls, std::string::npos);
	EXPECT_LT(SwitchToggle, ReloadToggle);
	EXPECT_LT(ReloadToggle, ReloadProbability);
	EXPECT_LT(ReloadProbability, SharedControls);
	EXPECT_LT(SharedControls, SwitchControls);
	EXPECT_NE(WeaponAnimationContent.find("RenderValue(\"qmclient-weapon-switch-duration\", \"Weapon switch duration\"", SwitchControls), std::string::npos);
	EXPECT_NE(WeaponAnimationContent.find("RenderValue(\"qmclient-weapon-switch-distance\", \"Weapon switch distance\"", SwitchControls), std::string::npos);
	EXPECT_NE(WeaponAnimationContent.find("RenderValue(\"qmclient-weapon-switch-rotation\", \"Weapon switch rotation\"", SwitchControls), std::string::npos);
	EXPECT_NE(WeaponAnimationContent.find("Localize(\"Weapon switch easing\")", SwitchControls), std::string::npos);

	// 武器动画卡片的高度/重测版本/预布局输入都在视觉卡片目录里：
	// 高度决定展开后的行数，重测版本里装填动画占 2u，预布局必须命中同一个装填动画开关。
	const std::string VisualCatalog = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogVisual.cpp");
	const std::string VisualMeasure = FunctionBody(VisualCatalog, "float MeasureVisualCardHeight(");
	const std::string VisualRevision = FunctionBody(VisualCatalog, "uint64_t MeasureVisualCardRevision(");
	const std::string VisualPreLayout = FunctionBody(VisualCatalog, "FSettingsCardPreLayoutInput BuildVisualPreLayoutInput(");
	ASSERT_FALSE(VisualMeasure.empty());
	ASSERT_FALSE(VisualRevision.empty());
	ASSERT_FALSE(VisualPreLayout.empty());
	EXPECT_NE(VisualMeasure.find("ResolveQmVisualWeaponAnimationHeight(Metrics, g_Config.m_QmWeaponSwitchAnim != 0, g_Config.m_QmWeaponReloadAnim != 0)"), std::string::npos);
	EXPECT_NE(VisualRevision.find("(g_Config.m_QmWeaponReloadAnim ? 2u : 0u)"), std::string::npos);
	EXPECT_NE(VisualPreLayout.find("HandleQmHudCheckboxInput(pMenus, Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponReloadAnim, &g_Config.m_QmWeaponReloadAnim)"), std::string::npos);
	EXPECT_NE(RegistrySource.find("装填动画 zhuangtian donghua reload animation"), std::string::npos);
}

TEST(QmNewUiMenuBranches, EmoticonShadowHasConfigRenderPassAndVisualToggle)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string PlayersSource = ReadTextFile("src/game/client/components/players.cpp");
	const std::string EmoticonSource = ReadTextFile("src/game/client/components/emoticon.cpp");
	const std::string RenderPlayerBody = FunctionBody(PlayersSource, "void CPlayers::RenderPlayer(");
	const std::string EmoticonRenderBody = FunctionBody(EmoticonSource, "void CEmoticon::OnRender()");
	const std::string EmoticonItemsBody = BlockBodyAfter(EmoticonRenderBody, "for(int Emote = 0; Emote < NUM_EMOTICONS; Emote++)");
	const auto CountOccurrences = [](const std::string &Text, const char *pNeedle) {
		int Count = 0;
		size_t Pos = 0;
		while((Pos = Text.find(pNeedle, Pos)) != std::string::npos)
		{
			++Count;
			Pos += str_length(pNeedle);
		}
		return Count;
	};

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmEmoticonShadow, qm_emoticon_shadow, 0, 0, 1"), std::string::npos);
	EXPECT_NE(RenderPlayerBody.find("EmoticonShadowOpacity"), std::string::npos);
	EXPECT_NE(RenderPlayerBody.find("EmoticonShadowOffsetX"), std::string::npos);
	EXPECT_EQ(CountOccurrences(RenderPlayerBody, "if(g_Config.m_QmEmoticonShadow)"), 3);
	EXPECT_EQ(CountOccurrences(RenderPlayerBody, "Graphics()->SetColor(0.0f, 0.0f, 0.0f"), 3);
	EXPECT_NE(RenderPlayerBody.find("EmoticonShadowOffsetX * h"), std::string::npos);
	EXPECT_NE(RenderPlayerBody.find("EmoticonShadowOffsetY * h"), std::string::npos);
	EXPECT_NE(RenderPlayerBody.find("Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);\n\t\tGraphics()->RenderQuadContainerAsSprite"), std::string::npos);
	EXPECT_NE(RenderPlayerBody.find("Graphics()->SetColor(1.0f, 1.0f, 1.0f, a * Alpha);\n\t\t\tGraphics()->RenderQuadContainerAsSprite"), std::string::npos);
	EXPECT_NE(EmoticonRenderBody.find("EmoticonSelectorShadowOpacity"), std::string::npos);
	EXPECT_NE(EmoticonRenderBody.find("if(g_Config.m_QmEmoticonShadow)"), std::string::npos);
	EXPECT_NE(EmoticonRenderBody.find("Graphics()->SetColor(0.0f, 0.0f, 0.0f, EmoticonSelectorShadowOpacity);"), std::string::npos);
	EXPECT_NE(EmoticonRenderBody.find("ScreenCenter.x + Nudge.x + EmoticonSelectorShadowOffsetX"), std::string::npos);
	EXPECT_EQ(CountOccurrences(EmoticonItemsBody, "Graphics()->QuadsBegin();"), 2);
	const size_t ShadowBranch = EmoticonItemsBody.find("if(g_Config.m_QmEmoticonShadow)");
	ASSERT_NE(ShadowBranch, std::string::npos);
	const size_t ShadowClear = EmoticonItemsBody.find("Graphics()->TextureClear();", ShadowBranch);
	const size_t ShadowBegin = EmoticonItemsBody.find("Graphics()->QuadsBegin();", ShadowBranch);
	ASSERT_NE(ShadowClear, std::string::npos);
	ASSERT_NE(ShadowBegin, std::string::npos);
	EXPECT_LT(ShadowClear, ShadowBegin);
	// 皮肤卡的内容函数已迁入全局卡片目录（N3）：改在目录文件里定位函数体。
	// 「表情阴影」开关仍在皮肤外观卡内（QmCardCatalogSkin.cpp），未因迁移丢失。
	const std::string SkinCardSource = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogSkin.cpp");
	const std::string SkinAppearanceContent = FunctionBody(SkinCardSource, "void CMenus::RenderQmVisualSkinAppearanceContent(");
	const std::string SkinTransitionContent = FunctionBody(SkinCardSource, "void CMenus::RenderQmVisualSkinTransitionContent(");
	EXPECT_NE(SkinAppearanceContent.find("RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmEmoticonShadow"), std::string::npos);
	EXPECT_EQ(SkinTransitionContent.find("g_Config.m_QmEmoticonShadow"), std::string::npos);
	EXPECT_NE(SkinCardSource.find("Localize(\"Emoticon shadow\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NameplateOthersModeSuppressesLocalIdentityRows)
{
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string UpdateCoordXAlignFrameState = FunctionBody(Source, "void CNamePlates::UpdateCoordXAlignFrameState");
	const std::string RenderNamePlateGame = FunctionBody(Source, "void CNamePlates::RenderNamePlateGame");

	EXPECT_EQ(UpdateCoordXAlignFrameState.find("FrameState.m_LocalRoundedX = RoundCoordToCentitiles(GameClient()->m_LocalCharacterPos.x / 32.0f);\n\tFrameState.m_LocalAligned = true;"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("std::array<SCoordXAlignReference, NUM_DUMMIES> aLocalRefs{};"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("if(aLocalRefs[i].m_ClientId == ClientId)"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("CoordXAlignState.m_ReferenceClientId != ReferenceClientId"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("FrameState.m_aClientAligned[ReferenceClientId] = true;"), std::string::npos);
	EXPECT_EQ(UpdateCoordXAlignFrameState.find("if(ClientId == FrameState.m_LocalClientId"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("if(CoordXAlignState.m_Aligned)\n\t\t{"), std::string::npos);
	EXPECT_NE(UpdateCoordXAlignFrameState.find("FrameState.m_LocalAligned = true;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("const bool IsAnyLocalClient = GameClient()->IsLocalClientId(ClientId);"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("IsAnyLocalClient &&\n\t\tm_pData->m_CoordXAlignFrame.m_aClientAligned[ClientId];"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("CoordXAlignState.m_Aligned || m_pData->m_CoordXAlignFrame.m_LocalAligned"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("IsLocalClient &&\n\t\tm_pData->m_CoordXAlignFrame.m_LocalAligned"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("const bool OwnNameplateScopeVisible"), std::string::npos);
	// 录像机/禅模式重构后取值统一走 NameplateRenderValue(ConfigManager(), &...)：
	// 录制中读回接管前的真实值，未接管时与直接读 g_Config 等价。
	// 昵称可见性本身由 QmNameplateNameScope 单元测试覆盖；这里只锁定两件无法从
	// 纯函数观察到的事实：取值仍经 NameplateRenderValue，且判定委托给纯函数。
	EXPECT_NE(RenderNamePlateGame.find("NameplateRenderValue(ConfigManager(), &g_Config.m_QmNameplateShowScope)"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("ShouldShowQmNameplateName(NameplateScope, pPlayerInfo->m_Local, GameClient()->IsLocalClientId(ClientId))"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowClientId = Data.m_ShowName && (g_Config.m_Debug || g_Config.m_ClNamePlatesIds) && !HideIdentity;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowClan = Data.m_ShowName && g_Config.m_ClNamePlatesClan && !HideIdentity;"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("const bool NameplateScopeAllowsCoords"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("const bool CoordModuleAllowsCoords = IsAnyLocalClient ? g_Config.m_QmNameplateCoordsOwn : g_Config.m_QmNameplateCoords;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("const bool ShowLocalAlignedCoordX = CoordModuleAllowsCoords && CoordXAlignHintEnabled && LocalCoordXAligned;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowCoordX = (CoordModuleAllowsCoords && g_Config.m_QmNameplateCoordX != 0) || ShowLocalAlignedCoordX;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowCoordY = CoordModuleAllowsCoords && g_Config.m_QmNameplateCoordY != 0;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowCoords = CoordModuleAllowsCoords || ShowLocalAlignedCoordX;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("TrackCoordXAlign &&\n\t\tGameClient()->m_Snap.m_LocalClientId >= 0"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_CoordXAligned = IsAnyLocalClient ? LocalCoordXAligned : CoordXAlignState.m_Aligned;"), std::string::npos);
	EXPECT_EQ(RenderNamePlateGame.find("!IsAnyLocalClient &&\n\t\tGameClient()->m_Snap.m_LocalClientId >= 0"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("if(Data.m_ShowName && !HideIdentity && g_Config.m_TcWarList && g_Config.m_TcWarListShowClan"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_Local = pPlayerInfo->m_Local;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, PlayerTitlePrecedesInlineClientIdAndNameWithoutOverridingIdSettings)
{
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string AddNameRow = FunctionBody(Source, "void AddNameRow(");
	const std::string RenderNamePlateGame = FunctionBody(Source, "void CNamePlates::RenderNamePlateGame");

	const size_t FriendMark = AddNameRow.find("AddPart<CNamePlatePartFriendMark>(This);");
	const size_t Developer = AddNameRow.find("AddPart<CNamePlatePartTitle>(This);");
	const size_t InlineClientId = AddNameRow.find("AddPart<CNamePlatePartClientId>(This, false);");
	const size_t Name = AddNameRow.find("AddPart<CNamePlatePartName>(This);");
	ASSERT_NE(FriendMark, std::string::npos);
	ASSERT_NE(Developer, std::string::npos);
	ASSERT_NE(InlineClientId, std::string::npos);
	ASSERT_NE(Name, std::string::npos);
	EXPECT_LT(FriendMark, Developer);
	EXPECT_LT(Developer, InlineClientId);
	EXPECT_LT(InlineClientId, Name);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowClientId = Data.m_ShowName && (g_Config.m_Debug || g_Config.m_ClNamePlatesIds) && !HideIdentity;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NameplateStrongHookRowReservesLayoutWithoutContentWidth)
{
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string RangeSize = FunctionBody(Source, "vec2 RangeSize(");
	const std::string AddHookRow = FunctionBody(Source, "void AddHookRow(");
	const std::string RenderNamePlateGame = FunctionBody(Source, "void CNamePlates::RenderNamePlateGame");
	const std::string BuildPreviewData = FunctionBody(Source, "static void BuildNamePlatePreviewData");

	EXPECT_NE(Source.find("bool m_ReserveHookStrongWeakRow;"), std::string::npos);
	EXPECT_NE(Source.find("bool m_ReserveLineHeight = false;"), std::string::npos);
	EXPECT_NE(Source.find("bool ReserveLineHeight() const { return m_ReserveLineHeight; }"), std::string::npos);
	EXPECT_NE(Source.find("class CNamePlatePartHookStrongWeakRowReserve"), std::string::npos);
	EXPECT_NE(RangeSize.find("else if(Part.ReserveLineHeight())\n\t\t\t{"), std::string::npos);
	EXPECT_NE(RangeSize.find("LineSize.y = std::max(LineSize.y, Part.Size().y + Part.Padding().y);"), std::string::npos);
	EXPECT_NE(AddHookRow.find("AddPart<CNamePlatePartHookStrongWeakRowReserve>(This);"), std::string::npos);
	EXPECT_LT(AddHookRow.find("AddPart<CNamePlatePartHookStrongWeakRowReserve>(This);"), AddHookRow.find("AddPart<CNamePlatePartHookStrongWeak>(This);"));
	// 运行期走解析后的 DisplaySettings；预览路径仍直接读 g_Config，两条都要钉住。
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ReserveHookStrongWeakRow = (g_Config.m_Debug && !DemoPlayback) || DisplaySettings.m_StrongWeak > 0;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowHookStrongWeak = false;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowHookStrongWeak = (g_Config.m_Debug && !DemoPlayback) || (DisplaySettings.m_StrongWeak > 0 && ShouldShowQmHookStrongWeakScope(DisplaySettings.m_StrongWeakScope, false, Strong, Weak));"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("Data.m_ReserveHookStrongWeakRow = g_Config.m_Debug || g_Config.m_ClNamePlatesStrong > 0;"), std::string::npos);
	EXPECT_NE(BuildPreviewData.find("Data.m_ShowHookStrongWeak = NameplateScopeAllowsPreview && g_Config.m_ClNamePlatesStrong > 0 && ShouldShowQmHookStrongWeakScope(g_Config.m_QmNameplateHookStrongWeakScope, false, Strong, Weak);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NameplateGameUsesFullScopeReferenceFrame)
{
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string RenderNamePlateGame = FunctionBody(Source, "void CNamePlates::RenderNamePlateGame");
	const std::string ResetNamePlates = FunctionBody(Source, "void CNamePlates::ResetNamePlates");

	EXPECT_NE(Source.find("CNamePlate m_aNamePlateFrameReferences[MAX_CLIENTS];"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("CNamePlate *pLayoutReference = nullptr;"), std::string::npos);
	// NameplatePartiallyHidden 现在的含义是「六档里没选到全体」，取值同样经
	// NameplateRenderValue 读回接管前的真实值。
	EXPECT_NE(RenderNamePlateGame.find("const bool NameplatePartiallyHidden = NameplateRenderValue(ConfigManager(), &g_Config.m_QmNameplateShowScope) != QM_NAMEPLATE_SHOW_SCOPE_ALL;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("if(Alpha > 0.0f && NameplateFreeMoveEnabled() && NameplatePartiallyHidden)"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("CNamePlateData FrameData = Data;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("FrameData.m_ShowName = true;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("const bool FrameShowLocalAlignedCoordX = CoordModuleAllowsCoords && CoordXAlignHintEnabled && LocalCoordXAligned;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("FrameData.m_ShowCoords = CoordModuleAllowsCoords || FrameShowLocalAlignedCoordX;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("CNamePlate &FrameNamePlate = m_pData->m_aNamePlateFrameReferences[ClientId];"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("FrameNamePlate.Update(*GameClient(), FrameData);"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("pLayoutReference = &FrameNamePlate;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("NamePlate.Render(*GameClient(), Position - vec2(0.0f, (float)g_Config.m_ClNamePlatesOffset), pLayoutReference);"), std::string::npos);
	EXPECT_NE(ResetNamePlates.find("for(CNamePlate &NamePlate : m_pData->m_aNamePlateFrameReferences)\n\t\tNamePlate.Reset(*GameClient());"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowDirection = !pPlayerInfo->m_Local;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowDirection = true;"), std::string::npos);
	EXPECT_NE(RenderNamePlateGame.find("Data.m_ShowDirection = pPlayerInfo->m_Local;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, HudNotificationsKeepEdgeGeometryStableDuringSlide)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/hud_notifications/hud_notifications.cpp");
	const std::string MeasureVisibleRect = FunctionBody(Source, "CUIRect CQmHudNotifications::MeasureVisibleRect");
	const std::string RenderNotifications = FunctionBody(Source, "void CQmHudNotifications::RenderNotifications");

	EXPECT_EQ(MeasureVisibleRect.find("MaxSlideOffset"), std::string::npos);
	EXPECT_NE(MeasureVisibleRect.find("return QmHudNotifications::NotificationVisibleRect(BaseRect, MaxWidth, UsedHeight, Flow);"), std::string::npos);
	EXPECT_NE(RenderNotifications.find("Alpha = SmoothStep(ElapsedMs / (float)AnimMs);"), std::string::npos);
	EXPECT_NE(RenderNotifications.find("Alpha = 1.0f - SmoothStep((ElapsedMs - AnimMs - HoldMs) / (float)AnimMs);"), std::string::npos);
	EXPECT_NE(RenderNotifications.find("OffsetX = (1.0f - Alpha) * 14.0f * QmHudNotifications::SmallTextScale(FontSize);"), std::string::npos);
	EXPECT_EQ(RenderNotifications.find("OffsetX = (1.0f - Alpha) * 32.0f;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsCardMigrationsKeepVersionPendingWhenExactMigrationFails)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const size_t ProfilesStart = Source.find("if(g_Config.m_QmCardLayoutVersion < 2)");
	const size_t StatusBarStart = Source.find("if(g_Config.m_QmCardLayoutVersion < 3)");
	const size_t TeeStart = Source.find("if(g_Config.m_QmCardLayoutVersion < 4)");
	ASSERT_NE(ProfilesStart, std::string::npos);
	ASSERT_NE(StatusBarStart, std::string::npos);
	ASSERT_NE(TeeStart, std::string::npos);
	const std::string Profiles = Source.substr(ProfilesStart, StatusBarStart - ProfilesStart);
	const std::string StatusBar = Source.substr(StatusBarStart, TeeStart - StatusBarStart);
	const std::string Tee = Source.substr(TeeStart, Source.find("if(g_Config.m_QmCardLayoutVersion < 5)", TeeStart) - TeeStart);
	for(const std::string *pMigration : {&Profiles, &StatusBar, &Tee})
	{
		const size_t ShouldMigrate = pMigration->find("const bool ShouldMigrate = ExplicitStatus == qm_card_order::EExplicitLayoutStatus::MATCH;");
		const size_t CandidateChanged = pMigration->find("const bool CandidateChanged = ShouldMigrate && qm_card_order::MigrateExactLayout", ShouldMigrate);
		const size_t FailureGuard = pMigration->find("if(ShouldMigrate && !CandidateChanged)", CandidateChanged);
		const size_t Persist = pMigration->find("if(!PersistCandidate(Candidate, CandidateChanged))", FailureGuard);
		ASSERT_NE(ShouldMigrate, std::string::npos);
		ASSERT_NE(CandidateChanged, std::string::npos);
		ASSERT_NE(FailureGuard, std::string::npos);
		ASSERT_NE(Persist, std::string::npos);
		EXPECT_LT(ShouldMigrate, CandidateChanged);
		EXPECT_LT(CandidateChanged, FailureGuard);
		EXPECT_LT(FailureGuard, Persist);
	}
}

TEST(QmNewUiMenuBranches, NewOpacityControlsDoNotChainLegacyPanelOpacity)
{
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");

	EXPECT_EQ(MenusSource.find("m_ClMenuPanelOpacity / 100.0f) * (g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_EQ(MenusSource.find("m_ClMenuPanelElevatedOpacity / 100.0f) * (g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_EQ(MenusSource.find("m_ClSettingsTabbarOpacity / 100.0f) * (g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_EQ(MenusSource.find("m_ClMenuPanelOpacity / 100.0f) * (g_Config.m_QmMapBrowserOpacity"), std::string::npos);
	EXPECT_EQ(MenusSource.find("m_ClMenuPanelElevatedOpacity / 100.0f) * (g_Config.m_QmMapBrowserOpacity"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NewColorControlsUseIndependentUiDomains)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string BrowserSource = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string ScoreboardSource = ReadTextFile("src/game/client/components/scoreboard.cpp");

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_COL(QmUiColor, qm_ui_color"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_COL(QmMapBrowserColor, qm_map_browser_color"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_COL(QmScoreboardColor, qm_scoreboard_color"), std::string::npos);
	EXPECT_NE(MenusSource.find("ColorHSLA(g_Config.m_QmUiColor)"), std::string::npos);
	EXPECT_NE(MenusSource.find("ColorHSLA(g_Config.m_QmMapBrowserColor)"), std::string::npos);
	EXPECT_EQ(MenusSource.find("ColorHSLA(g_Config.m_ClMenuPanelColor)"), std::string::npos);
	const std::string UpdateColors = FunctionBody(MenusSource, "void CMenus::UpdateColors()");
	const std::string RenderBackground = FunctionBody(MenusSource, "void CMenus::RenderBackground()");
	EXPECT_NE(UpdateColors.find("ColorHSLA(g_Config.m_UiColor, true)"), std::string::npos);
	EXPECT_NE(RenderBackground.find("ms_GuiColor.WithAlpha(1.0f)"), std::string::npos);
	EXPECT_EQ(RenderBackground.find("g_Config.m_QmUiColor"), std::string::npos);
	EXPECT_NE(BrowserSource.find("ColorHSLA(g_Config.m_QmMapBrowserColor)"), std::string::npos);
	EXPECT_NE(ScoreboardSource.find("ColorHSLA(g_Config.m_QmScoreboardColor)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ScoreboardBackgroundsUseScoreboardOpacity)
{
	const std::string Source = ReadTextFile("src/game/client/components/scoreboard.cpp");

	EXPECT_NE(Source.find("Color.a = ScoreboardUiAlpha(AlphaScale);"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmScoreboardOpacity / 100.0f"), std::string::npos);
	EXPECT_NE(Source.find("ScoreboardDecorationColor(GameClient()->GetDDTeamColor(DDTeam).WithAlpha(0.5f * ItemAlpha))"), std::string::npos);
	EXPECT_NE(Source.find("Row.Draw(ScoreboardDecorationColor(ui_token::color::ACCENT_PRIMARY_DIM.WithMultipliedAlpha(ItemAlpha * 1.45f))"), std::string::npos);
	EXPECT_NE(Source.find("Row.Draw(ScoreboardDecorationColor(ColorRGBA(0.7f, 0.7f, 0.7f, 0.7f * ItemAlpha))"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ScoreboardDdTeamLabelUsesUnifiedBelowRowLayout)
{
	const std::string Source = ReadTextFile("src/game/client/components/scoreboard.cpp");
	const std::string RenderScoreboard = FunctionBody(Source, "void CScoreboard::RenderScoreboard(");

	EXPECT_NE(RenderScoreboard.find("ResolveScoreboardTeamLabelLayout("), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("if(EndsDDTeam)"), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("TextRender()->Text(TeamLabelLayout.m_X, TeamLabelLayout.m_Y, TeamFontSize, aBuf);"), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("TeamLabelLayout.m_IconY"), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("SCOREBOARD_TEAM_MODE_ICON_SIZE"), std::string::npos);
	EXPECT_EQ(RenderScoreboard.find("TeamLabelLayout.m_Y,\n\t\t\t\t\tTeamFontSize"), std::string::npos);
	EXPECT_EQ(RenderScoreboard.find("NumPlayers > 8"), std::string::npos);
	EXPECT_EQ(RenderScoreboard.find("State.m_TeamStartX"), std::string::npos);
	EXPECT_EQ(RenderScoreboard.find("Row.x + Row.w / 2.0f - TextRender()->TextWidth(TeamFontSize, aBuf) / 2.0f + 5.0f"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ScoreboardMediaIsInformationOnly)
{
	const std::string Source = ReadTextFile("src/game/client/components/scoreboard.cpp");
	const std::string Helper = FunctionBody(Source, "int DoScoreboardMediaIconButton(");

	EXPECT_NE(Helper.find("const float IconAlpha = std::clamp(ContentAlpha"), std::string::npos);
	EXPECT_NE(Helper.find("DefaultTextColor().WithMultipliedAlpha(IconAlpha)"), std::string::npos);
	EXPECT_NE(Helper.find("ColorRGBA(1.0f, 0.0f, 0.0f, IconAlpha)"), std::string::npos);
	EXPECT_NE(Helper.find("FontIcons::FONT_ICON_SLASH"), std::string::npos);
	// 计分板的三个 SMTC 播放控制按钮已按远程结果删除，助手只服务影子回放控制条。
	EXPECT_EQ(Source.find("s_SmtcPrevButton"), std::string::npos);
	EXPECT_EQ(Source.find("s_SmtcPlayButton"), std::string::npos);
	EXPECT_EQ(Source.find("s_SmtcNextButton"), std::string::npos);
	EXPECT_NE(Source.find("DoScoreboardMediaIconButton(Ui(), TextRender(), &s_GhostPlayButton"), std::string::npos);
	EXPECT_NE(Source.find("DoScoreboardMediaIconButton(Ui(), TextRender(), &s_GhostCloseButton"), std::string::npos);
	EXPECT_EQ(Source.find("Ui()->DoButton_FontIcon(&s_SmtcPrevButton"), std::string::npos);
	EXPECT_EQ(Source.find("Ui()->DoButton_FontIcon(&s_SmtcPlayButton"), std::string::npos);
	EXPECT_EQ(Source.find("Ui()->DoButton_FontIcon(&s_SmtcNextButton"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ScoreboardUsesOneRowPlanAndDenseTeeLod)
{
	const std::string Source = ReadTextFile("src/game/client/components/scoreboard.cpp");
	const std::string OnRender = FunctionBody(Source, "void CScoreboard::OnRender()");
	const std::string RenderScoreboard = FunctionBody(Source, "void CScoreboard::RenderScoreboard(");

	EXPECT_NE(OnRender.find("BuildPlayerRowPlan"), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("TEE_PREVIEW_LAYER_BODY"), std::string::npos);
	EXPECT_EQ(RenderScoreboard.find("for(int j ="), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("const bool HasWar ="), std::string::npos);

	// The rendering optimization must not alter point lookup or display behavior.
	EXPECT_NE(OnRender.find("m_PlayerPoints.EnsureQueried"), std::string::npos);
	EXPECT_NE(RenderScoreboard.find("m_PlayerPoints.GetPoints"), std::string::npos);
}

TEST(QmNewUiMenuBranches, GaussianBlurCoversRequestedHudAndVoteBackgroundsOnly)
{
	const std::string HudSource = ReadTextFile("src/game/client/components/hud.cpp");
	const std::string VotingSource = ReadTextFile("src/game/client/components/voting.cpp");
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/tclient.cpp");
	const std::string MovementInfo = FunctionBody(HudSource, "void CHud::RenderMovementInformation()");
	const std::string KeyStatus = FunctionBody(HudSource, "void CHud::RenderKeyStatus()");
	const std::string ScoreHud = FunctionBody(HudSource, "void CHud::RenderScoreHud()");
	const std::string Vote = FunctionBody(VotingSource, "void CVoting::Render()");
	const std::string MiniVote = FunctionBody(TClientSource, "void CTClient::RenderMiniVoteHud(");

	EXPECT_NE(MovementInfo.find("RenderGaussianBlur"), std::string::npos);
	EXPECT_NE(KeyStatus.find("RenderGaussianBlur"), std::string::npos);
	EXPECT_NE(ScoreHud.find("RenderGaussianBlur"), std::string::npos);
	EXPECT_NE(Vote.find("RenderGaussianBlur"), std::string::npos);
	EXPECT_NE(MiniVote.find("RenderGaussianBlur"), std::string::npos);
	EXPECT_NE(Vote.find("View.Draw(ui_token::color::SURFACE_GLASS"), std::string::npos);
	EXPECT_NE(MiniVote.find("View.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, IngameMenuPrimaryActionLabelsUseEnglishKeys)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_ingame.cpp");

	EXPECT_NE(Source.find("pDisconnectButtonLabel = Localize(\"Disconnect\")"), std::string::npos);
	EXPECT_NE(Source.find("pDummyButtonLabel = Localize(\"Connect dummy\")"), std::string::npos);
	EXPECT_NE(Source.find("pDummyButtonLabel = Localize(\"Connecting dummy\")"), std::string::npos);
	EXPECT_NE(Source.find("pDummyButtonLabel = Localize(\"Disconnect dummy\")"), std::string::npos);
	EXPECT_NE(Source.find("pEditHudButtonLabel = Localize(\"Edit HUD\")"), std::string::npos);
	EXPECT_NE(Source.find("pDemoButtonLabel = Recording ? Localize(\"Stop record\") : Localize(\"Record demo\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Save last %d min\")"), std::string::npos);
	EXPECT_NE(Source.find("pDemoMarkerButtonLabel = Localize(\"Mark demo\")"), std::string::npos);
	EXPECT_NE(Source.find("pJoinRedButtonLabel = Localize(\"Join red\")"), std::string::npos);
	EXPECT_NE(Source.find("pJoinBlueButtonLabel = Localize(\"Join blue\")"), std::string::npos);
	EXPECT_NE(Source.find("pJoinGameButtonLabel = Localize(\"Join game\")"), std::string::npos);
	EXPECT_NE(Source.find("pKillButtonLabel = Localize(\"Kill\")"), std::string::npos);
	EXPECT_NE(Source.find("pPauseButtonLabel = (!Paused && !Spec) ? Localize(\"Pause\") : Localize(\"Join game\")"), std::string::npos);
	EXPECT_NE(Source.find("pFastPracticeLabel = FastPracticeEnabled ? Localize(\"Stop practice\") : Localize(\"Fast practice\")"), std::string::npos);
	EXPECT_NE(Source.find("DoToolTip(&s_DummyButton, &Button, Localize(\"Please wait…\"))"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DummyAndSpectateBindLabelsUseEnglishKeys)
{
	const std::string ControlsSource = ReadTextFile("src/game/client/components/menus_settings_controls.cpp");
	const std::string TouchSource = ReadTextFile("src/game/client/components/touch_controls.cpp");

	EXPECT_NE(ControlsSource.find("Localizable(\"Toggle dummy\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Dummy jump\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Dummy fire\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Dummy hook\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Dummy copy\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Dummy hammer fly\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Dummy Control\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Spectate mode\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Spectate teleport\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Spectate next\")"), std::string::npos);
	EXPECT_NE(ControlsSource.find("Localizable(\"Spectate previous\")"), std::string::npos);
	EXPECT_NE(TouchSource.find("Localizable(\"Toggle dummy\")"), std::string::npos);
	EXPECT_NE(TouchSource.find("Localizable(\"Spectate mode\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ConsoleChatExportLabelsUseEnglishKeys)
{
	const std::string Source = ReadTextFile("src/game/client/components/console.cpp");

	EXPECT_NE(Source.find("Localize(\"QmClient chat log\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Total\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Messages\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"No chat log selected\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Chat export failed\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Exported %d chat messages\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Selected %d\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Cancel\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Export selected\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Clear\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Select all chat\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Select export\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, HudDummyStatusLabelsUseEnglishKeys)
{
	const std::string Source = ReadTextFile("src/game/client/components/hud.cpp");

	EXPECT_NE(Source.find("Localize(\"Dummy mini view\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Connect dummy to enable\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Key Sticking: ?\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Key Sticking: On\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Key Sticking: Off\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Key Sticking: Reset Self\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Hammer: %s\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Dummy Control: %s\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Dummy copy: %s\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TranslationAndDemoUiLabelsUseEnglishKeys)
{
	const std::string ChatSource = ReadTextFile("src/game/client/components/chat.cpp");
	const std::string DemoSource = ReadTextFile("src/game/client/components/menus_demo.cpp");
	const std::string BrowserSource = ReadTextFile("src/game/client/components/menus_browser.cpp");

	EXPECT_NE(ChatSource.find("Localize(\"Translation Settings\")"), std::string::npos);
	EXPECT_NE(ChatSource.find("Localize(\"Auto-translate incoming messages\")"), std::string::npos);
	EXPECT_NE(ChatSource.find("Localize(\"Auto-translate outgoing messages\")"), std::string::npos);
	EXPECT_NE(ChatSource.find("Localize(\"Incoming language\")"), std::string::npos);
	EXPECT_NE(ChatSource.find("Localize(\"Outgoing language\")"), std::string::npos);
	EXPECT_NE(ChatSource.find("Localize(\"Translation service\")"), std::string::npos);
	EXPECT_NE(DemoSource.find("Localize(\"Could not preview this image\")"), std::string::npos);
	EXPECT_NE(DemoSource.find("BrowsingScreenshots ? Localize(\"Open the folder containing screenshots\") : Localize(\"Open the folder containing demo files\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"Map\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"Category\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"Difficulty stars\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"Note\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"Has save\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"None\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ProtectedFriendCategoriesCannotBeRenamedOrDeleted)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const size_t ProtectedFn = Source.find("static bool IsProtectedFriendsCategory");
	ASSERT_NE(ProtectedFn, std::string::npos);
	const size_t ProtectedFnEnd = Source.find("static const char *LocalizeFriendsCategory", ProtectedFn);
	ASSERT_NE(ProtectedFnEnd, std::string::npos);
	const std::string ProtectedBody = Source.substr(ProtectedFn, ProtectedFnEnd - ProtectedFn);

	EXPECT_NE(ProtectedBody.find("IFriends::DEFAULT_CATEGORY"), std::string::npos);
	EXPECT_NE(ProtectedBody.find("IsClanMembersCategory(pCategory)"), std::string::npos);
	EXPECT_NE(ProtectedBody.find("IsOfflineFriendsCategory(pCategory)"), std::string::npos);

	const size_t Popup = Source.find("CUi::EPopupMenuFunctionResult CMenus::PopupFriendsCategory");
	ASSERT_NE(Popup, std::string::npos);
	const std::string PopupBody = Source.substr(Popup);
	EXPECT_NE(PopupBody.find("const bool IsProtectedCategory = IsProtectedFriendsCategory(pCategory);"), std::string::npos);
	EXPECT_NE(PopupBody.find("Localize(\"Rename\"), &Button, FontSize, TEXTALIGN_MC, 0.0f, false, !IsProtectedCategory"), std::string::npos);
	EXPECT_NE(PopupBody.find("Localize(\"Delete category\"), &Button, FontSize, TEXTALIGN_MC, 0.0f, false, !IsProtectedCategory"), std::string::npos);
}

TEST(QmNewUiMenuBranches, FriendAddPopupExposesCreateCategoryAction)
{
	const std::string Header = ReadTextFile("src/game/client/components/menus.h");
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");

	EXPECT_NE(Header.find("m_FriendsAddCategoryCreateButton"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Create category\")"), std::string::npos);
	EXPECT_NE(Source.find("m_FriendsCategoryPopupContext.m_Mode = CFriendsCategoryPopupContext::MODE_ADD"), std::string::npos);
	EXPECT_NE(Source.find("Ui()->DoPopupMenu(&m_FriendsCategoryPopupContext"), std::string::npos);
}

TEST(QmNewUiMenuBranches, FriendAutoFollowDistinguishesManualAndAutomaticConnects)
{
	const std::string Header = ReadTextFile("src/game/client/components/menus.h");
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string QmMenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	EXPECT_NE(Header.find("enum class EConnectIntent"), std::string::npos);
	EXPECT_NE(Header.find("void Connect(const char *pAddress, EConnectIntent Intent = EConnectIntent::Manual)"), std::string::npos);
	EXPECT_NE(Source.find("if(Intent == EConnectIntent::Manual)"), std::string::npos);
	EXPECT_NE(Source.find("StopFriendAutoFollow(m_FriendAutoFollowState);"), std::string::npos);
	EXPECT_NE(Source.find("Connect(g_Config.m_UiServerAddress, EConnectIntent::AutoFollow)"), std::string::npos);
	const std::string FriendNotifyBody = FunctionBody(QmMenusSource, "void CMenus::RenderQmFunctionFriendNotifyContent(");
	ASSERT_FALSE(FriendNotifyBody.empty());
	EXPECT_NE(FriendNotifyBody.find("RenderValue(\"qmclient-friend-auto-follow-delay\", \"Auto-follow delay\""), std::string::npos);
	EXPECT_NE(FriendNotifyBody.find("&g_Config.m_QmFriendAutoFollowDelay, 0, 30, \"s\""), std::string::npos);
}

TEST(QmNewUiMenuBranches, ShortServerNamesKeepDisplayNameHighlightPath)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");

	EXPECT_NE(Source.find("g_Config.m_QmShortServerNames || (pItem->m_QuickSearchHit & IServerBrowser::QUICK_SERVERNAME)"), std::string::npos);
	EXPECT_NE(Source.find("PrintHighlighted(pDisplayServerName"), std::string::npos);
	EXPECT_EQ(Source.find("!g_Config.m_QmShortServerNames && g_Config.m_BrFilterString"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ClientSourceDoesNotUseChineseLocalizeKeys)
{
	const std::string HudEditorSource = ReadTextFile("src/game/client/components/hud_editor.cpp");
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string BrowserSource = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string DemoSource = ReadTextFile("src/game/client/components/menus_demo.cpp");
	const std::string IngameTouchSource = ReadTextFile("src/game/client/components/menus_ingame_touch_controls.cpp");
	const std::string IngameSource = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string SettingsControlsSource = ReadTextFile("src/game/client/components/menus_settings_controls.cpp");
	const std::string Settings7Source = ReadTextFile("src/game/client/components/menus_settings7.cpp");
	const std::string StartSource = ReadTextFile("src/game/client/components/menus_start.cpp");
	const std::string PieMenuSource = ReadTextFile("src/game/client/components/pie_menu.cpp");
	const std::string ScoreboardSource = ReadTextFile("src/game/client/components/scoreboard.cpp");

	EXPECT_NE(HudEditorSource.find("Localize(\"Position jump tip\")"), std::string::npos);
	EXPECT_NE(MenusSource.find("m_apSettingsTabs[SETTINGS_SOUND] = Localize(\"Sound\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"DDmaX Easy\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"Favorite map\")"), std::string::npos);
	EXPECT_NE(DemoSource.find("Localize(\"Screenshots directory\")"), std::string::npos);
	EXPECT_NE(IngameTouchSource.find("Localize(\"Allow dummy\", \"Touch button visibilities\")"), std::string::npos);
	EXPECT_NE(IngameTouchSource.find("Localize(\"Dummy connected\", \"Touch button visibilities\")"), std::string::npos);
	EXPECT_NE(IngameTouchSource.find("Localize(\"Spectate\", \"Predefined touch button behaviors\")"), std::string::npos);
	EXPECT_NE(IngameSource.find("Localize(\"Spectate\")"), std::string::npos);
	EXPECT_NE(IngameSource.find("Localize(\"Dummies are not allowed on this server\")"), std::string::npos);
	EXPECT_NE(SettingsSource.find("Localize(\"Show spectator cursor\")"), std::string::npos);
	EXPECT_NE(SettingsSource.find("Localize(\"Auto save chat log\")"), std::string::npos);
	EXPECT_NE(Settings7Source.find("Localize(\"Dummy\")"), std::string::npos);
	EXPECT_NE(Settings7Source.find("Localize(\"Dummy\")"), std::string::npos);
	EXPECT_NE(StartSource.find("Localize(\"(Update required)\")"), std::string::npos);
	EXPECT_NE(PieMenuSource.find("Localize(\"Spectate\")"), std::string::npos);
	EXPECT_NE(ScoreboardSource.find("Localize(\"Spectators\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, PieMenuSeparatesSelfRenameFromOtherPlayerActions)
{
	const std::string Source = ReadTextFile("src/game/client/components/pie_menu.cpp");
	const std::string FindNearestPlayer = FunctionBody(Source, "int CPieMenu::FindNearestPlayer()");
	const std::string OpenMenu = FunctionBody(Source, "void CPieMenu::OpenMenu()");
	const std::string OnInput = FunctionBody(Source, "bool CPieMenu::OnInput(");
	const std::string UpdateSelection = FunctionBody(Source, "void CPieMenu::UpdateSelection()");
	const std::string OnRender = FunctionBody(Source, "void CPieMenu::OnRender()");
	const std::string RenderCenterInfo = FunctionBody(Source, "void CPieMenu::RenderCenterInfo()");
	const std::string ExecuteRenameOption = FunctionBody(Source, "void CPieMenu::ExecuteRenameOption(");

	// Both local connections belong to the user and must never become inner-ring targets.
	EXPECT_NE(FindNearestPlayer.find("GameClient()->IsLocalClientId(i)"), std::string::npos);

	// A connected local identity and at least one usable ring are required to open the menu.
	EXPECT_NE(OpenMenu.find("Client()->State() != IClient::STATE_ONLINE"), std::string::npos);
	EXPECT_NE(OpenMenu.find("LocalClientId < 0 || LocalClientId >= MAX_CLIENTS"), std::string::npos);
	EXPECT_NE(OpenMenu.find("if(TargetId < 0 && m_vRenameQueue.empty())"), std::string::npos);

	// Without another player the hidden inner ring cannot be selected or triggered by number keys.
	EXPECT_NE(OnInput.find("if(!HasTargetPlayer())"), std::string::npos);
	EXPECT_NE(UpdateSelection.find("if(HasTargetPlayer() && MouseDistance <= OuterRadius)"), std::string::npos);
	EXPECT_NE(OnRender.find("if(HasTargetPlayer())"), std::string::npos);

	// Targetless mode displays self, and hovering the outer ring identifies rename as a self action.
	EXPECT_NE(RenderCenterInfo.find("const int DisplayClientId = HasTargetPlayer() ? m_TargetClientId : LocalClientId;"), std::string::npos);
	EXPECT_NE(RenderCenterInfo.find("Localize(\"Self\")"), std::string::npos);
	EXPECT_EQ(ExecuteRenameOption.find("m_TargetClientId"), std::string::npos);
}

TEST(QmNewUiMenuBranches, QmClientAxiomAutoLoginLivesInQmClientComponent)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/axiom_auto_login.cpp");
	const std::string Header = ReadTextFile("src/game/client/components/qmclient/axiom_auto_login.h");
	const std::string TClientHeader = ReadTextFile("src/game/client/components/tclient/tclient.h");
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/tclient.cpp");
	const std::string GameClientHeader = ReadTextFile("src/game/client/gameclient.h");
	const std::string QmConfigHeader = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string QmMenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string IngameMenusSource = ReadTextFile("src/game/client/components/menus_ingame.cpp");

	EXPECT_NE(Header.find("class CQmAxiomAutoLogin : public CComponent"), std::string::npos);
	EXPECT_NE(GameClientHeader.find("CQmAxiomAutoLogin m_QmAxiomAutoLogin;"), std::string::npos);
	EXPECT_NE(Source.find("void CQmAxiomAutoLogin::TrySendLogin()"), std::string::npos);
	EXPECT_NE(Source.find("void CQmAxiomAutoLogin::TrySendDummyLogin()"), std::string::npos);
	EXPECT_NE(Source.find("SendChatOnConn(IClient::CONN_DUMMY"), std::string::npos);
	EXPECT_NE(Header.find("TrySendDummyLogin"), std::string::npos);
	EXPECT_NE(QmConfigHeader.find("QmAxiomDummyLoginPassword"), std::string::npos);
	EXPECT_NE(QmConfigHeader.find("qm_axiom_dummy_login_password"), std::string::npos);
	EXPECT_NE(QmMenusSource.find("Axiom dummy password"), std::string::npos);
	EXPECT_NE(Source.find("m_DummyLoginAllowedThisServer"), std::string::npos);
	EXPECT_NE(Source.find("m_DummyWasConnected"), std::string::npos);
	EXPECT_EQ(Source.find("if(DummyConnected && !m_DummyWasConnected)"), std::string::npos);
	EXPECT_NE(Source.find("m_DummyLoginAllowedThisServer = true;"), std::string::npos);
	EXPECT_NE(Source.find("if(!m_DummyLoginAllowedThisServer)"), std::string::npos);
	EXPECT_NE(Header.find("EnableDummyReconnectForServer"), std::string::npos);
	EXPECT_NE(Header.find("DisableDummyReconnectForServer"), std::string::npos);
	EXPECT_NE(Source.find("Client()->DummyConnect();"), std::string::npos);
	EXPECT_NE(IngameMenusSource.find("GameClient()->m_QmAxiomAutoLogin.EnableDummyReconnectForServer();"), std::string::npos);
	EXPECT_NE(IngameMenusSource.find("GameClient()->OnDummyManualDisconnect();"), std::string::npos);
	EXPECT_NE(GameClientHeader.find("void OnDummyManualDisconnect() override;"), std::string::npos);
	const std::string GameClientSource = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string ManualDisconnectBody = FunctionBody(GameClientSource, "void CGameClient::OnDummyManualDisconnect()");
	ASSERT_FALSE(ManualDisconnectBody.empty());
	EXPECT_NE(ManualDisconnectBody.find("m_QmAxiomAutoLogin.DisableDummyReconnectForServer();"), std::string::npos);
	const std::string ClientSource = ReadTextFile("src/engine/client/client.cpp");
	const std::string DummyDisconnectBody = FunctionBody(ClientSource, "void CClient::Con_DummyDisconnect(");
	ASSERT_FALSE(DummyDisconnectBody.empty());
	EXPECT_NE(DummyDisconnectBody.find("GameClient()->OnDummyManualDisconnect();"), std::string::npos);
	EXPECT_NE(DummyDisconnectBody.find("DummyDisconnect(nullptr);"), std::string::npos);
	EXPECT_NE(Source.find("bool CQmAxiomAutoLogin::IsAxiomCommunity() const"), std::string::npos);
	EXPECT_NE(Header.find("QMCLIENT_AXIOM_AUTO_LOGIN_SLOW_RETRY_SECONDS"), std::string::npos);
	EXPECT_NE(Header.find("SQmAxiomAutoLoginState m_AutoLoginState;"), std::string::npos);
	EXPECT_NE(Header.find("m_SlowRetryMode"), std::string::npos);
	EXPECT_NE(Header.find("m_HardFailed"), std::string::npos);
	EXPECT_NE(Header.find("QmScheduleAxiomAutoLoginRetry"), std::string::npos);
	EXPECT_NE(Source.find("QmClassifyAxiomLoginReply(pText)"), std::string::npos);
	EXPECT_NE(Source.find("QmApplyAxiomLoginReply"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Trying Axiom auto login\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Trying Axiom dummy auto login\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Axiom auto login succeeded\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Axiom auto login failed, retrying\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Axiom auto login failed\")"), std::string::npos);

	EXPECT_EQ(TClientHeader.find("IsAxiomCommunity() const"), std::string::npos);
	EXPECT_EQ(TClientHeader.find("ResetAxiomAutoLoginState"), std::string::npos);
	EXPECT_EQ(TClientHeader.find("UpdateAxiomAutoLogin"), std::string::npos);
	EXPECT_EQ(TClientHeader.find("HandleAxiomAutoLoginMessage"), std::string::npos);
	EXPECT_EQ(TClientSource.find("TrySendAxiomLogin"), std::string::npos);
	EXPECT_EQ(TClientSource.find("HandleAxiomAutoLoginMessage"), std::string::npos);
}

TEST(QmNewUiMenuBranches, FastPracticeSurfacesPracticeStateInHud)
{
	const std::string HudSource = ReadTextFile("src/game/client/components/hud.cpp");
	const std::string HudHeader = ReadTextFile("src/game/client/components/hud.h");

	const std::string PlayerStateBody = FunctionBody(HudSource, "void CHud::RenderPlayerState(");
	ASSERT_FALSE(PlayerStateBody.empty());
	EXPECT_NE(PlayerStateBody.find("const bool FastPracticeParticipant = GameClient()->m_FastPractice.IsPracticeParticipant(ClientId);"), std::string::npos);
	EXPECT_NE(PlayerStateBody.find("|| FastPracticeParticipant"), std::string::npos);
	EXPECT_EQ(PlayerStateBody.find("m_FastPractice.Enabled()"), std::string::npos);
	EXPECT_NE(PlayerStateBody.find("m_PracticeModeOffset"), std::string::npos);

	const std::string MovementBody = FunctionBody(HudSource, "void CHud::RenderMovementInformation()");
	ASSERT_FALSE(MovementBody.empty());
	EXPECT_NE(MovementBody.find("const bool FastPracticeParticipant = GameClient()->m_FastPractice.IsPracticeParticipant(ClientId);"), std::string::npos);
	EXPECT_NE(MovementBody.find("const bool ShowSpeed = !PosOnly && (g_Config.m_ClShowhudPlayerSpeed || FastPracticeParticipant);"), std::string::npos);
	EXPECT_EQ(MovementBody.find("m_FastPractice.Enabled()"), std::string::npos);
	EXPECT_NE(HudHeader.find("void RenderMovementInformation();"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NameplatePreviewRebuildsTextContainerInsteadOfAppendingSizes)
{
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string Body = BlockBodyAfter(Source, "void Update(CGameClient &This, const CNamePlateData &Data) override");
	ASSERT_FALSE(Body.empty());

	const size_t DeletePos = Body.find("This.TextRender()->DeleteTextContainer(m_TextContainerIndex);");
	const size_t UpdateTextPos = Body.find("UpdateText(This, Data);");
	ASSERT_NE(DeletePos, std::string::npos);
	ASSERT_NE(UpdateTextPos, std::string::npos);
	EXPECT_LT(DeletePos, UpdateTextPos);
	EXPECT_EQ(Body.find("else\n\t\t{\n\t\t\tUpdateText(This, Data);\n\t\t}"), std::string::npos);
	EXPECT_NE(Body.find("QmNameplateTextEffectPadding"), std::string::npos);
}

TEST(QmNewUiMenuBranches, QmLaserSettingsMovedToAppearanceLaserTab)
{
	const std::string QmSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string VisualDeck = FunctionBody(QmSource, "void CMenus::RenderSettingsQmClientVisualDeck(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(VisualDeck.empty());
	EXPECT_EQ(VisualDeck.find("qm:laser"), std::string::npos);
	EXPECT_EQ(VisualDeck.find("qm:nameplate_text"), std::string::npos);
	// 薄壳只声明清单，卡片本体在卡片目录：目录清单与视觉卡片模块都不得收录这两个设置。
	const std::string VisualCatalog = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogVisual.cpp");
	const std::string CardCatalog = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalog.cpp");
	ASSERT_FALSE(VisualCatalog.empty());
	ASSERT_FALSE(CardCatalog.empty());
	EXPECT_EQ(VisualCatalog.find("qm:laser"), std::string::npos);
	EXPECT_EQ(VisualCatalog.find("qm:nameplate_text"), std::string::npos);
	EXPECT_EQ(CardCatalog.find("qm:laser"), std::string::npos);
	EXPECT_EQ(CardCatalog.find("qm:nameplate_text"), std::string::npos);

	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string LaserBranch = BlockBodyAfter(SettingsSource, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_LASER)");
	ASSERT_FALSE(LaserBranch.empty());
	EXPECT_NE(LaserBranch.find("AddCard(10, ResolveLaserEnhancedMinCardHeight()"), std::string::npos);
	EXPECT_NE(LaserBranch.find("AddCard(11, LaserColorMinCardHeight"), std::string::npos);
	EXPECT_NE(LaserBranch.find("AddCard(12, LaserPreviewMinCardHeight"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("RenderQmSettingsGlassCard(EnhancedCard, QmCardStyle);"), std::string::npos);
	EXPECT_NE(SettingsSource.find("QmResolveScrollPolicy(ScrollRequest, AppearanceUiScale"), std::string::npos);
	EXPECT_NE(SettingsSource.find("std::array<CScrollRegion, NUMBER_OF_APPEARANCE_TABS> s_AppearanceSettingsCardScrollRegions"), std::string::npos);
	EXPECT_NE(SettingsSource.find("CQmScrollState &ScrollState = s_AppearanceSettingsCardScrollRegions[m_AppearanceSettingsTab].State()"), std::string::npos);
	EXPECT_NE(SettingsSource.find("SettingsCardDeckForRenderPass().RenderCached(AppearanceCardCtx, AppearancePage, pAppearanceDeckTab"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("const float EnhancedContentHeight ="), std::string::npos);
	EXPECT_EQ(LaserBranch.find("const float ColorContentHeight ="), std::string::npos);
	EXPECT_EQ(LaserBranch.find("const float PreviewContentHeight ="), std::string::npos);
	EXPECT_EQ(LaserBranch.find("s_LaserMeasuredEnhancedCardHeight"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("s_LaserMeasuredColorCardHeight"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("s_LaserMeasuredPreviewCardHeight"), std::string::npos);
	EXPECT_NE(LaserBranch.find("const auto ResolveLaserEnhancedMinCardHeight"), std::string::npos);
	EXPECT_NE(LaserBranch.find("ResolveAppearanceLaserEnhancedHeight(AppearanceMetrics, g_Config.m_QmLaserEnhanced != 0)"), std::string::npos);
	EXPECT_NE(LaserBranch.find("vCards.back().m_PreLayoutInput"), std::string::npos);
	EXPECT_EQ(LaserBranch.find("appearance-laser-enhancement-title"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_LASER, &g_Config.m_QmLaserEnhanced"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserGlowIntensity"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserSize"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserAlpha"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserRoundCaps"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserPulseSpeed"), std::string::npos);
	EXPECT_NE(LaserBranch.find("g_Config.m_QmLaserPulseAmplitude"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoLaserPreview(&LaserPreviewRect, LaserRifleOutlineColor, LaserRifleInnerColor, LASERTYPE_RIFLE);"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoLaserPreview(&LaserPreviewRect, LaserShotgunOutlineColor, LaserShotgunInnerColor, LASERTYPE_SHOTGUN);"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoLaserPreview(&LaserPreviewRect, LaserDoorOutlineColor, LaserDoorInnerColor, LASERTYPE_DOOR);"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoLaserPreview(&LaserPreviewRect, LaserFreezeOutlineColor, LaserFreezeInnerColor, LASERTYPE_FREEZE);"), std::string::npos);
	EXPECT_NE(LaserBranch.find("DoLaserPreview(&LaserPreviewRect, LaserDraggerOutlineColor, LaserDraggerInnerColor, LASERTYPE_DRAGGER);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsCardUsesOneCanonicalSurfaceWithoutLegacyGlass)
{
	const std::string MenuSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Body = ReadTextFile("src/game/client/QmUi/SettingsCard.cpp");
	ASSERT_FALSE(Body.empty());
	EXPECT_EQ(Body.find("Shadow.Draw"), std::string::npos);
	EXPECT_NE(Body.find("DrawRoundedSurface(Ctx, ChromeRect, Surface, Border, CardRadius"), std::string::npos);
	EXPECT_EQ(Body.find("ChromeRect.Draw(Surface, IGraphics::CORNER_ALL, CardRadius)"), std::string::npos);
	EXPECT_EQ(Body.find("ChromeRect.Draw(Border, IGraphics::CORNER_ALL, CardRadius)"), std::string::npos);
	EXPECT_EQ(Body.find("ResolveSettingsCardBorderRingClipRects"), std::string::npos);
	EXPECT_EQ(Body.find("InnerSurface.Margin(BorderWidth, &InnerSurface);"), std::string::npos);
	EXPECT_EQ(Body.find("BorderRect.Draw(Border, IGraphics::CORNER_ALL, CardRadius)"), std::string::npos);
	EXPECT_EQ(Body.find("DrawOutline(Border)"), std::string::npos);
	EXPECT_EQ(MenuSource.find("RenderQmSettingsGlassCard"), std::string::npos);
	EXPECT_EQ(MenuSource.find("m_QmCardBackdropBlur"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsNumericFieldRectanglesInitializeBeforeLayoutBranching)
{
	const std::string Source = ReadTextFile("src/game/client/QmUi/UiForms.cpp");
	const std::string Body = FunctionBody(Source, "bool NumericField(const IUiContext &Ctx");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("CUIRect Label{}, Controls{}, ValueRect{}, ScrollBar{}, InputField{};"), std::string::npos);
	EXPECT_NE(Body.find("Controls.VSplitRight(ValueWidth, &ScrollBar, &InputField);"), std::string::npos);
	EXPECT_NE(Body.find("FieldOptions.m_TextAlign = TEXTALIGN_MC;"), std::string::npos);
	EXPECT_NE(Body.find("FieldOptions.m_pTrailingText = HasSuffix ? Options.m_pSuffix : nullptr;"), std::string::npos);
	EXPECT_NE(Body.find("FieldOptions.m_InlineTrailingText = HasSuffix;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NumericInputKeepsValueAndUnitInOneGeometry)
{
	const std::string Forms = ReadTextFile("src/game/client/QmUi/UiForms.cpp");
	const std::string Header = ReadTextFile("src/game/client/QmUi/UiForms.h");
	EXPECT_NE(Header.find("struct SInlineTrailingTextLayout"), std::string::npos);
	EXPECT_NE(Header.find("ResolveInlineTrailingTextLayout("), std::string::npos);
	EXPECT_NE(Forms.find("const SInlineTrailingTextLayout InlineLayout = ResolveInlineTrailingTextLayout"), std::string::npos);
	EXPECT_NE(Forms.find("FieldOptions.m_TextAlign = TEXTALIGN_MC;"), std::string::npos);
	EXPECT_NE(Forms.find("FieldOptions.m_InlineTrailingText = HasSuffix;"), std::string::npos);
	EXPECT_EQ(Forms.find("m_pInactiveDisplayText"), std::string::npos);
}

TEST(QmNewUiMenuBranches, EditBoxesActivateFromTheirConfiguredHitRect)
{
	const std::string Source = ReadTextFile("src/game/client/ui.cpp");
	const std::string Body = FunctionBody(Source, "bool CUi::DoEditBox(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, int Corners, const std::vector<STextColorSplit> &vColorSplits, int Align, const SEditBoxRenderOptions &RenderOptions)");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("else if(Inside)"), std::string::npos);
	EXPECT_EQ(Body.find("else if(HotItem() == pLineInput)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, GraphicsFsaaSelectionDefersBackendReconfigure)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("static constexpr int s_aFsaaSamples[] = {0, 2, 4, 8, 16, 32, 64};"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_GfxFsaaSamples = s_aFsaaSamples[NewValue];"), std::string::npos);
	EXPECT_NE(Body.find("CheckSettings = true;"), std::string::npos);
	EXPECT_EQ(Body.find("Graphics()->SetMultiSampling"), std::string::npos);
	EXPECT_NE(Body.find("m_NeedRestartGraphics = !(s_GfxFsaaSamples == g_Config.m_GfxFsaaSamples"), std::string::npos);
}

TEST(QmNewUiMenuBranches, AnimationControlsExposeIndependentScopes)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("Localize(\"UI motion level\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Card list entry animation\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Card height animation\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Card reflow animation\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Presentation animations\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Animate chat box, emote selector, scoreboard, and spectate selection\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Off: disables all interface animations while preserving the options below\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Reduced: uses shorter transitions and disables presentation animations\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Full: each enabled animation category uses its complete transition\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Text input focus ring color\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, MultilineInputFieldsReleaseFocusOutsideAndCenterSingleLineContent)
{
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string QmClientSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string MultiLineBody = FunctionBody(UiSource, "bool CUi::DoEditBoxMultiLine(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, float LineSpacing, int TextAlign, const SEditBoxRenderOptions &RenderOptions)");
	ASSERT_FALSE(MultiLineBody.empty());

	EXPECT_NE(MultiLineBody.find("const bool ClickedOutside = (MouseButtonClicked(0) || MouseButtonClicked(1)) && !Inside;"), std::string::npos);
	EXPECT_NE(MultiLineBody.find("if(Active && ClickedOutside)"), std::string::npos);
	EXPECT_NE(MultiLineBody.find("ReleaseActiveTextInput(pLineInput);"), std::string::npos);
	EXPECT_NE(QmClientSource.find("InputOptions.m_TextAlign = TEXTALIGN_ML;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientScaledInputsUseSettingsThemeAndPreciseFreezeLabels)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string SliderBody = FunctionBody(Source, "bool CMenus::DoSliderWithScaledValue(");
	ASSERT_FALSE(SliderBody.empty());

	EXPECT_NE(SliderBody.find("IUiContext InputCtx = SettingsUiContext(\"tclient_slider_input\""), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Base prediction margin\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Maximum reduction\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Delay before reduction\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Frozen prediction margin\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SharedListEntryRevealUsesElapsedGapWithoutMovingScrollGeometry)
{
	const std::string ListBoxSource = ReadTextFile("src/game/client/ui_listbox.cpp");
	const std::string ListBoxHeader = ReadTextFile("src/game/client/ui_listbox.h");

	EXPECT_EQ(ListBoxSource.find("PerfFrame()"), std::string::npos);
	EXPECT_EQ(ListBoxSource.find("QmListBoxShouldRearmInitialScroll"), std::string::npos);
	EXPECT_EQ(ListBoxHeader.find("m_LastRenderFrame"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("m_InitialScrollPending = true;"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("m_EntryAnimationStartTime"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("QmListBoxEntryAnimatedRect"), std::string::npos);
	EXPECT_NE(ListBoxSource.find("QmListBoxShouldStartEntryAnimation"), std::string::npos);
	EXPECT_NE(ListBoxSource.find("QmListBoxEntryOffset"), std::string::npos);
	const std::string DoNextRowBody = FunctionBody(ListBoxSource, "CListboxItem CListBox::DoNextRow()");
	const size_t AddRectPos = DoNextRowBody.find("m_ScrollRegion.AddRect(m_RowView);");
	const size_t AnimateRectPos = DoNextRowBody.find("QmListBoxEntryAnimatedRect");
	ASSERT_NE(AddRectPos, std::string::npos);
	ASSERT_NE(AnimateRectPos, std::string::npos);
	EXPECT_LT(AddRectPos, AnimateRectPos);
	const std::string DoCustomRowBody = FunctionBody(ListBoxSource, "CListboxItem CListBox::DoCustomRow(float Height, bool ScrollHere)");
	const size_t CustomAddRectPos = DoCustomRowBody.find("m_ScrollRegion.AddRect(Item.m_Rect, ScrollHere);");
	const size_t CustomAnimateRectPos = DoCustomRowBody.find("QmListBoxEntryAnimatedRect");
	ASSERT_NE(CustomAddRectPos, std::string::npos);
	ASSERT_NE(CustomAnimateRectPos, std::string::npos);
	EXPECT_LT(CustomAddRectPos, CustomAnimateRectPos);
	EXPECT_NE(ListBoxSource.find("if(!RenderOnly && EntryAnimationEnabled"), std::string::npos);
}

TEST(QmNewUiMenuBranches, StatusBarOpacityUsesPercentAndAxiomUsesInputTrailingActions)
{
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string QmClientSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	EXPECT_NE(TClientSource.find("\"tclient-statusbar-alpha\""), std::string::npos);
	EXPECT_NE(TClientSource.find("\"tclient-statusbar-text-alpha\""), std::string::npos);
	EXPECT_NE(TClientSource.find("&CUi::ms_LinearScrollbarScale, 0, \"%\""), std::string::npos);
	EXPECT_NE(QmClientSource.find("Options.m_pTrailingActionId = &ToggleButton;"), std::string::npos);
	EXPECT_NE(QmClientSource.find("Options.m_pTrailingActionIcon = Visible ? FONT_ICON_EYE_SLASH : FONT_ICON_EYE;"), std::string::npos);
	EXPECT_NE(QmClientSource.find("Options.m_TrailingActionQmIcon = static_cast<int>(Visible ? EQmIcon::EYE_OFF : EQmIcon::EYE);"), std::string::npos);
	EXPECT_EQ(QmClientSource.find("PasswordToggleRect"), std::string::npos);
}

TEST(QmNewUiMenuBranches, NoThemeBackgroundAndFriendRowsUseSharedSdfSurfaces)
{
	const std::string Menus = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Browser = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string Background = FunctionBody(Menus, "void CMenus::RenderBackground()");
	ASSERT_FALSE(Background.empty());

	EXPECT_NE(Background.find("const bool NoMenuTheme = g_Config.m_ClMenuMap[0] == '\\0';"), std::string::npos);
	EXPECT_NE(Background.find("const ColorRGBA CheckerColor = NoMenuTheme"), std::string::npos);
	EXPECT_NE(Background.find("Graphics()->SetColor(CheckerColor);"), std::string::npos);
	EXPECT_NE(Browser.find("#include <game/client/QmUi/UiSurface.h>"), std::string::npos);
	EXPECT_NE(Browser.find("DrawRoundedSurface(Ui(), Header, HeaderColor, ColorRGBA(), 5.0f);"), std::string::npos);
	EXPECT_NE(Browser.find("DrawRoundedSurface(Ui(), Rect, Color, ColorRGBA(), 5.0f);"), std::string::npos);
	EXPECT_EQ(Browser.find("Rect.Draw(Color, IGraphics::CORNER_ALL, 5.0f);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, KeyReaderUsesOneOuterShellForValueAndDeleteAction)
{
	const std::string Source = ReadTextFile("src/game/client/components/key_binder.cpp");
	const std::string Body = FunctionBody(Source, "CKeyBinder::CKeyReaderResult CKeyBinder::DoKeyReader(");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiSurface.h>"), std::string::npos);
	EXPECT_NE(Body.find("DrawRoundedSurface(Ui(), *pRect, ReaderBaseColor"), std::string::npos);
	EXPECT_NE(Body.find("if(ClearChecked == 0)"), std::string::npos);
	EXPECT_NE(Body.find("const float ClearSurfaceAlpha = 0.22f * Ui()->ButtonColorMul(pClearButton);"), std::string::npos);
	EXPECT_NE(Body.find("DrawRoundedSurface(Ui(), ClearButton"), std::string::npos);
	EXPECT_NE(Body.find("ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f)"), std::string::npos);
	EXPECT_NE(Body.find("if(m_pKeyReaderId == pReaderButton && m_TakeKey)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SharedListsAndResourceCardsUseRoundedSurfacePath)
{
	const std::string ListBox = ReadTextFile("src/game/client/ui_listbox.cpp");
	const std::string Assets = ReadTextFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Settings = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Sound = FunctionBody(Settings, "void CMenus::RenderSettingsSound(CUIRect MainView)");
	const std::string Language = FunctionBody(Settings, "bool CMenus::RenderLanguageSelection(CUIRect MainView, const SSettingsContentMetrics *pMetrics)");
	const std::string Controls = ReadTextFile("src/game/client/components/menus_settings_controls.cpp");
	EXPECT_NE(ListBox.find("DrawRoundedSurface(Ui(), Item.m_Rect"), std::string::npos);
	EXPECT_NE(ListBox.find("DrawRoundedSurface(Ui(), View"), std::string::npos);
	EXPECT_NE(Assets.find("DrawRoundedSurface(Ui(), ShellRect"), std::string::npos);
	EXPECT_NE(Assets.find("DrawRoundedSurface(Ui(), PreviewFrame"), std::string::npos);
	EXPECT_NE(Assets.find("DrawRoundedSurface(Ui(), FallbackRect"), std::string::npos);
	EXPECT_NE(Assets.find("DrawRoundedSurface(pUi, StatusRect"), std::string::npos);
	EXPECT_NE(Assets.find("DrawRoundedSurface(Ui(), WorkshopHudView"), std::string::npos);
	EXPECT_NE(Sound.find("DrawRoundedSurface(Ui(), ListRow"), std::string::npos);
	EXPECT_NE(Sound.find("DrawRoundedSurface(Ui(), BadgeRect"), std::string::npos);
	EXPECT_NE(Language.find("DrawRoundedSurface(Ui(), ItemRect"), std::string::npos);
	EXPECT_NE(Controls.find("DrawRoundedSurface(Ui(), KeyReaders"), std::string::npos);
	EXPECT_NE(Controls.find("DrawRoundedSurface(Ui(), Row"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsCardDeckResetsStateWhenDefinitionViewChanges)
{
	const std::string SettingsDeck = ReadTextFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	ASSERT_FALSE(SettingsDeck.empty());
	EXPECT_NE(SettingsDeck.find("void CSettingsCardDeck::ResetDefinitionViewState()"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("if(TabChanged || StableIdsChanged || ModelCountChanged || StateIndexChanged)"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_PreparedDefinitionStateIndexRevision != Model.StateIndexRevision()"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_vContentHeights.clear();"), std::string::npos);
	EXPECT_NE(SettingsDeck.find("m_ProjectionCache = {};"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientSettingsCardsUseSharedQmCardStyle)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string HeaderSource = ReadTextFile("src/game/client/components/menus.h");
	const std::string RenderSettingsTClientSettings = FunctionBody(Source, "void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
	const std::string RenderSettingsTClientChatBinds = FunctionBody(Source, "void CMenus::RenderSettingsTClientChatBinds(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(RenderSettingsTClientSettings.empty());
	ASSERT_FALSE(RenderSettingsTClientChatBinds.empty());

	EXPECT_EQ(Source.find("RenderQmSettingsGlassCard(TClientCacheSectionBoxRect(BoxRect), QmSettingsCardStyle(1.0f));"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientSettings.find("m_SettingsCardDeck.RenderCached(SettingsUiContext(\"settings_tclient_main\""), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientChatBinds.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientChatBinds.find("BeginSettingsScrollRegion("), std::string::npos);
	EXPECT_EQ(Source.find("ScrollParams.m_ScrollUnit = 60.0f;"), std::string::npos);
	EXPECT_EQ(RenderSettingsTClientSettings.find("ScrollParams.m_ScrollbarMargin = 5.0f;"), std::string::npos);
	EXPECT_EQ(Source.find("BoxRect.Draw(Ui()->ScaleBackgroundAlpha(MenuPanelColor(0.92f))"), std::string::npos);
	EXPECT_EQ(HeaderSource.find("void DrawTClientCacheSectionBox(CUIRect BoxRect);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DeckPreLayoutPressClearsStaleActiveInput)
{
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const size_t PreLayoutPress = UiSource.find("if(PreLayoutInput() && Inside && !IsPopupOpen())");
	ASSERT_NE(PreLayoutPress, std::string::npos);
	const size_t PreLayoutPressEnd = UiSource.find("int CUi::DoDraggableButtonLogic", PreLayoutPress);
	ASSERT_NE(PreLayoutPressEnd, std::string::npos);
	const std::string ButtonLogic = UiSource.substr(PreLayoutPress, PreLayoutPressEnd - PreLayoutPress);
	EXPECT_NE(ButtonLogic.find("CLineInput::GetActiveInput()"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("pActiveInput->Deactivate()"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("m_pLastActiveItem = nullptr"), std::string::npos);
	EXPECT_NE(ButtonLogic.find("SetActiveItem(nullptr)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DDNetSettingsPageUsesSharedQmCards)
{
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettingsDDNet = FunctionBody(SettingsSource, "void CMenus::RenderSettingsDDNet(CUIRect MainView)");
	ASSERT_FALSE(RenderSettingsDDNet.empty());

	EXPECT_NE(RenderSettingsDDNet.find("const SSettingsPageLayoutFrame DDNetPage = SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const IUiContext DDNetCardCtx = SettingsUiContext(\"settings_ddnet\", UiScale);"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("static CScrollRegion s_DDNetSettingsCardScrollRegion;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("SettingsCardDeckForRenderPass().RenderCached(DDNetCardCtx, DDNetPage, \"ddnet\""), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(DemoSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const bool ReplaysLayout = g_Config.m_ClReplays != 0;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const bool RaceGhostLayout = g_Config.m_ClRaceGhost != 0;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const bool RaceSaveGhostLayout = g_Config.m_ClRaceSaveGhost != 0;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("ResolveDDNetDemoRows(ReplaysLayout, RaceGhostLayout, RaceSaveGhostLayout)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("vCards.back().m_Measure = [DDNetRowPitch]"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("ResolveDDNetDemoRows(g_Config.m_ClReplays != 0, g_Config.m_ClRaceGhost != 0, g_Config.m_ClRaceSaveGhost != 0)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("vCards.back().m_PreLayoutInput = ProcessDemoPreLayoutInput;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("DDNetLayoutRevision = DDNetLayoutRevision * 1099511628211ULL ^ static_cast<uint64_t>(g_Config.m_ClReplays != 0);"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("if(g_Config.m_ClRaceGhost)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("if(g_Config.m_ClRaceSaveGhost)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(GameplaySpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const bool TextEntitiesLayout = g_Config.m_ClTextEntities != 0;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("const bool AntiPingLayout = g_Config.m_ClAntiPing != 0;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("ResolveDDNetGameplayRows(TextEntitiesLayout, AntiPingLayout)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("vCards.back().m_PreLayoutInput = ProcessGameplayPreLayoutInput;"), std::string::npos);
	const std::string GameplayPreLayout = BlockBodyAfter(RenderSettingsDDNet, "const auto ProcessGameplayPreLayoutInput");
	ASSERT_FALSE(GameplayPreLayout.empty());
	EXPECT_NE(GameplayPreLayout.find("SplitDDNetRow(Gameplay, &GameplayRow);"), std::string::npos);
	EXPECT_NE(GameplayPreLayout.find("GameplayRow.VSplitLeft(std::clamp(GameplayRow.w * 0.38f"), std::string::npos);
	EXPECT_NE(GameplayPreLayout.find("Ui()->DoButtonLogic(&g_Config.m_ClTextEntities"), std::string::npos);
	EXPECT_NE(GameplayPreLayout.find("g_Config.m_ClTextEntities ^= 1;"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("((uint64_t)(g_Config.m_ClTextEntities != 0) << 0)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("if(g_Config.m_ClTextEntities)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("if(g_Config.m_ClAntiPing)"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("CUIRect GameplayRow;"), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("Gameplay.VSplitMid(&Left, &Right, 20.0f);"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(BackgroundSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(MiscellaneousSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("SaveSettingsCardOrderModel();"), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("EndSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("s_PrevDDNetSettingsScrollY"), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("MainView.HSplitTop(130.0f, &Demo, &MainView);"), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("MainView.HSplitTop(GameplayHeight, &Gameplay, &MainView);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsDisplayCycleUpdatesAfterTabInputBeforePageRender)
{
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string DeckSource = ReadTextFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	const std::string RenderSettings = FunctionBody(SettingsSource, "void CMenus::RenderSettings(CUIRect MainView)");
	ASSERT_FALSE(RenderSettings.empty());
	const size_t TabInput = RenderSettings.find("DoButton_MenuTab(&m_aSettingsTabButtons[i]");
	const size_t DisplayCycle = RenderSettings.find("m_SettingsCardDeck.BeginDisplayCycle(");
	const size_t PageRender = RenderSettings.find("RenderSettingsGeneral(ContentView)");
	ASSERT_NE(TabInput, std::string::npos);
	ASSERT_NE(DisplayCycle, std::string::npos);
	ASSERT_NE(PageRender, std::string::npos);
	EXPECT_LT(TabInput, DisplayCycle);
	EXPECT_LT(DisplayCycle, PageRender);
	EXPECT_NE(DeckSource.find("const bool TabChanged = m_LastRenderedTab != pTab;"), std::string::npos);
	EXPECT_NE(DeckSource.find("if(TabChanged || StableIdsChanged || ModelCountChanged || StateIndexChanged)"), std::string::npos);
	EXPECT_NE(DeckSource.find("m_SuppressHoverFeedbackOnce = true;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsTransitionsDoNotChangePageBrightness)
{
	const std::vector<const char *> vFiles = {
		"src/game/client/components/menus_settings.cpp",
		"src/game/client/components/menus_settings7.cpp",
		"src/game/client/components/tclient/menus_tclient.cpp",
		"src/game/client/components/qmclient/menus_qmclient.cpp",
	};
	for(const char *pFile : vFiles)
	{
		const std::string Source = ReadTextFile(pFile);
		EXPECT_EQ(Source.find("ColorRGBA(0.0f, 0.0f, 0.0f, TransitionAlpha)"), std::string::npos) << pFile;
		EXPECT_EQ(Source.find("ColorRGBA(0.0f, 0.0f, 0.0f, TabTransitionAlpha)"), std::string::npos) << pFile;
	}
}

TEST(QmNewUiMenuBranches, SettingsCardFeedbackFixesUseStableLayouts)
{
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettingsDDNet = FunctionBody(SettingsSource, "void CMenus::RenderSettingsDDNet(CUIRect MainView)");
	ASSERT_FALSE(RenderSettingsDDNet.empty());
	EXPECT_NE(RenderSettingsDDNet.find("SettingsPageLayout("), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(DemoSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(GameplaySpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(BackgroundSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("AddCard(MiscellaneousSpec"), std::string::npos);
	EXPECT_NE(RenderSettingsDDNet.find("SettingsCardDeckForRenderPass().RenderCached(DDNetCardCtx, DDNetPage, \"ddnet\""), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(RenderSettingsDDNet.find("BeginDDNetCard(MainView"), std::string::npos);

	const std::string NamePlateBranch = BlockBodyAfter(SettingsSource, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_NAME_PLATE)");
	ASSERT_FALSE(NamePlateBranch.empty());
	EXPECT_EQ(NamePlateBranch.find("BeginSettingsScrollRegion(s_NameplateTextCardScrollRegion"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("FinishSettingsScrollRegion(s_NameplateTextCardScrollRegion"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("Localize(\"Nameplate text\")"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("NameplateTextExpandedMinHeight"), std::string::npos);

	const std::string QmSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	EXPECT_EQ(QmSource.find("RenderNameplateTextSettings(CardContent);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, LaserPreviewEntityBranchesReserveEndpointDecorationSpace)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string DoLaserPreview = FunctionBody(Source, "void CMenus::DoLaserPreview(const CUIRect *pRect, const ColorHSLA LaserOutlineColor, const ColorHSLA LaserInnerColor, const int LaserType)");

	EXPECT_NE(DoLaserPreview.find("const vec2 EntityLaserEnd = LaserType == LASERTYPE_DOOR ? Pos - vec2(34.0f, 0.0f) : Pos - vec2(42.0f, 0.0f);"), std::string::npos);
	EXPECT_NE(DoLaserPreview.find("RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, EMOTE_NORMAL, vec2(-1, 0), Pos - vec2(20.0f, 0.0f));"), std::string::npos);
	EXPECT_EQ(DoLaserPreview.find("RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, EMOTE_NORMAL, vec2(-1, 0), Pos);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, LaserEntityTypesUseDdnetEndpointRendering)
{
	const std::string Source = ReadTextFile("src/game/client/components/items.cpp");
	const std::string RenderLaser = FunctionBody(Source, "void CItems::RenderLaser(vec2 From, vec2 Pos, ColorRGBA OuterColor, ColorRGBA InnerColor, float TicksBody, float TicksHead, int Type, float GlowIntensity) const");
	ASSERT_FALSE(RenderLaser.empty());

	// Regression guard for DDNet entity beams: door/freeze/dragger are carried by
	// the laser render path, but they are not weapon lasers and must not receive
	// generic impact-splat endpoints. The endpoint side is part of the behavior:
	// door blocker at Pos only, dragger pulley at From, freeze hectagon at Pos.
	const size_t DoorBranchPos = RenderLaser.find("if(Type == LASERTYPE_DOOR)");
	const size_t DraggerBranchPos = RenderLaser.find("else if(Type == LASERTYPE_DRAGGER)");
	const size_t FreezeBranchPos = RenderLaser.find("else if(Type == LASERTYPE_FREEZE)");
	const size_t GenericHeadPos = RenderLaser.find("else\n\t{", FreezeBranchPos);
	ASSERT_NE(DoorBranchPos, std::string::npos);
	ASSERT_NE(DraggerBranchPos, std::string::npos);
	ASSERT_NE(FreezeBranchPos, std::string::npos);
	ASSERT_NE(GenericHeadPos, std::string::npos);
	EXPECT_LT(DoorBranchPos, GenericHeadPos);
	EXPECT_LT(DraggerBranchPos, GenericHeadPos);
	EXPECT_LT(FreezeBranchPos, GenericHeadPos);

	const std::string DoorBranch = RenderLaser.substr(DoorBranchPos, DraggerBranchPos - DoorBranchPos);
	const std::string DraggerBranch = RenderLaser.substr(DraggerBranchPos, FreezeBranchPos - DraggerBranchPos);
	const std::string FreezeBranch = RenderLaser.substr(FreezeBranchPos, GenericHeadPos - FreezeBranchPos);
	const std::string GenericHeadBranch = RenderLaser.substr(GenericHeadPos);
	EXPECT_NE(DoorBranch.find("m_DoorHeadOffset"), std::string::npos);
	EXPECT_NE(DoorBranch.find("Pos.x - 8.0f, Pos.y - 8.0f"), std::string::npos);
	EXPECT_EQ(DoorBranch.find("From.x"), std::string::npos);
	EXPECT_NE(DraggerBranch.find("GameClient()->m_ExtrasSkin.m_SpritePulley"), std::string::npos);
	EXPECT_NE(DraggerBranch.find("m_PulleyHeadOffset, From.x, From.y"), std::string::npos);
	EXPECT_EQ(DraggerBranch.find("m_PulleyHeadOffset, Pos.x, Pos.y"), std::string::npos);
	EXPECT_NE(FreezeBranch.find("GameClient()->m_ExtrasSkin.m_SpriteHectagon"), std::string::npos);
	EXPECT_NE(FreezeBranch.find("m_FreezeHeadOffset, Pos.x, Pos.y"), std::string::npos);
	EXPECT_EQ(FreezeBranch.find("m_FreezeHeadOffset, From.x, From.y"), std::string::npos);
	EXPECT_NE(GenericHeadBranch.find("m_aParticleSplatOffset[CurParticle]"), std::string::npos);
	EXPECT_EQ(DoorBranch.find("m_aParticleSplatOffset"), std::string::npos);
	EXPECT_EQ(DraggerBranch.find("m_aParticleSplatOffset"), std::string::npos);
	EXPECT_EQ(FreezeBranch.find("m_aParticleSplatOffset"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientSettingsTabsPreserveHiddenStateAndVisibleCorners)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string RenderSettingsTClient = FunctionBody(Source, "void CMenus::RenderSettingsTClient(CUIRect MainView, bool PrewarmOnly)");
	const std::string RenderSettingsTClientInfo = FunctionBody(Source, "void CMenus::RenderSettingsTClientInfo(CUIRect MainView, bool PrewarmOnly)");

	EXPECT_NE(RenderSettingsTClient.find("if(TabCount <= 0)"), std::string::npos);
	EXPECT_NE(RenderSettingsTClient.find("FirstVisibleTab"), std::string::npos);
	EXPECT_NE(RenderSettingsTClient.find("VisibleTabIndex"), std::string::npos);
	EXPECT_NE(RenderSettingsTClient.find("VisibleTabIndex == 0"), std::string::npos);
	EXPECT_NE(RenderSettingsTClient.find("VisibleTabIndex == TabCount - 1"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientInfo.find("s_aShowTabs[i] = IsFlagSet(g_Config.m_TcTClientSettingsTabs, i);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientInfoUsesPublicCardDeck)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Registry = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsTClientInfo(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(Body.find("SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(Body.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_InfoPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(Body.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_EQ(Body.find("MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-info-links\", \"tclient-info\", ECardColumn::Left, 0"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-info-files\", \"tclient-info\", ECardColumn::Left, 1"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-info-developers\", \"tclient-info\", ECardColumn::Right, 0"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-info-tabs\", \"tclient-info\", ECardColumn::Right, 1"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientProfilesUsesPublicCardDeck)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Registry = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsTClientProfiles(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(Body.find("SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(Body.find("ProfilesListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);"), std::string::npos);
	EXPECT_NE(Body.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_ProfilesPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(Body.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(Body.find("const float ProfileActionsHeight = s_AllowDelete ? ProfileMetrics.m_ButtonHeight * 5.0f"), std::string::npos);
	EXPECT_NE(Body.find("Rect.VSplitLeft(ProfileMetrics.m_LineSpacing, nullptr, &Rect);"), std::string::npos);
	EXPECT_NE(Body.find("DrawRoundedSurface(Ui(), Skin, ColorRGBA(1.0f, 1.0f, 1.0f, 0.035f)"), std::string::npos);
	EXPECT_NE(Body.find("Skin.VMargin(std::max(0.0f, (Skin.w - PreviewRowWidth) * 0.5f), &Skin);"), std::string::npos);
	EXPECT_NE(Body.find("ResolveSettingsInlineRowMinimumWidth(ProfileMetrics.m_LabelWidth"), std::string::npos);
	EXPECT_NE(Body.find("ProfileMetrics.m_ListRowHeight"), std::string::npos);
	EXPECT_NE(Body.find("static_cast<uint64_t>(s_AllowDelete != 0) << 1"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-profiles-actions\", \"tclient-profiles\", ECardColumn::Left, 0"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-profiles-options\", \"tclient-profiles\", ECardColumn::Right, 0"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-profiles-list\", \"tclient-profiles\", ECardColumn::Left, 1"), std::string::npos);
	EXPECT_NE(Body.find("CTClientSettingsRowAllocator IdentityRows(View);"), std::string::npos);
	EXPECT_NE(Body.find("Row = IdentityRows.Next();"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientConfigsUsesPublicCardDeck)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Registry = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsTClientConfigs(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(Body.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_ConfigsPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(Body.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(Body.find("static CScrollRegion s_ConfigListScrollRegion;"), std::string::npos);
	EXPECT_NE(Body.find("ConfigListScrollRequest.m_Profile = EQmScrollProfile::SETTINGS_INNER;"), std::string::npos);
	EXPECT_EQ(Body.find("QmSettingsScrollRegionParams(1.0f)"), std::string::npos);
	EXPECT_NE(Registry.find("{\"deck:tclient-configs-actions\", \"tclient-configs\", ECardColumn::Full, 0"), std::string::npos);
	EXPECT_EQ(Registry.find("deck:tclient-configs-filters"), std::string::npos);
	EXPECT_EQ(Registry.find("deck:tclient-configs-list"), std::string::npos);
	EXPECT_NE(Body.find("ResolveSettingsInlineRowMinimumWidth("), std::string::npos);
	EXPECT_EQ(Body.find("ContentWidth < 760.0f"), std::string::npos);
}

TEST(QmNewUiMenuBranches, QmClientDecksIsolateRenderOnlyState)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	for(const char *pSignature : {
		    "void CMenus::RenderSettingsQmClientHudDeck(CUIRect MainView, bool PrewarmOnly)",
		    "void CMenus::RenderSettingsQmClientFunctionDeck(CUIRect MainView, bool PrewarmOnly)",
		    "void CMenus::RenderSettingsQmClientVisualDeck(CUIRect MainView, bool PrewarmOnly)",
		    "void CMenus::RenderSettingsQmClientContributors(CUIRect MainView, bool PrewarmOnly)",
	    })
	{
		const std::string Body = FunctionBody(Source, pSignature);
		ASSERT_FALSE(Body.empty());
		EXPECT_NE(Body.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
		EXPECT_NE(Body.find("CSettingsCardDeck &CardDeck = ReadOnly ?"), std::string::npos);
		EXPECT_NE(Body.find("qm_card_order::CModel &CardOrderModel = ReadOnly ?"), std::string::npos);
		EXPECT_NE(Body.find("InputState.m_AllowHeaderDrag = !ReadOnly;"), std::string::npos);
		EXPECT_NE(Body.find("ReadOnly ? nullptr : &"), std::string::npos);
		EXPECT_NE(Body.find("if(!ReadOnly && DeckResult.m_OrderChanged)"), std::string::npos);
	}
}

TEST(QmNewUiMenuBranches, TClientWarListDefersDeletesAndValidatesSelections)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string RenderSettingsTClientWarList = FunctionBody(Source, "void CMenus::RenderSettingsTClientWarList(CUIRect MainView, bool PrewarmOnly)");

	EXPECT_NE(RenderSettingsTClientWarList.find("static CWarType *s_pSelectedType = nullptr;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientWarList.find("WarTypeExists"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientWarList.find("WarEntryExists"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientWarList.find("CWarEntry *pEntryToRemove = nullptr;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientWarList.find("RemoveWarEntry(pEntryToRemove);"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientWarList.find("s_pSelectedEntry = nullptr;"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientWarList.find("NewSelectedEntry < (int)s_vFilteredEntries.size()"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientWarList.find("NewSelectedType < (int)GameClient()->m_WarList.m_WarTypes.size()"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientWarListUsesPublicCardDeck)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Registry = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSettingsTClientWarList(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(Body.find("SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(Body.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_WarListPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(Body.find("str_startswith(m_SettingsCardFocusStableId.c_str(), \"deck:tclient-warlist\")"), std::string::npos);
	EXPECT_NE(Body.find("CardDeck.RequestReveal(m_SettingsCardFocusStableId.c_str());"), std::string::npos);
	EXPECT_NE(Body.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(Body.find("if(!ReadOnly)\n\t\t\tui_widget::InputField"), std::string::npos);
	EXPECT_NE(Body.find("if(!ReadOnly && DeckResult.m_OrderChanged)"), std::string::npos);
	EXPECT_EQ(Body.find("MainView.VSplitMid(&LeftView, &RightView, Margin);"), std::string::npos);
	EXPECT_EQ(Body.find("LeftView.VSplitMid(&Column1, &Column2, Margin);"), std::string::npos);
	EXPECT_EQ(Body.find("RightView.VSplitMid(&Column3, &Column4, Margin);"), std::string::npos);

	EXPECT_NE(Registry.find("{\"deck:tclient-warlist\", \"tclient-warlist\", ECardColumn::Full, 0"), std::string::npos);
	EXPECT_NE(Body.find("const float FourColumnMinWidth = 4.0f * WarListColumnMinimum"), std::string::npos);
	EXPECT_NE(Body.find("const float TwoColumnMinWidth = 2.0f * WarListColumnMinimum"), std::string::npos);
	EXPECT_NE(Body.find("WarListMetrics.m_ListRowHeight"), std::string::npos);
	EXPECT_NE(Body.find("constexpr int WarListViewportRows = 8;"), std::string::npos);
	EXPECT_NE(Body.find("const float EntriesHeight = LineSize * 2.0f + MarginSmall + WarListViewportRows * ListRowHeight;"), std::string::npos);
	EXPECT_NE(Body.find("Column.HSplitTop(WarListViewportRows * ListRowHeight, &WarTypeList, &Column);"), std::string::npos);
	EXPECT_NE(Body.find("const float PlayersHeight = LineSize + MarginSmall + WarListViewportRows * ListRowHeight;"), std::string::npos);
	EXPECT_NE(Body.find("if(!ReadOnly)\n\t\t\t\tRenderTeeCute"), std::string::npos);
	EXPECT_NE(Body.find("RenderWarListLayout(ContentRect, true);"), std::string::npos);
	EXPECT_NE(Body.find("Localizable(\"War Entries\")"), std::string::npos);
	EXPECT_NE(Body.find("Localizable(\"War Groups\")"), std::string::npos);
	EXPECT_NE(Body.find("Localizable(\"Edit Entry\")"), std::string::npos);
	EXPECT_NE(Body.find("Localizable(\"Online Players\")"), std::string::npos);
	EXPECT_NE(Body.find("RenderSection(EntriesColumn, \"tclient-warlist-section-entries\", pWarEntriesTitle"), std::string::npos);
	EXPECT_NE(Body.find("RenderSection(EditorColumn, \"tclient-warlist-section-editor\", pEditEntryTitle"), std::string::npos);
	EXPECT_NE(Body.find("RenderSection(GroupsColumn, \"tclient-warlist-section-groups\", pWarGroupsTitle"), std::string::npos);
	EXPECT_NE(Body.find("RenderSection(PlayersColumn, \"tclient-warlist-section-players\", pOnlinePlayersTitle"), std::string::npos);
	EXPECT_NE(Body.find("RenderSection(EntriesColumn, \"tclient-warlist-section-settings\", pSettingsTitle"), std::string::npos);
	EXPECT_NE(Body.find("PlayerListBox.DoStart(ListRowHeight, s_vFilteredPlayerIds.size()"), std::string::npos);
	EXPECT_EQ(Body.find("PlayerListBox.DoStart(ListRowHeight, MAX_CLIENTS"), std::string::npos);
	EXPECT_NE(Body.find("maximum(EntriesSectionHeight + SectionGap + SettingsSectionHeight, EditorSectionHeight)"), std::string::npos);
	EXPECT_NE(Body.find("SecondRow.VSplitMid(&GroupsColumn, &PlayersColumn, SectionGap);"), std::string::npos);
	const size_t TwoColumnBranch = Body.find("else if(ContentRect.w >= TwoColumnMinWidth)");
	ASSERT_NE(TwoColumnBranch, std::string::npos);
	const size_t SingleColumnBranch = Body.find("\t\telse\n\t\t{", TwoColumnBranch);
	ASSERT_NE(SingleColumnBranch, std::string::npos);
	const size_t SingleEntries = Body.find("RenderSection(ContentRect, \"tclient-warlist-section-entries\"", SingleColumnBranch);
	const size_t SingleSettings = Body.find("RenderSection(ContentRect, \"tclient-warlist-section-settings\"", SingleColumnBranch);
	const size_t SingleEditor = Body.find("RenderSection(ContentRect, \"tclient-warlist-section-editor\"", SingleColumnBranch);
	const size_t SingleGroups = Body.find("RenderSection(ContentRect, \"tclient-warlist-section-groups\"", SingleColumnBranch);
	const size_t SinglePlayers = Body.find("RenderSection(ContentRect, \"tclient-warlist-section-players\"", SingleColumnBranch);
	ASSERT_NE(SingleEntries, std::string::npos);
	ASSERT_NE(SingleSettings, std::string::npos);
	ASSERT_NE(SingleEditor, std::string::npos);
	ASSERT_NE(SingleGroups, std::string::npos);
	ASSERT_NE(SinglePlayers, std::string::npos);
	EXPECT_LT(SingleEntries, SingleSettings);
	EXPECT_LT(SingleSettings, SingleEditor);
	EXPECT_LT(SingleEditor, SingleGroups);
	EXPECT_LT(SingleGroups, SinglePlayers);
	EXPECT_EQ(Body.find("deck:tclient-warlist-entries"), std::string::npos);

	const size_t EntriesPriority = Body.find("EntriesListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t EntriesStart = Body.find("EntriesListBox.DoStart(");
	const size_t GroupsPriority = Body.find("WarTypeListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t GroupsStart = Body.find("WarTypeListBox.DoStart(");
	const size_t PlayersPriority = Body.find("PlayerListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t PlayersStart = Body.find("PlayerListBox.DoStart(");
	ASSERT_NE(EntriesPriority, std::string::npos);
	ASSERT_NE(EntriesStart, std::string::npos);
	ASSERT_NE(GroupsPriority, std::string::npos);
	ASSERT_NE(GroupsStart, std::string::npos);
	ASSERT_NE(PlayersPriority, std::string::npos);
	ASSERT_NE(PlayersStart, std::string::npos);
	EXPECT_LT(EntriesPriority, EntriesStart);
	EXPECT_LT(GroupsPriority, GroupsStart);
	EXPECT_LT(PlayersPriority, PlayersStart);
}

TEST(QmNewUiMenuBranches, TClientProfilesAndStatusBarClampUiIndices)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string RenderSettingsTClientProfiles = FunctionBody(Source, "void CMenus::RenderSettingsTClientProfiles(CUIRect MainView, bool PrewarmOnly)");
	const std::string RenderSettingsTClientStatusBar = FunctionBody(Source, "void CMenus::RenderSettingsTClientStatusBar(CUIRect MainView, bool PrewarmOnly)");

	EXPECT_NE(RenderSettingsTClientProfiles.find("Profile.m_FeetColor >= 0"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientProfiles.find("ProfilesPerRow = maximum(1"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("StatusItemTypeCount"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("s_TypeSelectedOld < StatusItemTypeCount"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("s_SelectedItem < (int)GameClient()->m_StatusBar.m_StatusBarItems.size()"), std::string::npos);
}

TEST(QmNewUiMenuBranches, BackgroundMapPickerUsesMapsRootAndSupportedFiles)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettingsDDNet = FunctionBody(Source, "void CMenus::RenderSettingsDDNet(CUIRect MainView)");
	const std::string MapListPopulate = FunctionBody(Source, "void CMenus::CPopupMapPickerContext::MapListPopulate()");
	const std::string MapListFetchCallback = FunctionBody(Source, "int CMenus::CPopupMapPickerContext::MapListFetchCallback");

	EXPECT_NE(RenderSettingsDDNet.find("str_copy(s_PopupMapPickerContext.m_aRootPath, \"maps\""), std::string::npos);
	EXPECT_NE(MapListPopulate.find("ListRoot(m_aRootPath[0] != '\\0' ? m_aRootPath : \"maps\", m_aValuePrefix);"), std::string::npos);
	EXPECT_EQ(MapListPopulate.find("m_aFallbackRootPath"), std::string::npos);
	EXPECT_EQ(MapListPopulate.find("m_aFallbackValuePrefix"), std::string::npos);
	EXPECT_NE(MapListFetchCallback.find("FindBackgroundFileExtension(pInfo->m_pName)"), std::string::npos);
	EXPECT_EQ(MapListFetchCallback.find("str_endswith(pInfo->m_pName, \".map\")"), std::string::npos);
}

TEST(QmNewUiMenuBranches, EditorSaveFileDialogKeepsFilenameInputInControl)
{
	const std::string Source = ReadTextFile("src/game/editor/file_browser.cpp");
	const std::string OnRender = FunctionBody(Source, "void CFileBrowser::OnRender(CUIRect _)");

	EXPECT_NE(OnRender.find("m_ListBox.SetActive(!Ui()->IsPopupOpen() && (!m_SaveAction || !m_FilenameInput.IsActive()))"), std::string::npos);
	EXPECT_NE(OnRender.find("const bool ListChoseItem = m_ListBox.WasItemSelected() || m_ListBox.WasItemActivated();"), std::string::npos);
	EXPECT_NE(OnRender.find("const bool SyncFilenameInput = !m_SaveAction || (ListChoseItem && m_SelectedFileIndex >= 0);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, KcpLogUsesBoundedFormatting)
{
	const std::string Source = ReadTextFile("src/engine/external/kcp/ikcp.c");

	EXPECT_EQ(Source.find("vsprintf(buffer, fmt, argptr);"), std::string::npos);
	EXPECT_NE(Source.find("vsnprintf(buffer, sizeof(buffer), fmt, argptr);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DisplayChangedDoesNotUseDisplayUnionData)
{
	const std::string Source = ReadTextFile("src/engine/client/input.cpp");
	const size_t CaseStart = Source.find("case SDL_WINDOWEVENT_DISPLAY_CHANGED:");
	ASSERT_NE(CaseStart, std::string::npos);
	const size_t Break = Source.find("break;", CaseStart);
	ASSERT_NE(Break, std::string::npos);
	const std::string Body = Source.substr(CaseStart, Break - CaseStart);

	EXPECT_EQ(Body.find("Event.display.data1"), std::string::npos);
	EXPECT_NE(Body.find("Event.window.data1"), std::string::npos);
	EXPECT_NE(Body.find("Graphics()->SwitchWindowScreen(DisplayIndex, false);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, OpenGLSelectionUsesRuntimeContextDetection)
{
	const std::string ConfigVariables = ReadTextFile("src/engine/shared/config_variables.h");
	const SOpenGLVersion AutoGL = AutoOpenGLProbeVersion(EBackendType::BACKEND_TYPE_OPENGL);
	const SOpenGLVersion AutoGLES = AutoOpenGLProbeVersion(EBackendType::BACKEND_TYPE_OPENGL_ES);
	EXPECT_EQ(AutoGL.m_Major, 4);
#if defined(CONF_PLATFORM_MACOS)
	EXPECT_EQ(AutoGL.m_Minor, 1);
#else
	EXPECT_EQ(AutoGL.m_Minor, 6);
#endif
	EXPECT_EQ(AutoGLES.m_Major, 3);
	EXPECT_EQ(AutoGLES.m_Minor, 0);
	EXPECT_TRUE(IsOpenGLVersionAtLeast({4, 6, 0}, {4, 5, 0}));
	EXPECT_FALSE(IsOpenGLVersionAtLeast({4, 5, 0}, {4, 6, 0}));
	EXPECT_TRUE(IsOpenGLVersionAtLeast({3, 3, 0}, {3, 3, 0}));
	EXPECT_FALSE(IsOpenGLVersionAtLeast({1, 2, 0}, {1, 2, 1}));
	SOpenGLVersion ProbeVersion{4, 6, 0};
	for(int Minor = 5; Minor >= 0; --Minor)
	{
		EXPECT_TRUE(NextAutoOpenGLProbeVersion(ProbeVersion));
		EXPECT_EQ(ProbeVersion.m_Major, 4);
		EXPECT_EQ(ProbeVersion.m_Minor, Minor);
	}
	EXPECT_TRUE(NextAutoOpenGLProbeVersion(ProbeVersion));
	EXPECT_EQ(ProbeVersion.m_Major, 3);
	EXPECT_EQ(ProbeVersion.m_Minor, 3);
	EXPECT_TRUE(NextAutoOpenGLProbeVersion(ProbeVersion));
	EXPECT_EQ(ProbeVersion.m_Minor, 2);
	EXPECT_NE(ConfigVariables.find("MACRO_CONFIG_INT(GfxGLMajor, gfx_gl_major, 0, 0, 10"), std::string::npos);
	EXPECT_NE(ConfigVariables.find("MACRO_CONFIG_INT(GfxGLMinor, gfx_gl_minor, 0, 0, 10"), std::string::npos);

	const SOpenGLVersion Actual41{4, 1, 0};
	EXPECT_TRUE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL, {3, 3, 0}, Actual41));
	EXPECT_TRUE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL, {4, 6, 0}, Actual41));
	EXPECT_FALSE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL, {3, 0, 0}, Actual41));
	EXPECT_FALSE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_VULKAN, {3, 3, 0}, Actual41));
	EXPECT_TRUE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL_ES, {3, 0, 0}, {3, 2, 0}));
	EXPECT_FALSE(ShouldSyncActualOpenGLVersion(EBackendType::BACKEND_TYPE_OPENGL_ES, {1, 0, 0}, {3, 2, 0}));
}

TEST(QmNewUiMenuBranches, GraphicsCurrentModeLabelSanitizesScaleAndAspectRatio)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");

	EXPECT_NE(Source.find("const float HiDPIScale = std::isfinite(RawHiDPIScale) && RawHiDPIScale > 0.0f ? RawHiDPIScale : 1.0f;"), std::string::npos);
	EXPECT_NE(Source.find("const int AspectGcd = G > 0 ? G : 1;"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_GfxScreenWidth / AspectGcd"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_GfxScreenHeight / AspectGcd"), std::string::npos);
}

TEST(QmMovingTiles, RestoresIncomingScreenMappingAfterRendering)
{
	const std::string Body = FunctionBody(ReadTextFile("src/game/client/components/tclient/moving_tiles.cpp"), "void CMovingTiles::OnRender()");
	const size_t GroupStatePos = Body.find("auto ApplyGroupState =");
	ASSERT_NE(GroupStatePos, std::string::npos);
	const std::string BeforeGroupState = Body.substr(0, GroupStatePos);

	// 从入口保存操作提取四个坐标，要求绘制结束后按原顺序恢复，而不是重设默认相机。
	const std::regex SaveScreenPattern(R"(Graphics\(\)->GetScreen\(&([A-Za-z0-9_]+), &([A-Za-z0-9_]+), &([A-Za-z0-9_]+), &([A-Za-z0-9_]+)\);)");
	std::smatch SavedScreen;
	ASSERT_TRUE(std::regex_search(BeforeGroupState, SavedScreen, SaveScreenPattern));
	const std::string RestoreScreen = "Graphics()->MapScreen(" + SavedScreen[1].str() + ", " + SavedScreen[2].str() + ", " + SavedScreen[3].str() + ", " + SavedScreen[4].str() + ");";

	const size_t RenderPos = Body.rfind("RenderPass();");
	ASSERT_NE(RenderPos, std::string::npos);
	const size_t RestorePos = Body.find(RestoreScreen, RenderPos);
	ASSERT_NE(RestorePos, std::string::npos);
	EXPECT_EQ(Body.find("return", RenderPos), std::string::npos);
	EXPECT_EQ(Body.find("MapScreen", RestorePos + RestoreScreen.size()), std::string::npos);
}

TEST(QmCameraAspectRatio, KeepsUiAspectPhysicalAndOverridesOnlyGameWorld)
{
	const std::string GraphicsHeader = ReadTextFile("src/engine/graphics.h");
	const std::string GraphicsSource = ReadTextFile("src/engine/graphics.cpp");
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/tclient.cpp");
	const std::string GameClientSource = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string ControlsSource = ReadTextFile("src/game/client/components/controls.cpp");
	const std::string HudSource = ReadTextFile("src/game/client/components/hud.cpp");
	const std::string TouchControlsSource = ReadTextFile("src/game/client/components/touch_controls.cpp");
	const std::string CollisionHitboxSource = ReadTextFile("src/game/client/components/qmclient/collision_hitbox.cpp");
	const std::string BackgroundParticlesSource = ReadTextFile("src/game/client/components/tclient/background_particles.cpp");
	const std::string RenderLayerSource = ReadTextFile("src/game/map/render_layer.cpp");
	const std::string MapRendererSource = ReadTextFile("src/game/map/map_renderer.cpp");
	const std::string MovingTilesSource = ReadTextFile("src/game/client/components/tclient/moving_tiles.cpp");
	const std::string NameplatesSource = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");

	EXPECT_NE(GraphicsHeader.find("float ScreenAspect() const { return (float)ScreenWidth() / (float)ScreenHeight(); }"), std::string::npos);
	EXPECT_NE(GraphicsHeader.find("float GameScreenAspect() const { return m_GameScreenAspectOverride > 0.0f ? m_GameScreenAspectOverride : ScreenAspect(); }"), std::string::npos);
	EXPECT_NE(TClientSource.find("Graphics()->SetGameScreenAspectOverride(GameScreenAspectOverride);"), std::string::npos);
	EXPECT_EQ(TClientSource.find("SetScreenAspectOverride"), std::string::npos);
	EXPECT_NE(RenderLayerSource.find("Graphics()->GameScreenAspect()"), std::string::npos);
	EXPECT_NE(RenderLayerSource.find("Graphics()->MapScreenToGameInterface("), std::string::npos);
	EXPECT_NE(MapRendererSource.find("Graphics()->MapScreenToGameInterface("), std::string::npos);
	EXPECT_NE(MovingTilesSource.find("Graphics()->MapScreenToGameInterface("), std::string::npos);
	EXPECT_NE(NameplatesSource.find("This.Graphics()->MapScreenToGameInterface("), std::string::npos);
	EXPECT_NE(GameClientSource.find("CalcScreenParams(Graphics()->GameScreenAspect(), ShowDistanceZoom"), std::string::npos);
	EXPECT_NE(GameClientSource.find("m_LastScreenAspect = Graphics()->GameScreenAspect();"), std::string::npos);
	EXPECT_NE(GameClientSource.find("CalcScreenParams(Graphics()->GameScreenAspect(), m_Camera.m_Zoom"), std::string::npos);
	EXPECT_NE(ControlsSource.find("CalcScreenParams(Graphics()->GameScreenAspect(), 1.0f"), std::string::npos);
	EXPECT_NE(HudSource.find("CalcScreenParams(pGraphics->GameScreenAspect(), GameClient.m_Camera.m_Zoom"), std::string::npos);
	EXPECT_NE(HudSource.find("Graphics()->GameScreenAspect(), MiniZoom, aPoints"), std::string::npos);
	EXPECT_NE(HudSource.find("Graphics()->GameScreenAspect(), 1.0f, aPoints"), std::string::npos);
	EXPECT_NE(TouchControlsSource.find("CalcScreenParams(m_pTouchControls->Graphics()->GameScreenAspect()"), std::string::npos);
	EXPECT_NE(TouchControlsSource.find("CalcScreenParams(Graphics()->GameScreenAspect(), Zoom"), std::string::npos);
	EXPECT_NE(CollisionHitboxSource.find("Graphics()->GameScreenAspect(), GameClient()->m_Camera.m_Zoom"), std::string::npos);
	EXPECT_NE(BackgroundParticlesSource.find("Graphics()->GameScreenAspect(), Zoom, aPoints"), std::string::npos);

	const std::string MapScreenToInterface = FunctionBody(GraphicsSource, "void IGraphics::MapScreenToInterface(");
	const std::string MapScreenToGameInterface = FunctionBody(GraphicsSource, "void IGraphics::MapScreenToGameInterface(");
	EXPECT_NE(MapScreenToInterface.find("ScreenAspect()"), std::string::npos);
	EXPECT_EQ(MapScreenToInterface.find("GameScreenAspect()"), std::string::npos);
	EXPECT_NE(MapScreenToGameInterface.find("GameScreenAspect()"), std::string::npos);
	EXPECT_NE(UiSource.find("Graphics()->ScreenAspect()"), std::string::npos);
	EXPECT_EQ(UiSource.find("GameScreenAspect()"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TClientQueuesAspectRefreshFromSnapshots)
{
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/tclient.cpp");
	const std::string SnapshotBody = FunctionBody(TClientSource, "void CTClient::OnNewSnapshot()");
	const std::string UpdateBody = FunctionBody(TClientSource, "void CTClient::OnUpdate()");

	ASSERT_FALSE(SnapshotBody.empty());
	ASSERT_FALSE(UpdateBody.empty());
	EXPECT_NE(SnapshotBody.find("QueueAspectApply();"), std::string::npos);
	EXPECT_EQ(SnapshotBody.find("SetForcedAspect();"), std::string::npos);
	EXPECT_NE(UpdateBody.find("if(m_QmAspectApplyPending)"), std::string::npos);
	EXPECT_NE(UpdateBody.find("SetForcedAspect();"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsDropdownWrapperAndNestedListsKeepSharedVisualAndScrollContracts)
{
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string ListBoxHeader = ReadTextFile("src/game/client/ui_listbox.h");
	const std::string ListBoxSource = ReadTextFile("src/game/client/ui_listbox.cpp");
	const std::string Tee = FunctionBody(SettingsSource, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	const std::string Wrapper = FunctionBody(MenusSource, "int CMenus::DoSettingsDropDown(CUIRect *pRect, const int CurSelection, const char *const *ppStrs, const int Num, CUi::SDropDownState &State, CUi::SDropDownProperties Properties)");

	ASSERT_FALSE(Tee.empty());
	ASSERT_FALSE(Wrapper.empty());
	EXPECT_NE(Wrapper.find("Properties.m_VisualStyle = QmSettingsDropdownVisualStyle(m_SettingsUiTheme);"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("void SetScrollbarAlwaysReserved(bool AlwaysReserved)"), std::string::npos);
	EXPECT_NE(ListBoxSource.find("ScrollParams.m_ScrollbarAlwaysReserved = m_ScrollbarAlwaysReserved;"), std::string::npos);
	EXPECT_NE(Tee.find("s_QueueListBox.SetScrollbarAlwaysReserved(true);"), std::string::npos);
	EXPECT_NE(Tee.find("s_PresetListBox.SetScrollbarAlwaysReserved(true);"), std::string::npos);
}

TEST(QmNewUiMenuBranches, CallVoteSearchSupportsIndependentExclusion)
{
	const std::string MenusHeader = ReadTextFile("src/game/client/components/menus.h");
	const std::string IngameMenus = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const std::string ServerList = FunctionBody(IngameMenus, "bool CMenus::RenderServerControlServer(CUIRect MainView, bool UpdateScroll)");
	const std::string PlayerList = FunctionBody(IngameMenus, "bool CMenus::RenderServerControlKick(CUIRect MainView, bool FilterSpectators, bool UpdateScroll)");
	const std::string RenderControl = FunctionBody(IngameMenus, "void CMenus::RenderServerControl(CUIRect MainView)");

	ASSERT_FALSE(ServerList.empty());
	ASSERT_FALSE(PlayerList.empty());
	ASSERT_FALSE(RenderControl.empty());
	EXPECT_NE(MenusHeader.find("CLineInputBuffered<64> m_ExcludeInput;"), std::string::npos);
	EXPECT_NE(ServerList.find("QmTextMatchesIncludeExcludeFilter(pOption->m_aDescription, m_FilterInput.GetString(), m_ExcludeInput.GetString())"), std::string::npos);
	EXPECT_NE(PlayerList.find("QmTextMatchesIncludeExcludeFilter(GameClient()->m_aClients[Index].m_aName, m_FilterInput.GetString(), m_ExcludeInput.GetString())"), std::string::npos);
	EXPECT_NE(RenderControl.find("ingame_callvote_exclude"), std::string::npos);
	EXPECT_NE(RenderControl.find("CallvoteExcludeOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;"), std::string::npos);
	EXPECT_NE(RenderControl.find("CallvoteExcludeOptions.m_pPlaceholder = Localize(\"Exclude\");"), std::string::npos);
	EXPECT_NE(RenderControl.find("const float MapSortWidth = HasMapSort ? 140.0f : 0.0f;"), std::string::npos);
	EXPECT_NE(RenderControl.find("const float FilterWidth = std::min(220.0f, std::max(1.0f, (Bottom.w - 5.0f - MapSortWidth - MapSortGap) * 0.5f));"), std::string::npos);
}

TEST(QmNewUiMenuBranches, IngameFavoriteMapsUsesSharedBookmarkIcon)
{
	const std::string Ingame = FunctionBody(ReadTextFile("src/game/client/components/menus_ingame.cpp"), "void CMenus::RenderInGameNetwork(CUIRect MainView)");

	ASSERT_FALSE(Ingame.empty());
	EXPECT_NE(Ingame.find("DoMenuTabV2(&s_FavoriteMapsButton, \"\", g_Config.m_UiPage == PAGE_FAVORITE_MAPS"), std::string::npos);
	EXPECT_NE(Ingame.find("QmIconManager()->RenderIcon(EQmIcon::BOOKMARK"), std::string::npos);
	EXPECT_NE(Ingame.find("FONT_ICON_BOOKMARK"), std::string::npos);
	EXPECT_NE(Ingame.find("TextRender()->TextColor(OldTextColor)"), std::string::npos);
	EXPECT_EQ(Ingame.find("\xF0\x9F\x94\x96"), std::string::npos);
}

TEST(QmNewUiMenuBranches, TeePresetListUsesTheSameRowSpacingAsItsMeasuredViewport)
{
	const std::string Settings = FunctionBody(ReadTextFile("src/game/client/components/menus_settings.cpp"), "void CMenus::RenderSettingsTee(CUIRect MainView)");

	ASSERT_FALSE(Settings.empty());
	EXPECT_NE(Settings.find("const float PresetRowSpacing = TeeMetrics.m_LineSpacing * 0.5f;"), std::string::npos);
	EXPECT_NE(Settings.find("s_PresetListBox.DoAutoSpacing(PresetRowSpacing);"), std::string::npos);
	EXPECT_NE(Settings.find("s_PresetListBox.DoNextItem(&s_vPresetItemIds[i], ActivePresetIndex == (int)i, PresetRowSpacing)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, GeneralStandardPageUsesUnifiedSettingsStack)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Menus = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string General = FunctionBody(Source, "void CMenus::RenderSettingsGeneral(CUIRect MainView)");
	const std::string NumericLabelBridge = FunctionBody(Menus, "bool CMenus::PrepareSettingsNumericFieldLabel(");
	ASSERT_FALSE(General.empty());
	ASSERT_FALSE(NumericLabelBridge.empty());
	EXPECT_NE(General.find("SettingsPageLayout("), std::string::npos);
	EXPECT_NE(General.find("SSettingsCardDefinition"), std::string::npos);
	EXPECT_NE(General.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(General.find("CQmScrollState"), std::string::npos);
	EXPECT_NE(General.find("const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(General.find("QmScrollRegionParamsFromPolicy(ScrollPolicy)"), std::string::npos);
	EXPECT_EQ(General.find("(void)QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(General.find("PrepareSettingsNumericFieldLabel("), std::string::npos);
	EXPECT_NE(NumericLabelBridge.find("if(m_MenuTextPlanCollecting)"), std::string::npos);
	EXPECT_NE(NumericLabelBridge.find("CollectMenuTextPlanItem(MENU_TEXT_SCOPE_SETTINGS"), std::string::npos);
	EXPECT_NE(General.find("ui_widget::NumericField("), std::string::npos);
	EXPECT_NE(General.find("const auto IsGeneralDynamicCameraEnabled"), std::string::npos);
	EXPECT_NE(General.find("ResolveSettingsGeneralGameContentHeight(GeneralMetrics, IsGeneralDynamicCameraEnabled())"), std::string::npos);
	EXPECT_NE(General.find("vCards.back().m_PreLayoutInput = [this, GeneralMetrics, IsGeneralDynamicCameraEnabled]"), std::string::npos);
	EXPECT_NE(General.find("ResolveSettingsGeneralLanguageListGeometry("), std::string::npos);
	EXPECT_NE(General.find("ResolveSettingsGeneralThemeListGeometry("), std::string::npos);
	EXPECT_NE(General.find("RenderLanguageSelection(Content, &GeneralMetrics);"), std::string::npos);
	EXPECT_NE(General.find("RenderThemeSelection(Content, &GeneralMetrics);"), std::string::npos);
	EXPECT_NE(General.find("ResolveSettingsGeneralLayoutRevision("), std::string::npos);
	EXPECT_NE(General.find("Content.h = std::min(Content.h, GeneralLanguageListHeight);"), std::string::npos);
	EXPECT_NE(General.find("Content.h = std::min(Content.h, GeneralThemeListHeight);"), std::string::npos);
	EXPECT_NE(General.find("Row.VSplitMid(&LeftButton, &RightButton, GeneralMetrics.m_LineSpacing);"), std::string::npos);
	EXPECT_EQ(General.find("maximum(300.0f * UiScale, GeneralPage.m_ScrollViewport.h - 100.0f * UiScale)"), std::string::npos);
	EXPECT_NE(General.find("deck:general-game"), std::string::npos);
	EXPECT_NE(General.find("deck:general-language"), std::string::npos);
	EXPECT_NE(General.find("deck:general-client"), std::string::npos);
	EXPECT_NE(General.find("deck:general-recording"), std::string::npos);
	EXPECT_NE(General.find("RecordingDefinition.m_MeasureRevision"), std::string::npos);
	EXPECT_NE(General.find("RecordingDefinition.m_PreLayoutInput"), std::string::npos);
	EXPECT_NE(General.find("RecordingDefinition.m_VisibilityController = true;"), std::string::npos);
	EXPECT_NE(General.find("return 4.0f * GeneralMetrics.m_RowStep + EnabledRows * (GeneralMetrics.m_RowStep + GeneralMetrics.m_LineSpacing);"), std::string::npos);
	EXPECT_EQ(General.find("AddCard(RecordingSpec"), std::string::npos);
	EXPECT_EQ(General.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(General.find("DoSettingsScrollbarOption("), std::string::npos);
	EXPECT_EQ(General.find("Ui()->DoEditBox("), std::string::npos);
	EXPECT_EQ(General.find("Ui()->DoScrollbarH("), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsCardContentHeightsExcludeSharedHeaderChrome)
{
	const std::string ControlsSource = ReadTextFile("src/game/client/components/menus_settings_controls.cpp");
	const std::string ContributorsSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string MouseMeasure = FunctionBody(ControlsSource, "float CMenusSettingsControls::MeasureSettingsMouseHeight() const");
	const std::string Contributors = FunctionBody(ContributorsSource, "void CMenus::RenderSettingsQmClientContributors(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(MouseMeasure.empty());
	ASSERT_FALSE(Contributors.empty());
	EXPECT_NE(MouseMeasure.find("return 2.0f * BUTTON_HEIGHT + BUTTON_SPACING;"), std::string::npos);
	EXPECT_EQ(MouseMeasure.find("CARD_HEADER"), std::string::npos);
	EXPECT_NE(Contributors.find("Community.m_Measure = [LineHeight, LineSpacing](float) { return ResolveSettingsRowsHeight(3, LineHeight, LineSpacing); };"), std::string::npos);
}
TEST(QmNewUiMenuBranches, Tee7NestedGridsOwnWheelAndCacheRefreshes)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings7.cpp");
	const std::string Tee7 = FunctionBody(Source, "void CMenus::RenderSettingsTee7Content(CUIRect MainView, const SSettingsContentMetrics &Metrics)");
	const std::string SkinSelection = FunctionBody(Source, "void CMenus::RenderSkinSelection7(CUIRect MainView, float BodySize)");
	const std::string SkinPartSelection = FunctionBody(Source, "void CMenus::RenderSkinPartSelection7(CUIRect MainView, float BodySize)");
	ASSERT_FALSE(Tee7.empty());
	ASSERT_FALSE(SkinSelection.empty());
	ASSERT_FALSE(SkinPartSelection.empty());
	EXPECT_EQ(Tee7.find("Buttons.VSplitLeft(220.0f, &QuickSearch, &Buttons);"), std::string::npos);
	EXPECT_NE(Tee7.find("Buttons.VSplitRight(120.0f, &QuickSearch, &SaveDeleteButton);"), std::string::npos);

	const size_t SkinPriority = SkinSelection.find("s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t SkinStart = SkinSelection.find("s_ListBox.DoStart(", SkinPriority);
	ASSERT_NE(SkinPriority, std::string::npos);
	ASSERT_NE(SkinStart, std::string::npos);
	EXPECT_LT(SkinPriority, SkinStart);
	EXPECT_NE(SkinSelection.find("SetScrollProfile(EQmScrollProfile::SETTINGS_GRID)"), std::string::npos);
	EXPECT_NE(SkinSelection.find("std::vector<std::string>"), std::string::npos);
	EXPECT_EQ(SkinSelection.find("std::vector<const CSkins7::CSkin *>"), std::string::npos);
	EXPECT_NE(SkinSelection.find("m_SkinList7LastRefreshTime.value() != RefreshTime"), std::string::npos);
	EXPECT_NE(SkinSelection.find("m_SkinList7LastRefreshTime = RefreshTime;"), std::string::npos);
	EXPECT_EQ(SkinSelection.find("m_SkinList7LastRefreshTime.value() != m_SkinList7LastRefreshTime"), std::string::npos);

	const size_t SkinPartPriority = SkinPartSelection.find("s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t SkinPartStart = SkinPartSelection.find("s_ListBox.DoStart(", SkinPartPriority);
	ASSERT_NE(SkinPartPriority, std::string::npos);
	ASSERT_NE(SkinPartStart, std::string::npos);
	EXPECT_LT(SkinPartPriority, SkinPartStart);
	EXPECT_NE(SkinPartSelection.find("SetScrollProfile(EQmScrollProfile::SETTINGS_GRID)"), std::string::npos);
	EXPECT_NE(SkinPartSelection.find("std::vector<std::string>"), std::string::npos);
	EXPECT_EQ(SkinPartSelection.find("std::vector<const CSkins7::CSkinPart *>"), std::string::npos);
	EXPECT_NE(SkinPartSelection.find("m_SkinPartsList7LastRefreshTime.value() != RefreshTime"), std::string::npos);
	EXPECT_NE(SkinPartSelection.find("m_SkinPartsList7LastRefreshTime = RefreshTime;"), std::string::npos);
	EXPECT_EQ(SkinPartSelection.find("m_SkinList7LastRefreshTime"), std::string::npos);
}

TEST(QmNewUiMenuBranches, CountryPopupOwnsWheelAndBlocksTheSettingsPage)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Popup = FunctionBody(Source, "CUi::EPopupMenuFunctionResult CMenus::PopupSettingsCountrySelection(void *pContext, CUIRect View, bool Active)");
	const std::string Identity = FunctionBody(Source, "void CMenus::RenderSettingsTeeIdentity(CUIRect MainView, CUIRect *pFlagButton, float BodySize)");
	const std::string MapPopup = FunctionBody(Source, "CUi::EPopupMenuFunctionResult CMenus::PopupMapPicker(void *pContext, CUIRect View, bool Active)");
	const std::string DDNet = FunctionBody(Source, "void CMenus::RenderSettingsDDNet(CUIRect MainView)");
	ASSERT_FALSE(Popup.empty());
	ASSERT_FALSE(Identity.empty());
	ASSERT_FALSE(MapPopup.empty());
	ASSERT_FALSE(DDNet.empty());
	EXPECT_NE(Popup.find("s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::POPUP);"), std::string::npos);
	EXPECT_NE(Popup.find("s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_GRID);"), std::string::npos);
	EXPECT_NE(Identity.find("PopupProps.m_BlockUnderlyingScroll = true;"), std::string::npos);
	EXPECT_NE(Identity.find("PopupSettingsCountrySelection, PopupProps"), std::string::npos);
	EXPECT_NE(MapPopup.find("s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::POPUP);"), std::string::npos);
	EXPECT_NE(MapPopup.find("s_ListBox.SetScrollProfile(EQmScrollProfile::POPUP_LIST);"), std::string::npos);
	const size_t MapPickerId = DDNet.find("s_PopupMapPickerId");
	ASSERT_NE(MapPickerId, std::string::npos);
	EXPECT_NE(DDNet.find("QmResolveDropdownPopupPolicy", MapPickerId), std::string::npos);
	EXPECT_NE(DDNet.find("CUi::PopupMenuContentInset()", MapPickerId), std::string::npos);
	EXPECT_NE(DDNet.find("PopupPolicy.m_PreferredHeight", MapPickerId), std::string::npos);
	EXPECT_NE(DDNet.find("PopupProps.m_BlockUnderlyingScroll = true;", MapPickerId), std::string::npos);
	EXPECT_NE(DDNet.find("PopupMapPicker, PopupProps", MapPickerId), std::string::npos);
}

TEST(QmNewUiMenuBranches, TeeOptionsMeasureAllRowsAndPlayerDummyChangeDisplayCycle)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Tee = FunctionBody(Source, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	const std::string Settings = FunctionBody(Source, "void CMenus::RenderSettings(CUIRect MainView)");
	ASSERT_FALSE(Tee.empty());
	ASSERT_FALSE(Settings.empty());
	EXPECT_NE(Tee.find("ResolveSettingsRowsHeight(4, ControlLineHeight, ControlSpacing)"), std::string::npos);
	EXPECT_NE(Tee.find("ResolveSettingsRowsHeight(6, ControlLineHeight, ControlSpacing)"), std::string::npos);
	EXPECT_NE(Tee.find("ResolveSettingsTeeCustomColorsLayout"), std::string::npos);
	EXPECT_NE(Tee.find("g_Config.m_QmSkinShowMetadata != 0"), std::string::npos);
	EXPECT_EQ(Tee.find("g_Config.m_QmSkinSortMode == 1 && g_Config.m_QmSkinShowMetadata"), std::string::npos);
	EXPECT_NE(Tee.find("SkinSortDropDownProps.m_FontSize = BodySize;"), std::string::npos);
	EXPECT_NE(Tee.find("const float SortLabelWidth = std::clamp(SortModeControl.w * 0.36f"), std::string::npos);
	EXPECT_NE(Tee.find("SortDropDown.VSplitLeft(ControlSpacing, nullptr, &SortDropDown);"), std::string::npos);
	EXPECT_EQ(Tee.find("settings_tee_skin_sort_dropdown"), std::string::npos);
	EXPECT_EQ(Tee.find("SkinSortDropDownProps.m_VisualStyle"), std::string::npos);
	EXPECT_NE(ConfigSource.find("\"Show skin release date and author\""), std::string::npos);
	EXPECT_EQ(ConfigSource.find("\"Show release date and author when sorted by date\""), std::string::npos);
	EXPECT_NE(Tee.find("const auto NextCheckboxRow"), std::string::npos);
	EXPECT_NE(Tee.find("const auto NextPrefixRow"), std::string::npos);
	EXPECT_NE(Tee.find("s_QueueListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);"), std::string::npos);
	EXPECT_NE(Tee.find("s_PresetListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);"), std::string::npos);
	const size_t DummyInput = Tee.find("s_TeeSubTab = 1;");
	const size_t DisplayCycle = Tee.find("const uint64_t TeeDisplayKey", DummyInput);
	ASSERT_NE(DummyInput, std::string::npos);
	ASSERT_NE(DisplayCycle, std::string::npos);
	EXPECT_LT(DummyInput, DisplayCycle);
	EXPECT_NE(Tee.find("m_SettingsCardDeckDisplayState.EnterView(TeeDisplayKey)", DisplayCycle), std::string::npos);
	EXPECT_NE(Settings.find("g_Config.m_UiSettingsPage != SETTINGS_TEE"), std::string::npos);
	EXPECT_EQ(Settings.find("m_Dummy + 1"), std::string::npos);
}

TEST(QmNewUiMenuBranches, SettingsSubTabPagesUseTheSharedLayoutContract)
{
	const std::string Settings = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Assets = ReadTextFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string TClient = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string QmClient = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	EXPECT_NE(FunctionBody(Settings, "void CMenus::RenderSettingsPlayer(CUIRect MainView)").find("ResolveSettingsSubTabLayout("), std::string::npos);
	EXPECT_NE(FunctionBody(Settings, "void CMenus::RenderSettingsTee(CUIRect MainView)").find("ResolveSettingsSubTabLayout("), std::string::npos);
	EXPECT_NE(FunctionBody(Settings, "void CMenus::RenderSettingsAppearance(CUIRect MainView)").find("ResolveSettingsSubTabLayout("), std::string::npos);
	EXPECT_NE(FunctionBody(Assets, "void CMenus::RenderSettingsCustom(CUIRect MainView)").find("ResolveSettingsSubTabLayout("), std::string::npos);
	EXPECT_NE(FunctionBody(TClient, "void CMenus::RenderSettingsTClient(CUIRect MainView, bool PrewarmOnly)").find("TClientSettingsContentView("), std::string::npos);
	EXPECT_NE(TClient.find("ResolveSettingsSubTabLayout(MainView, Metrics.m_UiScale)"), std::string::npos);
	EXPECT_NE(FunctionBody(QmClient, "void CMenus::RenderSettingsQmClientContent(CUIRect MainView, bool ContributorsPage, bool PrewarmOnly)").find("ResolveSettingsSubTabLayout("), std::string::npos);
}

TEST(QmNewUiMenuBranches, GraphicsAndSoundNestedListsOwnWheel)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Graphics = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	const std::string Sound = FunctionBody(Source, "void CMenus::RenderSettingsSound(CUIRect MainView)");
	ASSERT_FALSE(Graphics.empty());
	ASSERT_FALSE(Sound.empty());
	const size_t GraphicsPriority = Graphics.find("s_ListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t GraphicsStart = Graphics.find("s_ListBox.DoStart(RowHeightResList", GraphicsPriority);
	const size_t SoundPriority = Sound.find("s_AudioPackListBox.SetWheelOwnerPriority(EUiWheelOwnerPriority::COMPOSITE_CONTROL);");
	const size_t SoundStart = Sound.find("s_AudioPackListBox.DoStart(", SoundPriority);
	ASSERT_NE(GraphicsPriority, std::string::npos);
	ASSERT_NE(GraphicsStart, std::string::npos);
	ASSERT_NE(SoundPriority, std::string::npos);
	ASSERT_NE(SoundStart, std::string::npos);
	EXPECT_LT(GraphicsPriority, GraphicsStart);
	EXPECT_LT(SoundPriority, SoundStart);
	EXPECT_NE(Graphics.find("s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);"), std::string::npos);
	EXPECT_NE(Sound.find("s_AudioPackListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);"), std::string::npos);
	EXPECT_NE(Graphics.find("ResolveSettingsGraphicsModesGeometry("), std::string::npos);
	EXPECT_NE(Sound.find("ResolveSettingsSoundAudioPackGeometry("), std::string::npos);
	EXPECT_NE(Sound.find("ResolveSettingsSoundAudioPackGeometry(AudioPackCount, SoundMetrics)"), std::string::npos);
	EXPECT_NE(Sound.find("s_AudioPackListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED"), std::string::npos);
	EXPECT_NE(Sound.find("ResolveSettingsSoundLayoutRevision(RenderOnly, g_Config.m_SndEnable != 0, AudioPackCount)"), std::string::npos);
	EXPECT_NE(Graphics.find("const int GraphicsBackendRowCount"), std::string::npos);
	EXPECT_NE(Graphics.find("GraphicsDisplayRowCount = 5 + (Graphics()->GetNumScreens() > 1 ? 1 : 0) + GraphicsBackendRowCount"), std::string::npos);
	EXPECT_EQ(Graphics.find("const auto NextBackendRow"), std::string::npos);
	EXPECT_NE(Graphics.find("GraphicsModesMeasureRevision"), std::string::npos);
	EXPECT_NE(Graphics.find("s_ListBox.SetHideScrollbar(true);"), std::string::npos);
	EXPECT_NE(Graphics.find("s_ListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_HOVER);"), std::string::npos);
	EXPECT_EQ(Graphics.find("GraphicsBackendMinCardHeight = 104.0f"), std::string::npos);
	EXPECT_EQ(Graphics.find("Localize(\"Graphics card\"), 16.0f"), std::string::npos);
}

TEST(QmNewUiMenuBranches, GraphicsPilotHasNoRemainingLegacyInputOrScrollPath)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Graphics = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(Graphics.empty());
	EXPECT_NE(Graphics.find("SettingsPageLayout("), std::string::npos);
	EXPECT_NE(Graphics.find("SSettingsCardDefinition"), std::string::npos);
	EXPECT_NE(Graphics.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(Graphics.find("ui_widget::NumericField("), std::string::npos);
	EXPECT_NE(Graphics.find("QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(Graphics.find("CQmScrollState"), std::string::npos);
	EXPECT_NE(Graphics.find("deck:graphics-display"), std::string::npos);
	EXPECT_NE(Graphics.find("deck:graphics-visual"), std::string::npos);
	EXPECT_EQ(Graphics.find("deck:graphics-backend"), std::string::npos);
	EXPECT_NE(Graphics.find("deck:graphics-modes"), std::string::npos);
	EXPECT_EQ(Graphics.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(Graphics.find("DoSliderWithValueInput("), std::string::npos);
	EXPECT_EQ(Graphics.find("Ui()->DoScrollbarH("), std::string::npos);
	EXPECT_EQ(Graphics.find("Ui()->DoValueSelectorWithState("), std::string::npos);
	EXPECT_EQ(Graphics.find("s_GraphicsSettingsScrollRegion"), std::string::npos);
}

TEST(QmNewUiMenuBranches, DisplayModesHideOnlyTheirVisualScrollbar)
{
	const std::string ListBoxHeader = ReadTextFile("src/game/client/ui_listbox.h");
	const std::string ListBoxSource = ReadTextFile("src/game/client/ui_listbox.cpp");
	EXPECT_NE(ListBoxHeader.find("bool m_HideScrollbar;"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("void SetHideScrollbar(bool HideScrollbar)"), std::string::npos);
	EXPECT_NE(ListBoxSource.find("m_HideScrollbar = false;"), std::string::npos);
	EXPECT_NE(ListBoxSource.find("ScrollParams.m_HideScrollbar = m_HideScrollbar;"), std::string::npos);
}
TEST(QmNewUiMenuBranches, NestedLanguageListWheelOwnerOutranksGeneralPage)
{
	EXPECT_TRUE(QmHotScrollRegionPriorityWins(EUiWheelOwnerPriority::PAGE, EUiWheelOwnerPriority::COMPOSITE_CONTROL));
	EXPECT_FALSE(QmHotScrollRegionPriorityWins(EUiWheelOwnerPriority::COMPOSITE_CONTROL, EUiWheelOwnerPriority::PAGE));
	const std::string ScrollRegionSource = ReadTextFile("src/game/client/ui_scrollregion.cpp");
	EXPECT_NE(ScrollRegionSource.find("Ui()->SetHotScrollRegion(this, m_Params.m_WheelOwnerPriority);"), std::string::npos);

	CScrollWheelOwnership Ownership;
	int OuterOwner = 0;
	int InnerOwner = 0;
	ASSERT_TRUE(Ownership.BeginFrame(1, 1.0f, false));
	Ownership.Register(&OuterOwner, EUiWheelOwnerPriority::PAGE, true);
	Ownership.Register(&InnerOwner, EUiWheelOwnerPriority::COMPOSITE_CONTROL, true);
	float WheelDelta = 0.0f;
	EXPECT_FALSE(Ownership.TryConsume(&OuterOwner, &WheelDelta));
	EXPECT_TRUE(Ownership.TryConsume(&InnerOwner, &WheelDelta));
	EXPECT_EQ(WheelDelta, 1.0f);

	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string LanguageSelection = FunctionBody(Source, "bool CMenus::RenderLanguageSelection(CUIRect MainView, const SSettingsContentMetrics *pMetrics)");
	ASSERT_FALSE(LanguageSelection.empty());
	EXPECT_NE(LanguageSelection.find("ScrollParams.m_WheelOwnerPriority = EUiWheelOwnerPriority::COMPOSITE_CONTROL;"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ControlsControllerCardUsesDynamicHeightPreLayout)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings_controls.cpp");
	const size_t CardStart = Source.find("AddCard(vCards, \"deck:controls-controller\"");
	ASSERT_NE(CardStart, std::string::npos);
	const size_t CardEnd = Source.find("const std::pair<EBindOptionGroup", CardStart);
	ASSERT_NE(CardEnd, std::string::npos);
	const std::string CardBody = Source.substr(CardStart, CardEnd - CardStart);
	EXPECT_NE(CardBody.find("ControllerMeasureRevision"), std::string::npos);
	EXPECT_NE(CardBody.find("m_PreLayoutInput"), std::string::npos);
	EXPECT_NE(CardBody.find("const bool WasJoystickEnabled"), std::string::npos);
	EXPECT_NE(CardBody.find("ResolveSettingsRadioRowLayout(Content, 2, Metrics)"), std::string::npos);
	EXPECT_NE(CardBody.find("m_vJoystickIngameModeButtonContainers"), std::string::npos);
	EXPECT_NE(CardBody.find("if(!WasAbsolute)"), std::string::npos);
}

TEST(QmNewUiMenuBranches, ShutdownReleasesUiResourcesBeforeRendererProviders)
{
	const std::string NameplatesSource = ReadTextFile("src/game/client/components/nameplates.cpp");
	const std::string NameplatesHeader = ReadTextFile("src/game/client/components/nameplates.h");
	const std::string UiSource = ReadTextFile("src/game/client/ui.cpp");
	const std::string UiHeader = ReadTextFile("src/game/client/ui.h");
	const std::string GameClientSource = ReadTextFile("src/game/client/gameclient.cpp");

	const std::string NameplatesShutdown = FunctionBody(NameplatesSource, "void CNamePlates::OnShutdown()");
	const std::string NameplatesDestructor = FunctionBody(NameplatesSource, "CNamePlates::~CNamePlates()");
	ASSERT_FALSE(NameplatesShutdown.empty());
	ASSERT_FALSE(NameplatesDestructor.empty());
	EXPECT_NE(NameplatesHeader.find("void OnShutdown() override;"), std::string::npos);
	EXPECT_NE(NameplatesShutdown.find("ResetNamePlates();"), std::string::npos);
	EXPECT_NE(NameplatesShutdown.find("ResetChatBubbleAnimState(i, true);"), std::string::npos);
	EXPECT_EQ(NameplatesDestructor.find("ResetNamePlates"), std::string::npos);
	EXPECT_EQ(NameplatesDestructor.find("TextRender"), std::string::npos);

	const std::string UiShutdown = FunctionBody(UiSource, "void CUi::OnShutdown()");
	const std::string UiDestructor = FunctionBody(UiSource, "CUi::~CUi()");
	ASSERT_FALSE(UiShutdown.empty());
	ASSERT_FALSE(UiDestructor.empty());
	EXPECT_NE(UiHeader.find("void OnShutdown();"), std::string::npos);
	EXPECT_NE(UiShutdown.find("OnElementsReset();"), std::string::npos);
	EXPECT_NE(UiShutdown.find("if(m_pGraphics == nullptr || m_pTextRender == nullptr)"), std::string::npos);
	EXPECT_EQ(UiDestructor.find("Graphics()"), std::string::npos);
	EXPECT_EQ(UiDestructor.find("TextRender()"), std::string::npos);

	const std::string GameClientShutdown = FunctionBody(GameClientSource, "void CGameClient::OnShutdown()");
	ASSERT_FALSE(GameClientShutdown.empty());
	const size_t ComponentsShutdown = GameClientShutdown.find("pComponent->OnShutdown();");
	const size_t UiShutdownCall = GameClientShutdown.find("m_UI.OnShutdown();");
	ASSERT_NE(ComponentsShutdown, std::string::npos);
	ASSERT_NE(UiShutdownCall, std::string::npos);
	EXPECT_LT(ComponentsShutdown, UiShutdownCall);
}

TEST(QmNewUiMenuBranches, GraphicsIconCardSupportsDynamicCustomColorAndFourWeights)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Graphics = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(Graphics.empty());
	EXPECT_NE(Graphics.find("s_aGraphicsIconColorButtons[4]"), std::string::npos);
	EXPECT_NE(Graphics.find("s_aGraphicsIconWeightButtons[5]"), std::string::npos);
	EXPECT_NE(Graphics.find("Localize(\"Custom\")"), std::string::npos);
	EXPECT_NE(Graphics.find("Localize(\"Rainbow\")"), std::string::npos);
	// Thin 未随包字体，设置页不再提供该样式。
	EXPECT_EQ(Graphics.find("Localize(\"Thin\")"), std::string::npos);
	EXPECT_NE(Graphics.find("Localize(\"Fill\")"), std::string::npos);
	// 图标风格分段控件：索引 -> 配置值表必须唯一一份、由绘制与点击路径共用。
	// 历史上点击路径残留了含 Thin 的 6 项旧表，导致点击整体错位一位。
	EXPECT_NE(Source.find("constexpr int s_aIconWeightValues[] = {4, 0, 1, 3, 5};"), std::string::npos);
	EXPECT_EQ(Source.find("{2, 0, 1, 3, 4, 5}"), std::string::npos);
	EXPECT_NE(Source.find("QmIconWeightSegmentIndex(g_Config.m_QmUiIconWeight)"), std::string::npos);
	EXPECT_NE(Source.find("const int NewWeight = s_aIconWeightValues[NewValue];"), std::string::npos);
	EXPECT_NE(Graphics.find("DoLine_ColorPicker(&s_GraphicsIconCustomColorResetId"), std::string::npos);
	EXPECT_NE(Graphics.find("vCards.back().m_MeasureRevision = static_cast<uint64_t>(g_Config.m_QmUiIconColor == 3) |"), std::string::npos);
	EXPECT_NE(Graphics.find("vCards.back().m_PreLayoutInput = [this, GraphicsMetrics]"), std::string::npos);
	EXPECT_NE(Graphics.find("g_Config.m_QmUiIconColor == 3 && NormalizeQmIconWeight"), std::string::npos);
	EXPECT_NE(Graphics.find("g_Config.m_QmUiIconColor == 3 || NormalizeQmIconWeight"), std::string::npos);
	EXPECT_NE(Graphics.find("std::initializer_list<float>{GraphicsMetrics.m_LineHeight, GraphicsMetrics.m_ButtonHeight, GraphicsMetrics.m_LineHeight}"), std::string::npos);

	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	EXPECT_NE(Config.find("MACRO_CONFIG_COL(QmUiIconCustomColor, qm_ui_icon_custom_color"), std::string::npos);
	EXPECT_NE(Config.find("Qm UI icon color: 1=White, 2=Black, 3=Custom, 4=Rainbow"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_COL(QmUiIconDuotoneSecondaryColor, qm_ui_icon_duotone_secondary_color"), std::string::npos);
}

TEST(QmNewUiMenuBranches, OrdinaryUiRoundedSurfacesUseSharedPath)
{
	const std::string Appearance = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Effects = ReadTextFile("src/game/client/components/ui_effects.cpp");
	const std::string HudEditor = ReadTextFile("src/game/client/components/hud_editor.cpp");
	const std::string TClientMenus = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Chat = ReadTextFile("src/game/client/components/chat.cpp");

	EXPECT_NE(Appearance.find("DrawRoundedSurface(Ui(), MessageBackground"), std::string::npos);
	EXPECT_EQ(Appearance.find("Graphics()->DrawRectExt(PreviewView"), std::string::npos);
	EXPECT_NE(Effects.find("DrawRoundedSurface(Ui(), ShadowRect"), std::string::npos);
	EXPECT_EQ(Effects.find("Graphics()->DrawRect(PreviewX + ShadowOffset"), std::string::npos);
	EXPECT_NE(HudEditor.find("DrawRoundedSurface(Ui(), HelpRect"), std::string::npos);
	EXPECT_EQ(HudEditor.find("Graphics()->DrawRect(HelpX, HelpY"), std::string::npos);
	EXPECT_NE(TClientMenus.find("DrawRoundedSurface(Ui(), BodyColor"), std::string::npos);
	EXPECT_NE(TClientMenus.find("DrawRoundedSurface(Ui(), FeetColor"), std::string::npos);

	// 聊天滚动条和实时预览仍属于高频绘制，保留批量直绘路径。
	EXPECT_NE(Chat.find("Graphics()->DrawRect(ScrollbarRect.x"), std::string::npos);
	EXPECT_NE(Chat.find("Graphics()->DrawRect(x, PreviewY"), std::string::npos);
}

TEST(QmNewUiMenuBranches, LegacyRoundedRectDrawSitesRequireExplicitAllowlist)
{
	const char *const apAllowlistedFiles[] = {
		"src/game/client/components/hud.cpp",
		"src/game/client/components/chat.cpp",
		"src/game/client/components/nameplates.cpp",
		"src/game/client/components/spectator.cpp",
		"src/game/client/components/statboard.cpp",
	};
	for(const char *pPath : apAllowlistedFiles)
	{
		const std::string Source = ReadTextFile(pPath);
		EXPECT_GT(CountRoundedRectDirectCalls(Source), 0u) << pPath;
	}

	const char *const apOrdinaryUiFiles[] = {
		"src/game/client/components/menus.cpp",
		"src/game/client/components/menus_ingame.cpp",
		"src/game/client/components/menus_start.cpp",
		"src/game/client/components/menus_browser.cpp",
		"src/game/client/components/menus_demo.cpp",
		"src/game/client/components/menus_settings.cpp",
		"src/game/client/components/menus_settings7.cpp",
		"src/game/client/components/menus_settings_assets.cpp",
		"src/game/client/components/menus_settings_controls.cpp",
		"src/game/client/components/tclient/menus_tclient.cpp",
		"src/game/client/components/qmclient/menus_qmclient.cpp",
		"src/game/client/components/ui_effects.cpp",
		"src/game/client/components/hud_editor.cpp",
		"src/game/client/ui.cpp",
		"src/game/client/QmUi/UiButtons.cpp",
		"src/game/client/QmUi/UiForms.cpp",
		"src/game/client/QmUi/SettingsCard.cpp",
		"src/game/client/QmUi/SettingsCardDeck.cpp",
		"src/game/client/QmUi/UiContainers.h",
		"src/game/client/QmUi/UiOverlays.h",
	};
	for(const char *pPath : apOrdinaryUiFiles)
	{
		const std::string Source = ReadTextFile(pPath);
		EXPECT_EQ(CountRoundedRectDirectCalls(Source), 0u) << pPath;
	}
}

TEST(QmNewUiMenuBranches, RoundedSurfaceGeometryCoversUiScaleAndRetinaMatrix)
{
	constexpr float Aspect = 16.0f / 9.0f;
	const int aScales[] = {100, 125, 150, 200};
	const int aScreenWidths[] = {1920, 3840};
	const CUIRect Rect{0.2f, 0.2f, 20.0f, 10.0f};
	const int aCornerMasks[] = {
		IGraphics::CORNER_ALL,
		IGraphics::CORNER_L,
		IGraphics::CORNER_R,
		IGraphics::CORNER_T,
		IGraphics::CORNER_B,
		IGraphics::CORNER_TL,
		IGraphics::CORNER_TR,
		IGraphics::CORNER_BL,
		IGraphics::CORNER_BR,
		IGraphics::CORNER_NONE,
	};
	for(const int Scale : aScales)
	{
		const float VirtualHeight = QmUiVirtualScreenHeight(Scale);
		const float VirtualWidth = VirtualHeight * Aspect;
		for(const int ScreenWidth : aScreenWidths)
		{
			const float PixelSize = VirtualWidth / (float)ScreenWidth;
			ASSERT_GT(PixelSize, 0.0f);
			SRoundedSurfaceParams Params;
			Params.m_Radius = 8.0f;
			Params.m_PixelSize = PixelSize;
			const SRoundedSurfacePlan Plan = ResolveRoundedSurfacePlan(Rect, Params, true);
			EXPECT_TRUE(Plan.m_UseSdf);
			const auto IsPixelAligned = [PixelSize](const float Value) {
				return std::abs(Value - std::round(Value / PixelSize) * PixelSize) < 1e-3f;
			};
			EXPECT_TRUE(IsPixelAligned(Plan.m_Rect.x));
			EXPECT_TRUE(IsPixelAligned(Plan.m_Rect.y));
			EXPECT_TRUE(IsPixelAligned(Plan.m_Rect.w));
			EXPECT_TRUE(IsPixelAligned(Plan.m_Rect.h));
			EXPECT_FLOAT_EQ(Plan.m_Radius, std::min(Plan.m_Rect.w, Plan.m_Rect.h) * 0.5f);
			for(const int Corners : aCornerMasks)
			{
				const vec4 Radii = ResolveRoundedSurfaceCornerRadii(Plan.m_Radius, Corners);
				EXPECT_FLOAT_EQ(Radii.x, Corners & IGraphics::CORNER_TL ? Plan.m_Radius : 0.0f);
				EXPECT_FLOAT_EQ(Radii.y, Corners & IGraphics::CORNER_TR ? Plan.m_Radius : 0.0f);
				EXPECT_FLOAT_EQ(Radii.z, Corners & IGraphics::CORNER_BR ? Plan.m_Radius : 0.0f);
				EXPECT_FLOAT_EQ(Radii.w, Corners & IGraphics::CORNER_BL ? Plan.m_Radius : 0.0f);
			}
			const SRoundedSurfacePlan Fallback = ResolveRoundedSurfacePlan(Rect, Params, false);
			EXPECT_FALSE(Fallback.m_UseSdf);
			EXPECT_FLOAT_EQ(Fallback.m_Rect.x, Plan.m_Rect.x);
			EXPECT_FLOAT_EQ(Fallback.m_Rect.y, Plan.m_Rect.y);
			EXPECT_FLOAT_EQ(Fallback.m_Rect.w, Plan.m_Rect.w);
			EXPECT_FLOAT_EQ(Fallback.m_Rect.h, Plan.m_Rect.h);
			EXPECT_FLOAT_EQ(Fallback.m_Radius, Plan.m_Radius);
		}
	}
}

TEST(QmUiScale, VirtualHeightUsesClampedPercentage)
{
	EXPECT_FLOAT_EQ(QmUiVirtualScreenHeight(50), 1200.0f);
	EXPECT_FLOAT_EQ(QmUiVirtualScreenHeight(100), 600.0f);
	EXPECT_FLOAT_EQ(QmUiVirtualScreenHeight(200), 300.0f);
	EXPECT_FLOAT_EQ(QmUiVirtualScreenHeight(0), 1200.0f);
	EXPECT_FLOAT_EQ(QmUiVirtualScreenHeight(300), 300.0f);
}

TEST(QmUiScale, CenteredPopupMarginKeepsUsableContentAtTwoHundredPercent)
{
	const CUIRect DefaultScreen = {0.0f, 0.0f, 1066.0f, 600.0f};
	const CUIRect ScaledScreen = {0.0f, 0.0f, 533.0f, 300.0f};
	const CUIRect NarrowScaledScreen = {0.0f, 0.0f, 375.0f, 300.0f};

	EXPECT_FLOAT_EQ(QmUiCenteredMargin(DefaultScreen, 150.0f, 300.0f, 180.0f), 150.0f);
	EXPECT_FLOAT_EQ(QmUiCenteredMargin(ScaledScreen, 150.0f, 300.0f, 180.0f), 60.0f);
	EXPECT_FLOAT_EQ(QmUiCenteredMargin(NarrowScaledScreen, 150.0f, 300.0f, 180.0f), 37.5f);
	EXPECT_FLOAT_EQ(QmUiCenteredMargin({0.0f, 0.0f, 200.0f, 120.0f}, 150.0f, 300.0f, 180.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmUiCenteredMargin(ScaledScreen, 150.0f, 300.0f, 300.0f), 0.0f);
	EXPECT_EQ(QmUiVisibleRows(52.0f, 20.0f, 20.0f, 4, 4), 1);
	EXPECT_EQ(QmUiVisibleRows(126.0f, 20.0f, 20.0f, 8, 4), 4);
	EXPECT_EQ(QmUiVisibleRows(19.0f, 20.0f, 20.0f, 4, 4), 0);
}

TEST(QmNewUiMenuBranches, NameplateTextRasterizesAtStandardZoom)
{
	// 铭牌文字按官方行为在"标准缩放"的界面映射下栅格化：容器顶点固定在世界单位，
	// 绘制时随当前相机映射缩放。非 1.0 缩放下字形会被重采样，但所有铭牌一致，
	// 不会再出现"部分玩家清晰、部分玩家发虚"。
	const std::string Source = ReadTextFile("src/game/client/components/nameplates.cpp");
	EXPECT_NE(Source.find("This.Graphics()->MapScreenToGameInterface(This.m_Camera.m_Center.x, This.m_Camera.m_Center.y);"), std::string::npos);
	// 文本重建改由 CQmNameplateTextCache 判定，但缓存只能跳过「内容没变」的重建：
	// 容器失效时仍必须重建（官方行为），否则会永久停在隐藏态。
	EXPECT_NE(Source.find("if(!m_TextCache.NeedsUpdate(m_Visible, NeedsTextUpdate) && m_TextContainerIndex.Valid())"), std::string::npos);
	EXPECT_NE(Source.find("m_TextCache.Reset();"), std::string::npos);
	EXPECT_NE(ReadTextFile("src/game/client/components/qmclient/nameplate_text_cache.h").find("bool NeedsUpdate(bool Visible, bool Changed) const { return Visible && (Changed || !m_Updated); }"), std::string::npos);

	// 旧的"按真实屏幕映射密度栅格化 + 缩放停稳判定 + 每帧重建预算"机制必须整体移除：
	// 它只在"刚停稳那一帧"放行重建，且每帧只允许 64 个文本部件重建，
	// 没抢到预算的部件会永久停在旧密度上，表现为同一屏内部分铭牌长期发虚。
	EXPECT_EQ(Source.find("This.Graphics()->MapScreenToGameInterface(This.m_Camera.m_Center.x, This.m_Camera.m_Center.y, This.m_Camera.m_Zoom);"), std::string::npos);
	EXPECT_EQ(Source.find("QmNameplateTextNeedsRebake"), std::string::npos);
	EXPECT_EQ(Source.find("m_Rasterization"), std::string::npos);
	EXPECT_EQ(Source.find("m_ZoomStability"), std::string::npos);
	EXPECT_EQ(Source.find("NAMEPLATE_TEXT_REBUILD_BUDGET_PER_FRAME"), std::string::npos);
	EXPECT_EQ(Source.find("s_NameplateRasterizationDensity"), std::string::npos);

	const std::string Header = ReadTextFile("src/game/client/components/nameplates.h");
	EXPECT_EQ(Header.find("SQmNameplateTextRasterization"), std::string::npos);
	EXPECT_EQ(Header.find("SQmNameplateTextZoomStability"), std::string::npos);
	EXPECT_EQ(Header.find("QmNameplateTextRasterizationDensity"), std::string::npos);
	// HiDPI 物理像素对齐开关属于官方行为，保留。
	EXPECT_NE(Header.find("QmNameplateUsesPhysicalPixelAlignment"), std::string::npos);
}

TEST(QmSpectatorFriendPriority, StablePartitionPutsFriendsFirst)
{
	const bool aIsFriend[] = {false, true, false, true, false};
	int aOrder[5] = {};
	const int FriendCount = qm_spectator_friends::BuildFriendFirstOrder(aIsFriend, 5, aOrder);
	ASSERT_EQ(FriendCount, 2);
	// 好友保持原有相对顺序（原下标 1 在 3 之前），其余玩家同理（0、2、4）。
	EXPECT_EQ(aOrder[0], 1);
	EXPECT_EQ(aOrder[1], 3);
	EXPECT_EQ(aOrder[2], 0);
	EXPECT_EQ(aOrder[3], 2);
	EXPECT_EQ(aOrder[4], 4);
}

TEST(QmSpectatorFriendPriority, HandlesAllFriendsNoFriendsAndEmptyList)
{
	const bool aAllFriends[] = {true, true, true};
	int aOrder[3] = {};
	EXPECT_EQ(qm_spectator_friends::BuildFriendFirstOrder(aAllFriends, 3, aOrder), 3);
	for(int i = 0; i < 3; ++i)
		EXPECT_EQ(aOrder[i], i);

	const bool aNoFriends[] = {false, false, false};
	EXPECT_EQ(qm_spectator_friends::BuildFriendFirstOrder(aNoFriends, 3, aOrder), 0);
	for(int i = 0; i < 3; ++i)
		EXPECT_EQ(aOrder[i], i);

	// 空列表不写出任何下标。
	int Dummy = -1;
	EXPECT_EQ(qm_spectator_friends::BuildFriendFirstOrder(aNoFriends, 0, &Dummy), 0);
	EXPECT_EQ(Dummy, -1);
	EXPECT_EQ(qm_spectator_friends::BuildFriendFirstOrder(aNoFriends, -1, &Dummy), 0);
	EXPECT_EQ(Dummy, -1);
}

TEST(QmSpectatorFriendPriority, FullServerOrderIsAPermutationWithSingleBoundary)
{
	bool aIsFriend[MAX_CLIENTS] = {};
	// 每隔三个玩家取一个好友，覆盖满员（64 人）时的下标边界。
	for(int i = 0; i < MAX_CLIENTS; i += 3)
		aIsFriend[i] = true;

	int aOrder[MAX_CLIENTS] = {};
	const int FriendCount = qm_spectator_friends::BuildFriendFirstOrder(aIsFriend, MAX_CLIENTS, aOrder);
	int ExpectedFriends = 0;
	for(int i = 0; i < MAX_CLIENTS; ++i)
		ExpectedFriends += aIsFriend[i] ? 1 : 0;
	EXPECT_EQ(FriendCount, ExpectedFriends);

	bool aSeen[MAX_CLIENTS] = {};
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		ASSERT_GE(aOrder[i], 0);
		ASSERT_LT(aOrder[i], MAX_CLIENTS);
		EXPECT_FALSE(aSeen[aOrder[i]]);
		aSeen[aOrder[i]] = true;
		// 分区边界之前必须全是好友，之后必须全不是好友。
		EXPECT_EQ(aIsFriend[aOrder[i]], i < FriendCount);
	}
}

TEST(QmSpectatorFriendPriority, SpectatorHudRendersFriendsFirstWithOwnGroupTitles)
{
	const std::string Spectator = ReadTextFile("src/game/client/components/spectator.cpp");
	const std::string Body = FunctionBody(Spectator, "void CSpectator::OnRender()");

	// 显示顺序由纯函数算出，绘制循环按该顺序取玩家，先排序后绘制。
	const size_t BuildOrder = Body.find("qm_spectator_friends::BuildFriendFirstOrder(");
	const size_t DrawLoop = Body.find("apDisplayPlayers[aDisplayOrder[");
	ASSERT_NE(BuildOrder, std::string::npos);
	ASSERT_NE(DrawLoop, std::string::npos);
	EXPECT_LT(BuildOrder, DrawLoop);

	// 好友组与其余玩家各自带分组标题，边界只出现一次。
	EXPECT_NE(Body.find("Localize(\"Friends\")"), std::string::npos);
	EXPECT_NE(Body.find("Localize(\"Others\")"), std::string::npos);
	EXPECT_NE(Body.find("i == FriendCount"), std::string::npos);
	// 分组标题不占用网格槽位，避免满员时多出一列把玩家挤出面板：换行判定只保留玩家行那一处。
	const size_t FirstWrap = Body.find("if(Count == PerLine + 1");
	ASSERT_NE(FirstWrap, std::string::npos);
	EXPECT_EQ(Body.find("if(Count == PerLine + 1", FirstWrap + 1), std::string::npos);

	// 好友判定沿用快照里的 m_Friend 缓存（与爱心图标同源），渲染路径不查好友表。
	EXPECT_NE(Body.find(".m_Friend;"), std::string::npos);
	EXPECT_EQ(Body.find("Friends()->IsFriend("), std::string::npos);
	EXPECT_EQ(Body.find("Foes()->IsFriend("), std::string::npos);
}

TEST(QmNewUiMenuBranches, FriendHeartsUseSolidHeartGlyph)
{
	// 意图：图标字体 Phosphor 只有中空心形（U+E2A8 为 2 轮廓），好友界面的爱心改用默认字体
	// DejaVu Sans 的实体心形 U+2665；调用点必须走共享常量，并把字体预设切回默认字体。
	EXPECT_STREQ(QM_FRIEND_HEART_ICON, "\xE2\x99\xA5");

	const std::string Header = ReadTextFile("src/game/client/components/qmclient/friend_heart_icon.h");
	EXPECT_NE(Header.find("QM_FRIEND_HEART_ICON"), std::string::npos);
	// 常量用 U+2665 的 UTF-8 字节，且不再引用图标字体的中空心形码位。
	EXPECT_NE(Header.find("\xE2\x99\xA5"), std::string::npos);
	EXPECT_EQ(Header.find("\xEE\x8A\xA8"), std::string::npos);

	const std::string Browser = ReadTextFile("src/game/client/components/menus_browser.cpp");
	EXPECT_NE(Browser.find("#include <game/client/components/qmclient/friend_heart_icon.h>"), std::string::npos);
	// 好友列表表头：默认字体的实体爱心。
	EXPECT_NE(Browser.find("Ui()->DoLabel(&Col.m_Rect, QM_FRIEND_HEART_ICON, 14.0f, TEXTALIGN_MC);"), std::string::npos);
	EXPECT_EQ(Browser.find("Ui()->DoLabel(&Col.m_Rect, FONT_ICON_HEART, 14.0f, TEXTALIGN_MC);"), std::string::npos);
	// 服务器列表好友图标：绘制辅助函数支持逐次指定字体预设，心形以外的图标继续用图标字体。
	EXPECT_NE(Browser.find("const char *pText, int TextAlign, bool SmallFont = false, EFontPreset FontPreset = EFontPreset::ICON_FONT)"), std::string::npos);
	EXPECT_NE(Browser.find("QM_FRIEND_HEART_ICON, TEXTALIGN_MC, false, EFontPreset::DEFAULT_FONT);"), std::string::npos);
	EXPECT_NE(Browser.find("FONT_ICON_STAR, TEXTALIGN_MC);"), std::string::npos);

	// 工具箱好友页签：两套 UI 分支都在画好友页签前切到默认字体，切换后不再切回图标字体。
	const std::string TabBar = FunctionBody(Browser, "void CMenus::RenderServerbrowserTabBar(CUIRect TabBar)");
	ASSERT_FALSE(TabBar.empty());
	size_t FriendsTabCount = 0;
	for(size_t Pos = TabBar.find("DoButton_MenuTab(&s_FriendsTabButton, QM_FRIEND_HEART_ICON"); Pos != std::string::npos;
		Pos = TabBar.find("DoButton_MenuTab(&s_FriendsTabButton, QM_FRIEND_HEART_ICON", Pos + 1))
	{
		const size_t SwitchPos = TabBar.rfind("TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);", Pos);
		ASSERT_NE(SwitchPos, std::string::npos);
		EXPECT_EQ(TabBar.substr(SwitchPos, Pos - SwitchPos).find("SetFontPreset(EFontPreset::ICON_FONT"), std::string::npos);
		++FriendsTabCount;
	}
	EXPECT_EQ(FriendsTabCount, 2);
	EXPECT_EQ(TabBar.find("DoButton_MenuTab(&s_FriendsTabButton, FONT_ICON_HEART"), std::string::npos);

	const std::string Scoreboard = ReadTextFile("src/game/client/components/scoreboard.cpp");
	EXPECT_NE(Scoreboard.find("#include <game/client/components/qmclient/friend_heart_icon.h>"), std::string::npos);
	// 未加好友态用实体爱心；已是好友悬停保留图标字体的中空裂心（图标字体没有实心裂心）。
	EXPECT_NE(Scoreboard.find("? FontIcons::FONT_ICON_HEART_CRACK : QM_FRIEND_HEART_ICON;"), std::string::npos);
	EXPECT_EQ(Scoreboard.find(": FontIcons::FONT_ICON_HEART;"), std::string::npos);
}
