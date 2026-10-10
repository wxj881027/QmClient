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

TEST(QmConsoleUiLayout, CategoryLabelStaysCenteredWithIndicatorSpaceAtDifferentScales)
{
	for(const float Scale : {0.5f, 1.0f, 2.0f})
	{
		SCOPED_TRACE(Scale);
		const CUIRect Button = {40.0f, 15.0f, 120.0f * Scale, 28.0f * Scale};
		const auto Layout = QmConsoleUi::LayoutButton(Button, 14.0f * Scale);
		EXPECT_FLOAT_EQ(Layout.m_Label.x + Layout.m_Label.w * 0.5f, Button.x + Button.w * 0.5f);
		EXPECT_FLOAT_EQ(Layout.m_Label.y + Layout.m_Label.h * 0.5f, Button.y + Button.h * 0.5f);
		EXPECT_GE(Layout.m_Label.x, Layout.m_FilterIndicator.x + Layout.m_FilterIndicator.w);
		EXPECT_GT(Layout.m_Label.w, 0.0f);
	}
}

TEST(QmConsoleUiLayout, ButtonWithoutIndicatorUsesWholeRectForLabel)
{
	const CUIRect Button = {40.0f, 15.0f, 120.0f, 28.0f};
	const auto Layout = QmConsoleUi::LayoutButton(Button, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_Label.x, Button.x);
	EXPECT_FLOAT_EQ(Layout.m_Label.y, Button.y);
	EXPECT_FLOAT_EQ(Layout.m_Label.w, Button.w);
	EXPECT_FLOAT_EQ(Layout.m_Label.h, Button.h);
	EXPECT_FLOAT_EQ(Layout.m_FilterIndicator.w, 0.0f);
}

TEST(QmConsoleUiLayout, NarrowButtonKeepsCenteredLabelAndIndicatorInsideButton)
{
	const CUIRect Button = {40.0f, 15.0f, 12.0f, 28.0f};
	const auto Layout = QmConsoleUi::LayoutButton(Button, 14.0f);
	EXPECT_GE(Layout.m_Label.w, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_Label.x + Layout.m_Label.w * 0.5f, Button.x + Button.w * 0.5f);
	EXPECT_GE(Layout.m_FilterIndicator.x, Button.x);
	EXPECT_LE(Layout.m_FilterIndicator.x + Layout.m_FilterIndicator.w, Layout.m_Label.x);
	EXPECT_LE(Layout.m_Label.x + Layout.m_Label.w, Button.x + Button.w);
}
