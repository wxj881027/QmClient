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
