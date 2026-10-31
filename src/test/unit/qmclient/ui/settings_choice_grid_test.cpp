#include <game/client/QmUi/SettingsChoiceGrid.h>

#include <gtest/gtest.h>

#include <limits>

TEST(SettingsChoiceGrid, FullWidthUsesFourColumnsAndHalfWidthKeepsAtMostTwo)
{
	const auto Full = ResolveSettingsToggleGrid(700, 100, 45, 10, 11, 4);
	const auto Half = ResolveSettingsToggleGrid(340, 100, 45, 10, 11, 2);
	EXPECT_EQ(Full.m_Columns, 4);
	EXPECT_EQ(Full.m_Rows, 3);
	EXPECT_EQ(Half.m_Columns, 2);
	EXPECT_EQ(Half.m_Rows, 6);
}

TEST(SettingsChoiceGrid, LongLabelsReduceColumnsWithoutReducingRequiredCellWidth)
{
	const auto Grid = ResolveSettingsToggleGrid(400, 170, 45, 10, 5, 4);
	EXPECT_EQ(Grid.m_Columns, 2);
	EXPECT_GE(Grid.m_CellWidth, 170);
	const auto Narrow = ResolveSettingsToggleGrid(220, 170, 45, 10, 5, 4);
	EXPECT_EQ(Narrow.m_Columns, 1);
	EXPECT_GE(Narrow.m_CellWidth, 170);
}

TEST(SettingsChoiceGrid, IncompleteLastRowFitsMeasuredAreaAndRetainsGaps)
{
	const auto Grid = ResolveSettingsToggleGrid(610, 130, 50, 10, 11, 4);
	const CUIRect Area{30, 50, 610, Grid.Height()};
	for(int Index = 0; Index < 11; ++Index)
	{
		const auto Cell = Grid.Cell(Area, Index);
		EXPECT_GE(Cell.x, Area.x);
		EXPECT_GE(Cell.y, Area.y);
		EXPECT_LE(Cell.x + Cell.w, Area.x + Area.w);
		EXPECT_LE(Cell.y + Cell.h, Area.y + Area.h);
		if(Index % Grid.m_Columns != 0)
		{
			const auto Previous = Grid.Cell(Area, Index - 1);
			EXPECT_FLOAT_EQ(Cell.x - Previous.x - Previous.w, Grid.m_Gap);
		}
	}
	EXPECT_FLOAT_EQ(Grid.Height(), 170);
}

TEST(SettingsChoiceGrid, EmptyOrTinyAreasKeepNonnegativeDimensions)
{
	const auto Empty = ResolveSettingsToggleGrid(0, 100, 50, 10, 0, 4);
	EXPECT_FLOAT_EQ(Empty.Height(), 0);
	EXPECT_FLOAT_EQ(Empty.m_CellWidth, 0);
	const auto Tiny = ResolveSettingsToggleGrid(20, 100, 50, 10, 2, 4);
	EXPECT_EQ(Tiny.m_Columns, 1);
	EXPECT_FLOAT_EQ(Tiny.m_CellWidth, 20);
}

TEST(SettingsChoiceGrid, LargeFittingCountsAreClampedBeforeIntegerConversion)
{
	for(float Width : {std::numeric_limits<float>::max(), std::numeric_limits<float>::infinity()})
	{
		SCOPED_TRACE(Width);
		const auto Grid = ResolveSettingsToggleGrid(Width, 1, 20, 0, 2, 4);
		EXPECT_EQ(Grid.m_Columns, 4);
		EXPECT_EQ(Grid.m_Rows, 1);
	}
}

TEST(SettingsChoiceGrid, IndeterminateFittingCountUsesOneColumnWithoutIntegerConversion)
{
	const auto Grid = ResolveSettingsToggleGrid(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), 20, 0, 2, 4);
	EXPECT_EQ(Grid.m_Columns, 1);
	EXPECT_EQ(Grid.m_Rows, 2);
}

TEST(SettingsChoiceGrid, MaximumItemCountRoundsRowsUpWithoutSignedAdditionOverflow)
{
	struct SCase
	{
		int m_Columns;
		int m_ExpectedRows;
	};
	for(const auto Case : {SCase{2, 1073741824}, SCase{3, 715827883}, SCase{4, 536870912}})
	{
		SCOPED_TRACE(Case.m_Columns);
		const auto Grid = ResolveSettingsToggleGrid(1000, 100, 0, 0, std::numeric_limits<int>::max(), Case.m_Columns);
		EXPECT_EQ(Grid.m_Columns, Case.m_Columns);
		EXPECT_EQ(Grid.m_Rows, Case.m_ExpectedRows);
	}
}
