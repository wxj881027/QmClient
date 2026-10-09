#include <game/client/QmUi/QmConsoleUi.h>

#include <gtest/gtest.h>

TEST(QmConsoleUiLayout, WideToolbarKeepsFiltersAndActionsOnOneRow)
{
	const auto Layout = QmConsoleUi::LayoutToolbar(1000.0f, 240.0f, 320.0f);
	EXPECT_FALSE(Layout.m_SplitRows);
	EXPECT_FLOAT_EQ(Layout.m_Height, 26.0f);
	EXPECT_FLOAT_EQ(Layout.m_FilterScale, 1.0f);
	EXPECT_FLOAT_EQ(Layout.m_ActionScale, 1.0f);
	EXPECT_GE(Layout.m_ActionX, 10.0f + 240.0f + 10.0f);
	EXPECT_LE(Layout.m_ActionX + 320.0f, 990.0f);
}

TEST(QmConsoleUiLayout, NarrowToolbarSeparatesActionsFromFilters)
{
	const auto Layout = QmConsoleUi::LayoutToolbar(500.0f, 240.0f, 320.0f);
	EXPECT_TRUE(Layout.m_SplitRows);
	EXPECT_FLOAT_EQ(Layout.m_Height, 52.0f);
	EXPECT_FLOAT_EQ(Layout.m_FilterScale, 1.0f);
	EXPECT_FLOAT_EQ(Layout.m_ActionScale, 1.0f);
	EXPECT_LE(Layout.m_ActionX + 320.0f, 490.0f);
}

TEST(QmConsoleUiLayout, LongTranslationsFitBothRowsWithinViewport)
{
	const auto Layout = QmConsoleUi::LayoutToolbar(300.0f, 480.0f, 640.0f);
	EXPECT_TRUE(Layout.m_SplitRows);
	EXPECT_GT(Layout.m_FilterScale, 0.0f);
	EXPECT_GT(Layout.m_ActionScale, 0.0f);
	EXPECT_LE(10.0f + 480.0f * Layout.m_FilterScale, 290.0f);
	EXPECT_LE(Layout.m_ActionX + 640.0f * Layout.m_ActionScale, 290.0f);
}

TEST(QmConsoleUiLayout, ExportActionsUseOneRowWhenFiltersAreHidden)
{
	const auto Layout = QmConsoleUi::LayoutToolbar(500.0f, 0.0f, 320.0f);
	EXPECT_FALSE(Layout.m_SplitRows);
	EXPECT_FLOAT_EQ(Layout.m_Height, 26.0f);
	EXPECT_LE(Layout.m_ActionX + 320.0f, 490.0f);
}

TEST(QmConsoleUiLayout, HiddenFiltersNeverReserveAnEmptyToolbarRow)
{
	const auto Layout = QmConsoleUi::LayoutToolbar(300.0f, 0.0f, 640.0f);
	EXPECT_FALSE(Layout.m_SplitRows);
	EXPECT_FLOAT_EQ(Layout.m_Height, 26.0f);
	EXPECT_LE(Layout.m_ActionX + 640.0f * Layout.m_ActionScale, 290.0f);
}

TEST(QmConsoleUiLayout, LargerFontsReserveTallerRowsWhenControlsWrap)
{
	const auto Layout = QmConsoleUi::LayoutToolbar(500.0f, 360.0f, 400.0f, 40.0f);
	EXPECT_TRUE(Layout.m_SplitRows);
	EXPECT_FLOAT_EQ(Layout.m_Height, 80.0f);
	EXPECT_LE(Layout.m_ActionX + 400.0f * Layout.m_ActionScale, 490.0f);
}

