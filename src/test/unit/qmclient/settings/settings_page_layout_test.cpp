#include <engine/shared/config.h>

#include <game/client/QmUi/QmAnim.h>
#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsCardDeck.h>
#include <game/client/QmUi/SettingsCardDeckLogic.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/cards/QmCardCatalogSkinMetrics.h>
#include <game/client/QmUi/cards/QmConsoleSettingsLayout.h>
#include <game/client/ui.h>

#include <gtest/gtest.h>

TEST(SettingsPageLayout, UpdateContentFitsShortViewportAtLargeScale)
{
	const CUIRect Content{20, 60, 420, 148};
	const float Scale = ui_widget::ResolveUpdatePanelContentScale(Content, 2.0f);
	EXPECT_GT(Scale, 0.0f);
	EXPECT_LT(Scale, 1.0f);
	// 版本与状态在标题栏小字；单行操作与下载进度之外，仍可显示至少两行发布说明。
	const float BodySize = ui_token::font::BODY * Scale;
	const float Gap = ui_token::spacing::MD * Scale;
	const float Actions = BodySize * ui_token::secondary_panel::UPDATE_ACTION_LINE_FACTOR + Gap;
	const float Progress = BodySize * 1.7f + std::max(4.0f, Gap) + Gap;
	EXPECT_GE(Content.h - Actions - Progress, 2.0f * BodySize);
}

TEST(SettingsPageLayout, UpdateContentKeepsRequestedScaleWhenSpaceAllows)
{
	EXPECT_FLOAT_EQ(ui_widget::ResolveUpdatePanelContentScale({0, 0, 600, 350}, 1.0f), 1.0f);
	EXPECT_FLOAT_EQ(ui_widget::ResolveUpdatePanelContentScale({0, 0, 600, 350}, 0.5f), 0.5f);
}

TEST(SettingsPageLayout, UpdateSecondaryPanelFollowsViewportHeight)
{
	const CUIRect Viewport{0, 0, 1200, 600};
	const CUIRect Panel = ResolveSettingsSecondaryPanelRect(Viewport, ESettingsSecondaryPanelKind::UPDATE, 1.0f);
	// 更新面板与公告面板一致占满可用高度，不再使用固定扁窗高度。
	EXPECT_FLOAT_EQ(Panel.h, Viewport.h - 4.0f * ui_token::spacing::LG);
	EXPECT_FLOAT_EQ(Panel.y, (Viewport.h - Panel.h) * 0.5f);
	EXPECT_FLOAT_EQ(Panel.w, std::min(ui_token::secondary_panel::UPDATE_WIDTH, Viewport.w - 2.0f * std::min(ui_token::spacing::LG, Viewport.w * 0.04f)));
}

#include <array>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

TEST(SettingsCardDeckScroll, BackgroundKeepsTheLiveOffsetWhileMeasurementStartsAtOrigin)
{
	EXPECT_FLOAT_EQ(ResolveSettingsCardDeckPassiveScrollOffset(true, true, -180.0f), -180.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsCardDeckPassiveScrollOffset(false, true, -180.0f), 0.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsCardDeckPassiveScrollOffset(true, false, -180.0f), 0.0f);
	EXPECT_FLOAT_EQ(ResolveSettingsCardDeckPassiveScrollOffset(true, true, -40.0f), -40.0f);
}

