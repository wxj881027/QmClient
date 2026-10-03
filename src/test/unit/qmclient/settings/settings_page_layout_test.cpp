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

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

TEST(SettingsPageLayout, DynamicVisualCardHeightsUseSharedMetrics)
{
	const SSettingsContentMetrics Metrics = ResolveSettingsContentMetrics(1000.0f);
	EXPECT_FLOAT_EQ(ResolveQmVisualWeaponAnimationHeight(Metrics, false, false), 2.0f * Metrics.m_RowStep + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(ResolveQmVisualWeaponAnimationHeight(Metrics, false, true), 4.0f * Metrics.m_RowStep + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(ResolveQmVisualWeaponAnimationHeight(Metrics, true, false), 7.0f * Metrics.m_RowStep + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(ResolveQmVisualWeaponAnimationHeight(Metrics, true, true), 8.0f * Metrics.m_RowStep + Metrics.m_LineSpacing);
	EXPECT_FLOAT_EQ(ResolveQmVisualCollisionHitboxHeight(Metrics, false), Metrics.m_RowStep);
	EXPECT_FLOAT_EQ(ResolveQmVisualCollisionHitboxHeight(Metrics, true), 16.0f * Metrics.m_RowStep);
	EXPECT_FLOAT_EQ(ResolveQmVisualFocusModeHeight(Metrics), 16.0f * Metrics.m_RowStep + 3.0f * (Metrics.m_SmallSize + Metrics.m_LineSpacing) + Metrics.m_LineSpacing);
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
		EXPECT_GE(Row.m_ButtonsRect.h, 24.0f);
		EXPECT_LE(Row.m_ButtonsRect.y + Row.m_ButtonsRect.h, View.y + Row.m_Height);
		EXPECT_LE(Row.m_ButtonsRect.x + Row.m_ButtonsRect.w, View.x + View.w);
		if(Row.m_Stacked)
			EXPECT_GE(Row.m_ButtonsRect.y, Row.m_LabelRect.y + Row.m_LabelRect.h + Metrics.m_LineSpacing);
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

	// 卡片固定渲染 4 行：使用原版样式、显示队伍、钩子倒计时、开关倒计时总开关。
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

TEST(SettingsPageLayout, AlphaColorRoundTripUpdatesColorAndOpacityTogether)
{
	const unsigned int SourceColor = ColorHSLA(0.31f, 0.72f, 0.44f, 1.0f).Pack(false);
	const unsigned int Packed = PackSettingsAlphaColor(SourceColor, 37);
	unsigned int UpdatedColor = 0;
	int UpdatedOpacity = 0;
	UnpackSettingsAlphaColor(Packed, UpdatedColor, UpdatedOpacity);

	const ColorHSLA Expected(SourceColor);
	const ColorHSLA Actual(UpdatedColor);
	EXPECT_NEAR(Actual.h, Expected.h, 1.0f / 255.0f);
	EXPECT_NEAR(Actual.s, Expected.s, 1.0f / 255.0f);
	EXPECT_NEAR(Actual.l, Expected.l, 1.0f / 255.0f);
	EXPECT_NEAR(UpdatedOpacity, 37, 1);
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