TEST(QmConsoleUiLayout, TextOnlyButtonCentersLabelWithoutReservingIconSlot)
{
	const CUIRect Button = {20.0f, 3.0f, 120.0f, 24.0f};
	const auto Content = QmConsoleUi::LayoutButtonContent(Button, 40.0f, 12.0f, 0.0f);
	EXPECT_FLOAT_EQ(Content.m_Icon.w, 0.0f);
	EXPECT_FLOAT_EQ(Content.m_Label.x + Content.m_Label.w * 0.5f, Button.x + Button.w * 0.5f);
	EXPECT_FLOAT_EQ(Content.m_Label.w, 40.0f);
}

TEST(QmConsoleUiLayout, IconAndLabelCenterTogetherWithVisibleGap)
{
	const CUIRect Button = {20.0f, 3.0f, 120.0f, 24.0f};
	const auto Content = QmConsoleUi::LayoutButtonContent(Button, 40.0f, 12.0f, 16.0f);
	EXPECT_GT(Content.m_Icon.w, 0.0f);
	EXPECT_GT(Content.m_Label.x, Content.m_Icon.x + Content.m_Icon.w);
	EXPECT_NEAR((Content.m_Icon.x + Content.m_Label.x + Content.m_Label.w) * 0.5f, Button.x + Button.w * 0.5f, 0.001f);
	EXPECT_FLOAT_EQ(Content.m_Icon.y + Content.m_Icon.h * 0.5f, Button.y + Button.h * 0.5f);
}

TEST(QmConsoleUiLayout, LongLabelAndIconFitNarrowButtons)
{
	for(const float Width : {0.0f, 1.0f, 12.0f, 40.0f, 80.0f})
	{
		SCOPED_TRACE(Width);
		const CUIRect Button = {10.0f, 3.0f, Width, 20.0f};
		const auto Content = QmConsoleUi::LayoutButtonContent(Button, 500.0f, 14.0f, 18.0f);
		EXPECT_GE(Content.m_Icon.x, Button.x);
		EXPECT_GE(Content.m_Label.w, 0.0f);
		EXPECT_LE(Content.m_Label.x + Content.m_Label.w, Button.x + Button.w);
		EXPECT_LE(Content.m_Icon.h, Button.h);
		EXPECT_NEAR((Content.m_Icon.x + Content.m_Label.x + Content.m_Label.w) * 0.5f, Button.x + Button.w * 0.5f, 0.001f);
	}
}

TEST(QmConsoleUiLayout, SelectionBackgroundPadsGlyphsAndJoinsAdjacentRows)
{
	const auto First = QmConsoleUi::LayoutSelectionBackground({10.0f, 20.0f, 40.0f, 12.0f}, 12.0f);
	const auto Next = QmConsoleUi::LayoutSelectionBackground({10.0f, 32.0f, 40.0f, 12.0f}, 12.0f);
	EXPECT_LT(First.x, 10.0f);
	EXPECT_GT(First.x + First.w, 50.0f);
	EXPECT_FLOAT_EQ(First.y, 20.0f);
	EXPECT_FLOAT_EQ(First.y + First.h, Next.y);
	EXPECT_GT(First.h, 0.0f);
}

TEST(QmConsoleUiLayout, EmptySelectionDoesNotBecomeVisiblePadding)
{
	for(const IGraphics::CQuadItem Quad : {IGraphics::CQuadItem(0.0f, 0.0f, 0.0f, 12.0f), IGraphics::CQuadItem(0.0f, 0.0f, 5.0f, 0.0f)})
	{
		const auto Background = QmConsoleUi::LayoutSelectionBackground(Quad, 12.0f);
		EXPECT_FLOAT_EQ(Background.w, 0.0f);
		EXPECT_FLOAT_EQ(Background.h, 0.0f);
	}
}