TEST(SettingsPageLayout, ReleaseViewportMatrixKeepsCardsAndNavigationInsideTheWindow)
{
	constexpr std::array<std::array<int, 2>, 9> aResolutions = {{{1280, 720}, {1366, 768}, {1920, 1080}, {2560, 1440}, {3840, 2160}, {1280, 1024}, {1600, 1200}, {1920, 1200}, {2560, 1080}}};
	for(const auto &Resolution : aResolutions)
		for(const int Scale : {100, 125, 150, 200})
		{
			SCOPED_TRACE(std::to_string(Resolution[0]) + "x" + std::to_string(Resolution[1]) + " scale=" + std::to_string(Scale));
			const float Height = QmUiVirtualScreenHeight(Scale);
			const CUIRect Available{10.0f, 40.0f, Height * Resolution[0] / Resolution[1] - 20.0f, Height - 50.0f};
			const auto Layout = ResolveSettingsShellLayout(Available, 20.0f);
			const auto ExpectInside = [](const CUIRect &Inner, const CUIRect &Outer) {
				EXPECT_GE(Inner.w, 0.0f);
				EXPECT_GE(Inner.h, 0.0f);
				EXPECT_GE(Inner.x + 0.001f, Outer.x);
				EXPECT_GE(Inner.y + 0.001f, Outer.y);
				EXPECT_LE(Inner.x + Inner.w, Outer.x + Outer.w + 0.001f);
				EXPECT_LE(Inner.y + Inner.h, Outer.y + Outer.h + 0.001f);
			};
			ExpectInside(Layout.m_TabBarRect, Available);
			ExpectInside(Layout.m_ContentPanelRect, Available);
			ExpectInside(Layout.m_RestartBarRect, Available);
			ExpectInside(Layout.m_ScrollViewport, Layout.m_ContentPanelRect);
			ExpectInside(Layout.m_aColumns[0], Layout.m_ScrollViewport);
			EXPECT_LE(Layout.m_ContentPanelRect.x + Layout.m_ContentPanelRect.w, Layout.m_TabBarRect.x);
			if(Layout.m_TwoColumns)
			{
				ExpectInside(Layout.m_aColumns[1], Layout.m_ScrollViewport);
				EXPECT_GE(Layout.m_aColumns[0].w, 360.0f);
				EXPECT_GE(Layout.m_aColumns[1].w, 360.0f);
				EXPECT_LE(Layout.m_aColumns[0].x + Layout.m_aColumns[0].w, Layout.m_aColumns[1].x);
			}
		}
}

TEST(SettingsPageLayout, LargeUiUsesOneReadableColumnInsteadOfShrinkingTwo)
{
	const auto LayoutAtScale = [](int Scale) {
		const float Height = QmUiVirtualScreenHeight(Scale);
		return ResolveSettingsShellLayout({0.0f, 0.0f, Height * 16.0f / 9.0f, Height});
	};
	EXPECT_TRUE(LayoutAtScale(100).m_TwoColumns);
	for(const int Scale : {125, 150, 200})
	{
		SCOPED_TRACE(Scale);
		const auto Layout = LayoutAtScale(Scale);
		EXPECT_FALSE(Layout.m_TwoColumns);
		EXPECT_FLOAT_EQ(Layout.m_aColumns[0].w, Layout.m_ScrollViewport.w);
	}
}