TEST(QmConsoleUiLayout, OnePointTwoHighExportCheckboxStaysInsideItsEntry)
{
	const CUIRect Entry = {10.0f, 20.0f, 200.0f, 2.0f};
	const auto Checkbox = QmConsoleUi::LayoutExportCheckbox(Entry);
	EXPECT_GT(Checkbox.w, 0.0f);
	EXPECT_FLOAT_EQ(Checkbox.w, Checkbox.h);
	EXPECT_LT(Checkbox.h, Entry.h);
	EXPECT_GT(Checkbox.y, Entry.y);
	EXPECT_LT(Checkbox.y + Checkbox.h, Entry.y + Entry.h);
	EXPECT_FLOAT_EQ(Checkbox.x, Entry.x + 5.0f);
	EXPECT_FLOAT_EQ(Checkbox.y + Checkbox.h * 0.5f, Entry.y + Entry.h * 0.5f);
	EXPECT_TRUE(Entry.Inside(Checkbox.TopLeft()));
	EXPECT_TRUE(Entry.Inside(vec2(Checkbox.x + Checkbox.w, Checkbox.y + Checkbox.h)));
}

TEST(QmConsoleUiLayout, NormalTenPointExportCheckboxKeepsItsPreferredSize)
{
	for(const float Height : {11.0f, 12.0f})
	{
		SCOPED_TRACE(Height);
		const CUIRect Entry = {0.0f, 20.0f, 200.0f, Height};
		const auto Checkbox = QmConsoleUi::LayoutExportCheckbox(Entry);
		EXPECT_NEAR(Checkbox.h, 11.0f, 0.5f);
		EXPECT_FLOAT_EQ(Checkbox.w, Checkbox.h);
		EXPECT_FLOAT_EQ(Checkbox.x, 5.0f);
		EXPECT_FLOAT_EQ(Checkbox.y + Checkbox.h * 0.5f, Entry.y + Entry.h * 0.5f);
		EXPECT_GT(Checkbox.y, Entry.y);
		EXPECT_LT(Checkbox.y + Checkbox.h, Entry.y + Entry.h);
	}
}

TEST(QmConsoleUiLayout, MultilineExportCheckboxCentersWithoutGrowingPastEleven)
{
	for(const float Height : {4.0f, 8.0f, 33.0f})
	{
		SCOPED_TRACE(Height);
		const CUIRect Entry = {10.0f, 20.0f, 200.0f, Height};
		const auto Checkbox = QmConsoleUi::LayoutExportCheckbox(Entry);
		EXPECT_GT(Checkbox.h, 0.0f);
		EXPECT_LE(Checkbox.h, 11.0f);
		EXPECT_FLOAT_EQ(Checkbox.w, Checkbox.h);
		EXPECT_FLOAT_EQ(Checkbox.y + Checkbox.h * 0.5f, Entry.y + Entry.h * 0.5f);
		EXPECT_GT(Checkbox.y, Entry.y);
		EXPECT_LT(Checkbox.y + Checkbox.h, Entry.y + Entry.h);
	}
}

TEST(QmConsoleUiLayout, ConsecutiveExportCheckboxClicksBelongOnlyToTheirOwnEntries)
{
	const CUIRect aEntries[] = {
		{0.0f, 20.0f, 200.0f, 2.0f},
		{0.0f, 22.0f, 200.0f, 2.0f},
		{0.0f, 24.0f, 200.0f, 6.0f},
		{0.0f, 30.0f, 200.0f, 11.0f},
		{0.0f, 41.0f, 200.0f, 33.0f},
	};
	for(const auto &Entry : aEntries)
	{
		SCOPED_TRACE(Entry.y);
		const auto Checkbox = QmConsoleUi::LayoutExportCheckbox(Entry);
		for(const float Fraction : {0.0f, 0.5f, 1.0f})
		{
			SCOPED_TRACE(Fraction);
			const vec2 Click(Checkbox.x + Checkbox.w * 0.5f, Checkbox.y + Checkbox.h * Fraction);
			EXPECT_TRUE(Entry.Inside(Click));
			for(const auto &OtherEntry : aEntries)
			{
				if(&OtherEntry != &Entry)
					EXPECT_FALSE(OtherEntry.Inside(Click));
			}
		}
	}
}

TEST(QmConsoleUiLayout, NarrowExportCheckboxRemainsSquareAndInsideEntryWidth)
{
	for(const float Width : {1.0f, 8.0f, 16.0f})
	{
		SCOPED_TRACE(Width);
		const CUIRect Entry = {10.0f, 20.0f, Width, 33.0f};
		const auto Checkbox = QmConsoleUi::LayoutExportCheckbox(Entry);
		EXPECT_GT(Checkbox.w, 0.0f);
		EXPECT_FLOAT_EQ(Checkbox.w, Checkbox.h);
		EXPECT_GE(Checkbox.x, Entry.x);
		EXPECT_LE(Checkbox.x + Checkbox.w, Entry.x + Entry.w);
		EXPECT_LE(Checkbox.w, 11.0f);
	}
}

TEST(QmConsoleUiLayout, EmptyExportEntryProducesNoVisibleCheckbox)
{
	for(const CUIRect Entry : {CUIRect{10.0f, 20.0f, 200.0f, 0.0f}, CUIRect{10.0f, 20.0f, 200.0f, -2.0f}, CUIRect{10.0f, 20.0f, 0.0f, 11.0f}, CUIRect{10.0f, 20.0f, -2.0f, 11.0f}})
	{
		SCOPED_TRACE(Entry.w);
		SCOPED_TRACE(Entry.h);
		const auto Checkbox = QmConsoleUi::LayoutExportCheckbox(Entry);
		EXPECT_FLOAT_EQ(Checkbox.w, 0.0f);
		EXPECT_FLOAT_EQ(Checkbox.h, 0.0f);
	}
}

TEST(QmConsoleUiLayout, LogBottomLeavesReadableSeparatorGapAtEveryFontSize)
{
	for(float FontSize : {1.0f, 8.0f, 10.0f, 24.0f})
	{
		SCOPED_TRACE(FontSize);
		EXPECT_LE(QmConsoleUi::LogBottomBeforeSeparator(100.0f, FontSize), 97.0f);
	}
}

TEST(QmConsoleUiLayout, FilterDisableMovesGraduallyAndRedirectsWithoutJump)
{
	const int PreviousMotion = g_Config.m_QmUiMotionLevel;
	g_Config.m_QmUiMotionLevel = 2;
	CQmAnimationBackend Runtime;
	EXPECT_FLOAT_EQ(QmConsoleUi::ResolveFilterDisabled(&Runtime, 17, true), 0.0f);
	EXPECT_FLOAT_EQ(QmConsoleUi::ResolveFilterDisabled(&Runtime, 17, false), 0.0f);
	Runtime.Advance(0.06f);
	const float Middle = QmConsoleUi::ResolveFilterDisabled(&Runtime, 17, false);
	EXPECT_GT(Middle, 0.0f);
	EXPECT_LT(Middle, 1.0f);
	EXPECT_FLOAT_EQ(QmConsoleUi::ResolveFilterDisabled(&Runtime, 17, true), Middle);
	Runtime.Advance(0.2f);
	EXPECT_NEAR(QmConsoleUi::ResolveFilterDisabled(&Runtime, 17, true), 0.0f, 0.001f);
	g_Config.m_QmUiMotionLevel = PreviousMotion;
}

TEST(QmConsoleUiLayout, FilterWithoutMotionOrRuntimeUsesFinalState)
{
	EXPECT_FLOAT_EQ(QmConsoleUi::ResolveFilterDisabled(nullptr, 17, false), 1.0f);
	const int PreviousMotion = g_Config.m_QmUiMotionLevel;
	g_Config.m_QmUiMotionLevel = 0;
	CQmAnimationBackend Runtime;
	QmConsoleUi::ResolveFilterDisabled(&Runtime, 19, true);
	EXPECT_FLOAT_EQ(QmConsoleUi::ResolveFilterDisabled(&Runtime, 19, false), 1.0f);
	g_Config.m_QmUiMotionLevel = PreviousMotion;
}