TEST(SettingsPageLayout, DynamicVisualCardHeightsUseSharedMetrics)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	EXPECT_FLOAT_EQ(ResolveQmVisualWeaponAnimationHeight(Metrics, false, false), 2.0f * Metrics.m_RowStep + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(ResolveQmVisualWeaponAnimationHeight(Metrics, false, true), 4.0f * Metrics.m_RowStep + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(ResolveQmVisualWeaponAnimationHeight(Metrics, true, false), 7.0f * Metrics.m_RowStep + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(ResolveQmVisualWeaponAnimationHeight(Metrics, true, true), 8.0f * Metrics.m_RowStep + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(ResolveQmVisualCollisionHitboxHeight(Metrics, false), Metrics.m_RowStep);
	EXPECT_FLOAT_EQ(ResolveQmVisualCollisionHitboxHeight(Metrics, true), 17.0f * Metrics.m_RowStep);
	EXPECT_FLOAT_EQ(ResolveQmVisualSkinTransitionHeight(Metrics, true) - ResolveQmVisualSkinTransitionHeight(Metrics, false), 5.0f * Metrics.m_RowStep);
	EXPECT_FLOAT_EQ(ResolveQmVisualSkinTransitionHeight(Metrics, false), 2.0f * Metrics.m_RowStep);
}

TEST(SettingsPageLayout, GeneralDynamicCameraConsumesNoHiddenRowWhenCollapsed)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	const float Collapsed = ResolveSettingsGeneralGameContentHeight(Metrics, false);
	const float Expanded = ResolveSettingsGeneralGameContentHeight(Metrics, true);
	EXPECT_FLOAT_EQ(Collapsed, ResolveSettingsRowsHeight(3, Metrics.m_LineHeight, Metrics.m_LineSpacing));
	EXPECT_FLOAT_EQ(Expanded, ResolveSettingsRowsHeight(4, Metrics.m_LineHeight, Metrics.m_LineSpacing));
	EXPECT_FLOAT_EQ(Expanded - Collapsed, Metrics.m_RowStep);
}

TEST(SettingsPageLayout, SegmentedControlsKeepReadableSizeInNarrowAndWideContent)
{
	for(const float Width : {240.0f, 600.0f, 960.0f})
	{
		SCOPED_TRACE(Width);
		const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(Width);
		const CUIRect View{10.0f, 20.0f, Width, 100.0f};
		const SSettingsRadioRowLayout Row = ResolveSettingsRadioRowLayout(View, 4, Metrics);
		EXPECT_GE(ResolveSettingsRadioFontSize(Metrics), 12.0f);
		EXPECT_FLOAT_EQ(Row.m_ButtonsRect.h, Metrics.m_ButtonHeight);
		EXPECT_LE(Row.m_ButtonsRect.y + Row.m_ButtonsRect.h, View.y + Row.m_Height);
		EXPECT_LE(Row.m_ButtonsRect.x + Row.m_ButtonsRect.w, View.x + View.w);
		EXPECT_FLOAT_EQ(Row.m_ButtonsRect.y, Row.m_LabelRect.y);
		EXPECT_FLOAT_EQ(Row.m_Height, Metrics.m_LineHeight);
		const SSettingsNestedRadioRowLayout Nested = ResolveSettingsNestedRadioRowLayout(View, Metrics);
		EXPECT_EQ(Nested.m_ContainerRect.h, Row.m_ButtonsRect.h);
		EXPECT_FLOAT_EQ(Nested.m_ContainerRect.y + Nested.m_ContainerRect.h, View.y + Nested.m_Height);
	}
}

TEST(SettingsPageLayout, DynamicIslandHeightMatchesTheRenderedRowsAndColorRow)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	const float OriginalHeight = ResolveQmHudDynamicIslandHeight(Metrics, true, false, 700.0f);
	const float ExpandedHeight = ResolveQmHudDynamicIslandHeight(Metrics, false, false, 700.0f);
	const CUIRect ColorRowView{0.0f, 0.0f, 700.0f, 0.0f};

	// 卡片预留 4 行：原版样式、显示队伍、钩子倒计时、开关倒计时。
	// 展开时再多一行背景色和一行始终显示歌曲信息开关。
	EXPECT_FLOAT_EQ(OriginalHeight, 4.0f * Metrics.m_RowStep);
	EXPECT_FLOAT_EQ(ExpandedHeight - OriginalHeight, ResolveSettingsColorRowLayout(ColorRowView, Metrics, false).m_ConsumedHeight + Metrics.m_RowStep);
	// 开关倒计时启用后再多出「跟随 Tee」「灵动岛」两个位置开关（没有位置标题行）。
	EXPECT_FLOAT_EQ(ResolveQmHudDynamicIslandHeight(Metrics, true, true, 700.0f) - OriginalHeight, 2.0f * Metrics.m_RowStep);
	EXPECT_FLOAT_EQ(ResolveQmHudDynamicIslandHeight(Metrics, false, true, 700.0f) - ExpandedHeight, 2.0f * Metrics.m_RowStep);
}

TEST(SettingsPageLayout, ContentRowFlowKeepsConditionalRowsAndMeasuredHeightInSync)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	CUIRect Content{0.0f, 0.0f, 700.0f, 1000.0f};
	CSettingsContentRowFlow Rows(Content, Metrics);
	const CUIRect ColorChoice = Rows.NextLine();
	const CUIRect CustomColor = Rows.NextButton();
	const CUIRect WeightChoice = Rows.NextLine();

	EXPECT_FLOAT_EQ(CustomColor.y, ColorChoice.y + ColorChoice.h + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(WeightChoice.y, CustomColor.y + CustomColor.h + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(1000.0f - Content.h, ResolveSettingsContentFlowHeight(Metrics, {Metrics.m_LineHeight, Metrics.m_ButtonHeight, Metrics.m_LineHeight}));
}

TEST(SettingsPageLayout, ConditionalRowsShareVisibilityBetweenFlowAndMeasurement)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	CUIRect Content{0.0f, 0.0f, 700.0f, 1000.0f};
	CSettingsContentRowFlow Rows(Content, Metrics);
	const CUIRect First = Rows.NextLine();
	const CUIRect Hidden = Rows.NextIf(false, Metrics.m_ButtonHeight);
	const CUIRect Last = Rows.NextButton();
	EXPECT_FLOAT_EQ(Hidden.w, 0.0f);
	EXPECT_FLOAT_EQ(Last.y, First.y + First.h + Metrics.m_LineSpacing);
	const float WithoutOptional = ResolveSettingsContentFlowHeight(Metrics, {MakeSettingsContentFlowEntry(Metrics.m_LineHeight),
											MakeSettingsContentFlowEntry(Metrics.m_ButtonHeight, false),
											MakeSettingsContentFlowEntry(Metrics.m_ButtonHeight)});
	const float WithOptional = ResolveSettingsContentFlowHeight(Metrics, {MakeSettingsContentFlowEntry(Metrics.m_LineHeight),
										     MakeSettingsContentFlowEntry(Metrics.m_ButtonHeight, true),
										     MakeSettingsContentFlowEntry(Metrics.m_ButtonHeight)});
	EXPECT_FLOAT_EQ(WithoutOptional, Metrics.m_LineHeight + Metrics.m_LineSpacing + Metrics.m_ButtonHeight);
	EXPECT_FLOAT_EQ(WithOptional - WithoutOptional, Metrics.m_ButtonHeight + Metrics.m_LineSpacing);
}

TEST(SettingsPageLayout, AutoRowHeightUsesNaturalTextHeight)
{
	EXPECT_FLOAT_EQ(24.0f, ResolveSettingsAutoRowHeight(24.0f, 12.0f));
	EXPECT_FLOAT_EQ(36.0f, ResolveSettingsAutoRowHeight(24.0f, 36.0f));
	EXPECT_FLOAT_EQ(24.0f, ResolveSettingsAutoRowHeight(24.0f, -4.0f));
}

TEST(SettingsPageLayout, WrappedHelpReservesMeasuredHeightBeforeFollowingControl)
{
	CUIRect Content{20.0f, 40.0f, 400.0f, 200.0f};
	int Measurements = 0;
	const CUIRect Help = ConsumeSettingsWrappedTextRow(Content, 150.0f, 10.0f, 4.0f, [&](float Width) {
		++Measurements;
		EXPECT_FLOAT_EQ(Width, 280.0f);
		return 35.0f;
	});
	EXPECT_EQ(Measurements, 1);
	EXPECT_FLOAT_EQ(Help.x, 140.0f);
	EXPECT_FLOAT_EQ(Help.w, 280.0f);
	EXPECT_FLOAT_EQ(Help.h, 35.0f);
	EXPECT_FLOAT_EQ(Content.y, Help.y + Help.h + 4.0f);
	EXPECT_FLOAT_EQ(Content.y + Content.h, 240.0f);
}

TEST(SettingsPageLayout, WrappedHelpKeepsReadableWidthInNarrowCards)
{
	CUIRect Content{0.0f, 0.0f, 100.0f, 200.0f};
	const CUIRect Help = ConsumeSettingsWrappedTextRow(Content, 150.0f, 10.0f, 2.0f, [](float Width) {
		EXPECT_FLOAT_EQ(Width, 70.0f);
		return 60.0f;
	});
	EXPECT_FLOAT_EQ(Help.w, 70.0f);
	EXPECT_FLOAT_EQ(Content.y, 62.0f);
	EXPECT_FLOAT_EQ(Content.h, 138.0f);
}

TEST(SettingsPageLayout, WrappedHelpHandlesEmptyMeasurementsAndDegenerateWidth)
{
	CUIRect Content{0.0f, 0.0f, 0.0f, 100.0f};
	const CUIRect Help = ConsumeSettingsWrappedTextRow(Content, -5.0f, 10.0f, -2.0f, [](float Width) {
		EXPECT_FLOAT_EQ(Width, 1.0f);
		return 0.0f;
	});
	EXPECT_FLOAT_EQ(Help.x, 0.0f);
	EXPECT_FLOAT_EQ(Help.h, 10.0f);
	EXPECT_FLOAT_EQ(Content.y, 10.0f);
}

TEST(SettingsPageLayout, ColumnFlowUsesLongestColumnAndPreservesBottom)
{
	CUIRect Content{0.0f, 0.0f, 400.0f, 500.0f};
	Content.HSplitTop(20.0f, nullptr, &Content);
	CUIRect LeftColumn = Content;
	CUIRect RightColumn = Content;
	LeftColumn.HSplitTop(180.0f, nullptr, &LeftColumn);
	RightColumn.HSplitTop(320.0f, nullptr, &RightColumn);

	CommitSettingsColumnContentFlow(Content, LeftColumn, RightColumn);

	EXPECT_FLOAT_EQ(340.0f, Content.y);
	EXPECT_FLOAT_EQ(160.0f, Content.h);
}

TEST(SettingsPageLayout, SecondaryPanelFitsNarrowAndWideViewportsAndStaysCentered)
{
	for(const CUIRect Viewport : {CUIRect{10.0f, 20.0f, 400.0f, 300.0f}, CUIRect{0.0f, 0.0f, 1200.0f, 800.0f}})
	{
		const CUIRect Panel = ResolveSettingsSecondaryPanelRect(Viewport);
		EXPECT_LE(Panel.w, Viewport.w);
		EXPECT_LE(Panel.h, Viewport.h);
		EXPECT_LE(Panel.w, 780.0f);
		EXPECT_LE(Panel.h, 470.0f);
		EXPECT_FLOAT_EQ(Panel.x + Panel.w * 0.5f, Viewport.x + Viewport.w * 0.5f);
		EXPECT_FLOAT_EQ(Panel.y + Panel.h * 0.5f, Viewport.y + Viewport.h * 0.5f);
	}
}

TEST(SettingsPageLayout, SecondaryHeaderKeepsCloseSquareAndTitleOutsideItsHitRegion)
{
	for(const CUIRect Row : {CUIRect{5.0f, 10.0f, 200.0f, 20.0f}, CUIRect{5.0f, 10.0f, 12.0f, 20.0f}})
	{
		const auto Header = ui_widget::ResolveSecondaryPanelHeaderLayout(Row, 4.0f);
		EXPECT_FLOAT_EQ(Header.m_Close.w, Header.m_Close.h);
		EXPECT_GE(Header.m_Title.w, 0.0f);
		EXPECT_LE(Header.m_Title.x + Header.m_Title.w, Header.m_Close.x);
		EXPECT_LE(Header.m_Close.x + Header.m_Close.w, Row.x + Row.w);
		EXPECT_GE(Header.m_Close.y, Row.y);
		EXPECT_LE(Header.m_Close.y + Header.m_Close.h, Row.y + Row.h);
	}
}

TEST(SettingsPageLayout, SecondaryMetricsReserveTheVisibleDividerAndNotice)
{
	const auto Metrics = ui_widget::ResolveSecondaryPanelMetrics(1000.0f);
	EXPECT_GT(Metrics.m_DividerHeight, 0.0f);
	EXPECT_FLOAT_EQ(Metrics.ContentHeight(2, 3, 1) - Metrics.ContentHeight(2, 3, 0), Metrics.m_Spacing + Metrics.m_RowHeight);
	EXPECT_GT(Metrics.ContentHeight(0, 0, 0), 2.0f * Metrics.m_Margin + Metrics.m_TitleHeight);
}

TEST(SettingsPageLayout, SecondaryPanelPropertiesRequestCenteredModalPresentation)
{
	const SPopupMenuProperties Props = ui_widget::SecondaryPanelProperties();
	EXPECT_TRUE(Props.m_CenterInViewport);
	EXPECT_TRUE(Props.m_BlockUnderlyingPointerInput);
	EXPECT_TRUE(Props.m_BlockUnderlyingScroll);
	EXPECT_TRUE(Props.m_Animate);
}

TEST(SettingsPageLayout, ConfigEntriesAlignEditorAndResetAcrossDescriptionLengths)
{
	const auto Metrics = ResolveSettingsContentMetrics(800.0f);
	const CUIRect Area{20, 30, 700, 500};
	const auto Short = ResolveSettingsConfigEntryLayout(Area, Metrics, false, 60, 12);
	const auto Long = ResolveSettingsConfigEntryLayout(Area, Metrics, false, 60, 60);
	EXPECT_FALSE(Short.m_Stacked);
	EXPECT_FALSE(Long.m_Stacked);
	EXPECT_FLOAT_EQ(Short.m_Editor.x, Long.m_Editor.x);
	EXPECT_FLOAT_EQ(Short.m_Reset.x, Long.m_Reset.x);
	EXPECT_FLOAT_EQ(Short.m_Editor.w, Long.m_Editor.w);
	EXPECT_GT(Long.m_Height, Short.m_Height);
	EXPECT_LE(Long.m_Description.y + Long.m_Description.h, Area.y + Long.m_Height);
	EXPECT_LT(Long.m_Description.x + Long.m_Description.w, Long.m_Editor.x);
	EXPECT_LT(Long.m_Editor.x + Long.m_Editor.w, Long.m_Reset.x);
}

TEST(SettingsPageLayout, NarrowConfigEntryPlacesControlsBelowTheDescription)
{
	const auto Metrics = ResolveSettingsContentMetrics(800.0f);
	const CUIRect Area{20, 30, 220, 500};
	const auto Layout = ResolveSettingsConfigEntryLayout(Area, Metrics, false, 60, 48);
	EXPECT_TRUE(Layout.m_Stacked);
	EXPECT_FLOAT_EQ(Layout.m_Name.x, Layout.m_Editor.x);
	EXPECT_GT(Layout.m_Editor.y, Layout.m_Description.y + Layout.m_Description.h);
	EXPECT_FLOAT_EQ(Layout.m_Editor.y, Layout.m_Reset.y);
	EXPECT_LE(Layout.m_Reset.x + Layout.m_Reset.w, Area.x + Area.w);
	EXPECT_LE(Layout.m_Reset.y + Layout.m_Reset.h, Area.y + Layout.m_Height);
}

TEST(SettingsPageLayout, CompactConfigEntryRemovesDescriptionSpaceAndRestoresFullHeight)
{
	const auto Metrics = ResolveSettingsContentMetrics(800.0f);
	for(float Width : {220.0f, 700.0f})
	{
		SCOPED_TRACE(Width);
		const CUIRect Area{0, 0, Width, 500};
		const auto Full = ResolveSettingsConfigEntryLayout(Area, Metrics, false, 60, 48);
		const auto Compact = ResolveSettingsConfigEntryLayout(Area, Metrics, true, 60, 48);
		const auto Restored = ResolveSettingsConfigEntryLayout(Area, Metrics, false, 60, 48);
		EXPECT_LT(Compact.m_Height, Full.m_Height);
		EXPECT_FLOAT_EQ(Compact.m_Description.h, 0);
		EXPECT_FLOAT_EQ(Compact.m_Reset.x, Full.m_Reset.x);
		EXPECT_FLOAT_EQ(Restored.m_Height, Full.m_Height);
	}
}

TEST(SettingsPageLayout, ConfigEntryKeepsControlsInsideTinyWidths)
{
	const auto Metrics = ResolveSettingsContentMetrics(800.0f);
	for(float Width : {0.0f, 5.0f, 40.0f, 180.0f})
	{
		SCOPED_TRACE(Width);
		const CUIRect Area{20, 30, Width, 500};
		const auto Layout = ResolveSettingsConfigEntryLayout(Area, Metrics, false, 120, 48);
		for(const CUIRect Rect : {Layout.m_Name, Layout.m_Description, Layout.m_Editor, Layout.m_Reset})
		{
			EXPECT_GE(Rect.w, 0);
			EXPECT_GE(Rect.x, Area.x);
			EXPECT_LE(Rect.x + Rect.w, Area.x + Area.w);
			EXPECT_LE(Rect.y + Rect.h, Area.y + Layout.m_Height);
		}
	}
}

TEST(SettingsPageLayout, GpuControlsHideWithoutMultipleAdapters)
{
	for(const size_t Count : {size_t{0}, size_t{1}})
		for(const bool CanSelect : {false, true})
			for(const bool HasSystemSettings : {false, true})
			{
				const auto Control = ResolveSettingsGpuControl(Count, CanSelect, HasSystemSettings);
				EXPECT_FALSE(Control.m_ShowSelector);
				EXPECT_FALSE(Control.m_ShowSystemSettings);
				EXPECT_EQ(Control.RowCount(), 0);
			}
}

TEST(SettingsPageLayout, SelectableMultipleAdaptersAlsoOfferAvailableSystemSettings)
{
	for(const bool HasSystemSettings : {false, true})
	{
		const auto Control = ResolveSettingsGpuControl(2, true, HasSystemSettings);
		EXPECT_TRUE(Control.m_ShowSelector);
		EXPECT_EQ(Control.m_ShowSystemSettings, HasSystemSettings);
		EXPECT_EQ(Control.RowCount(), HasSystemSettings ? 2 : 1);
	}
}

TEST(SettingsPageLayout, SystemManagedMultipleAdaptersOnlyOfferSupportedSystemSettings)
{
	const auto Windows = ResolveSettingsGpuControl(2, false, true);
	EXPECT_FALSE(Windows.m_ShowSelector);
	EXPECT_TRUE(Windows.m_ShowSystemSettings);
	EXPECT_EQ(Windows.RowCount(), 1);
	const auto WithoutSystemSettings = ResolveSettingsGpuControl(2, false, false);
	EXPECT_FALSE(WithoutSystemSettings.m_ShowSelector);
	EXPECT_FALSE(WithoutSystemSettings.m_ShowSystemSettings);
	EXPECT_EQ(WithoutSystemSettings.RowCount(), 0);
}

TEST(SettingsPageLayout, GpuControlsRecoverAcrossAdapterAndBackendChanges)
{
	auto Control = ResolveSettingsGpuControl(2, false, true);
	ASSERT_TRUE(Control.m_ShowSystemSettings);
	Control = ResolveSettingsGpuControl(1, false, true);
	EXPECT_EQ(Control.RowCount(), 0);
	Control = ResolveSettingsGpuControl(2, true, true);
	EXPECT_TRUE(Control.m_ShowSelector);
	EXPECT_EQ(Control.RowCount(), 2);
	Control = ResolveSettingsGpuControl(0, false, true);
	EXPECT_EQ(Control.RowCount(), 0);
}

TEST(SettingsPageLayout, RuntimeGraphicsInfoSeparatesVulkanDriverFromActualApi)
{
	const auto Info = ResolveSettingsGraphicsRuntimeInfo("Vulkan 1.4.341 (driver: 610.88.0.0)", "Vulkan", 1, 4, 0);
	EXPECT_EQ(Info.m_Backend, "Vulkan 1.4.341");
	EXPECT_EQ(Info.m_Driver, "610.88.0.0");
}

TEST(SettingsPageLayout, RuntimeGraphicsInfoSeparatesOpenGlAndGlesDriverDescriptions)
{
	const auto Gl = ResolveSettingsGraphicsRuntimeInfo("4.6.0 NVIDIA 610.88", "OpenGL", 4, 6, 0);
	EXPECT_EQ(Gl.m_Backend, "OpenGL 4.6.0");
	EXPECT_EQ(Gl.m_Driver, "NVIDIA 610.88");
	const auto Es = ResolveSettingsGraphicsRuntimeInfo("OpenGL ES 3.2 Mesa 24.0", "GLES", 3, 2, 0);
	EXPECT_EQ(Es.m_Backend, "GLES 3.2.0");
	EXPECT_EQ(Es.m_Driver, "Mesa 24.0");
}

TEST(SettingsPageLayout, RuntimeGraphicsInfoNeverInventsUnavailableDriverVersions)
{
	const auto Metal = ResolveSettingsGraphicsRuntimeInfo("Metal native", "", 0, 0, 0);
	EXPECT_EQ(Metal.m_Backend, "Metal native");
	EXPECT_TRUE(Metal.m_Driver.empty());
	const auto Gl = ResolveSettingsGraphicsRuntimeInfo("4.6", "OpenGL", 4, 6, 0);
	EXPECT_EQ(Gl.m_Backend, "OpenGL 4.6.0");
	EXPECT_TRUE(Gl.m_Driver.empty());
	const auto Empty = ResolveSettingsGraphicsRuntimeInfo(nullptr, nullptr, 0, 0, 0);
	EXPECT_TRUE(Empty.m_Backend.empty());
	EXPECT_TRUE(Empty.m_Driver.empty());
}

TEST(QmConsoleSettingsPanelLayout, PresetFitsContentAndCustomExpandsWithinScreen)
{
	const CUIRect Screen = {20.0f, 10.0f, 1000.0f, 1000.0f};
	const auto Preset = QmConsoleSettingsLayout::PanelRect(Screen, false, 10);
	const auto Custom = QmConsoleSettingsLayout::PanelRect(Screen, true, 10);
	EXPECT_GT(Custom.h, Preset.h);
	EXPECT_FLOAT_EQ(Custom.w, Preset.w);
	EXPECT_LT(Preset.w, Screen.w * 0.5f);
	EXPECT_FLOAT_EQ(Preset.x + Preset.w * 0.5f, Screen.x + Screen.w * 0.5f);
	EXPECT_FLOAT_EQ(Custom.y + Custom.h * 0.5f, Screen.y + Screen.h * 0.5f);
}

TEST(QmConsoleSettingsPanelLayout, SmallScreensConstrainCustomContentForScrolling)
{
	for(const CUIRect Screen : {CUIRect{0, 0, 320, 200}, CUIRect{10, 20, 640, 360}})
	{
		SCOPED_TRACE(Screen.w);
		const auto Panel = QmConsoleSettingsLayout::PanelRect(Screen, true, 24);
		EXPECT_LE(Panel.w, Screen.w);
		EXPECT_LE(Panel.h, Screen.h);
		EXPECT_GE(Panel.x, Screen.x);
		EXPECT_GE(Panel.y, Screen.y);
		const auto Metrics = ResolveSettingsContentMetrics(Panel.w);
		EXPECT_GT(QmConsoleSettingsLayout::ContentHeight(Metrics, true, 24), Panel.h);
	}
}

TEST(QmConsoleSettingsPanelLayout, LargerPreviewFontExpandsPanelAndResetRestoresSize)
{
	const CUIRect Screen = {0, 0, 1000, 1000};
	const auto Initial = QmConsoleSettingsLayout::PanelRect(Screen, false, 10);
	const auto Enlarged = QmConsoleSettingsLayout::PanelRect(Screen, false, 24);
	const auto Restored = QmConsoleSettingsLayout::PanelRect(Screen, false, 10);
	EXPECT_GT(Enlarged.h, Initial.h);
	EXPECT_FLOAT_EQ(Restored.h, Initial.h);
}

TEST(QmConsoleSettingsPanelLayout, EmptyViewportNeverProducesNegativeGeometry)
{
	const auto Panel = QmConsoleSettingsLayout::PanelRect({10, 20, 0, 0}, true, 24);
	EXPECT_FLOAT_EQ(Panel.w, 0.0f);
	EXPECT_FLOAT_EQ(Panel.h, 0.0f);
}

TEST(QmConsoleSettingsPanelLayout, ActivePopupWheelOwnerWinsAndConsumesOnlyOnce)
{
	CScrollWheelOwnership Ownership;
	int Page = 0;
	int Popup = 0;
	Ownership.BeginFrame(1, 1.0f, false);
	Ownership.Register(&Page, EUiWheelOwnerPriority::PAGE, true);
	Ownership.Register(&Popup, EUiWheelOwnerPriority::POPUP, true);
	float Delta = 0.0f;
	EXPECT_FALSE(Ownership.TryConsume(&Page, &Delta));
	EXPECT_TRUE(Ownership.TryConsume(&Popup, &Delta));
	EXPECT_FLOAT_EQ(Delta, 1.0f);
	EXPECT_FALSE(Ownership.TryConsume(&Popup, &Delta));
	Ownership.BeginFrame(2, -1.0f, false);
	Ownership.Register(&Popup, EUiWheelOwnerPriority::POPUP, false);
	EXPECT_FALSE(Ownership.TryConsume(&Popup, &Delta));
	Ownership.BeginFrame(3, -1.0f, false);
	Ownership.Register(&Popup, EUiWheelOwnerPriority::POPUP, true);
	EXPECT_TRUE(Ownership.TryConsume(&Popup, &Delta));
	EXPECT_FLOAT_EQ(Delta, -1.0f);
}

TEST(SettingsPageLayout, LegacyRnnoiseModeDoesNotReserveFallbackRows)
{
	for(const float Width : {420.0f, 700.0f, 1000.0f})
	{
		SCOPED_TRACE(Width);
		const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(Width);
		const float DisabledHeight = ResolveQmHudVoiceHeight(Metrics, true, true, true, 0, true, true);
		EXPECT_FLOAT_EQ(ResolveQmHudVoiceHeight(Metrics, true, true, true, 2, true, true), DisabledHeight);
		EXPECT_FLOAT_EQ(ResolveQmHudVoiceHeight(Metrics, true, true, true, -1, true, true), DisabledHeight);
		EXPECT_FLOAT_EQ(ResolveQmHudVoiceHeight(Metrics, true, true, true, 99, true, true), DisabledHeight);
		EXPECT_GT(ResolveQmHudVoiceHeight(Metrics, true, true, true, 1, true, true), DisabledHeight);
	}
}

TEST(SettingsPageLayout, PairedControlsPreservePrimarySpaceAndSupportInPlaceSplitting)
{
	const auto Metrics = ResolveSettingsContentMetrics(1000);
	for(const float Width : {0.0f, 1.0f, 24.0f, 150.0f, 500.0f})
	{
		SCOPED_TRACE(Width);
		CUIRect Primary{40, 60, Width, Metrics.m_LineHeight};
		CUIRect Secondary;
		SplitSettingsPairedControls(Primary, Metrics, &Primary, &Secondary);
		EXPECT_GE(Primary.w, Width * 0.4f);
		EXPECT_GE(Secondary.w, 0.0f);
		EXPECT_GE(Secondary.x, Primary.x + Primary.w);
		EXPECT_NEAR(Secondary.x + Secondary.w, 40.0f + Width, 0.0001f);
		EXPECT_FLOAT_EQ(Primary.h, Metrics.m_LineHeight);
		EXPECT_FLOAT_EQ(Secondary.h, Primary.h);
	}
}
