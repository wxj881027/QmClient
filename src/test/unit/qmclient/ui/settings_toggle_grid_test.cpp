#include <game/client/QmUi/SettingsToggleGrid.h>

#include <gtest/gtest.h>

TEST(SettingsToggleGrid, FullWidthUsesFourColumnsAndHalfWidthKeepsAtMostTwo)
{
	const auto Full = ResolveSettingsToggleGrid(700, 100, 45, 10, 11, 4);
	const auto Half = ResolveSettingsToggleGrid(340, 100, 45, 10, 11, 2);
	EXPECT_EQ(Full.m_Columns, 4);
	EXPECT_EQ(Full.m_Rows, 3);
	EXPECT_EQ(Half.m_Columns, 2);
	EXPECT_EQ(Half.m_Rows, 6);
}

TEST(SettingsToggleGrid, LongLabelsReduceColumnsWithoutReducingRequiredCellWidth)
{
	const auto Grid = ResolveSettingsToggleGrid(400, 170, 45, 10, 5, 4);
	EXPECT_EQ(Grid.m_Columns, 2);
	EXPECT_GE(Grid.m_CellWidth, 170);
	const auto Narrow = ResolveSettingsToggleGrid(220, 170, 45, 10, 5, 4);
	EXPECT_EQ(Narrow.m_Columns, 1);
	EXPECT_GE(Narrow.m_CellWidth, 170);
}

TEST(SettingsToggleGrid, IncompleteLastRowFitsMeasuredAreaAndRetainsGaps)
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

TEST(SettingsToggleGrid, EmptyOrTinyAreasKeepNonnegativeDimensions)
{
	const auto Empty = ResolveSettingsToggleGrid(0, 100, 50, 10, 0, 4);
	EXPECT_FLOAT_EQ(Empty.Height(), 0);
	EXPECT_FLOAT_EQ(Empty.m_CellWidth, 0);
	const auto Tiny = ResolveSettingsToggleGrid(20, 100, 50, 10, 2, 4);
	EXPECT_EQ(Tiny.m_Columns, 1);
	EXPECT_FLOAT_EQ(Tiny.m_CellWidth, 20);
}

TEST(SettingsToggleGrid, SmallGroupsUseAllAvailableWidthWithoutEmptyColumns)
{
	const auto Pair = ResolveSettingsToggleGrid(700, 100, 45, 10, 2, 4);
	EXPECT_EQ(Pair.m_Columns, 2);
	EXPECT_EQ(Pair.m_Rows, 1);
	EXPECT_FLOAT_EQ(Pair.m_CellWidth, 345);
	const auto Single = ResolveSettingsToggleGrid(700, 100, 45, 10, 1, 4);
	EXPECT_EQ(Single.m_Columns, 1);
	EXPECT_FLOAT_EQ(Single.m_CellWidth, 700);
}

TEST(SettingsToggleGrid, CardWidthControlsColumnLimitIncludingWideHalfCards)
{
	EXPECT_EQ(ResolveSettingsToggleGroupColumnLimit(600, 640, true), 2);
	EXPECT_EQ(ResolveSettingsToggleGroupColumnLimit(1250, 640, true), 4);
	EXPECT_EQ(ResolveSettingsToggleGroupColumnLimit(600, 640, false), 4);
}

TEST(SettingsToggleGrid, WrappedLabelStaysAboveCenteredControl)
{
	const CUIRect Cell{30, 50, 120, 65};
	const auto Layout = ResolveSettingsToggleCell(Cell, 40, 20, 5);
	EXPECT_FLOAT_EQ(Layout.m_Label.h, 40);
	EXPECT_FLOAT_EQ(Layout.m_Control.y, 95);
	EXPECT_FLOAT_EQ(Layout.m_Control.x + Layout.m_Control.w * 0.5f, Cell.x + Cell.w * 0.5f);
	EXPECT_FLOAT_EQ(Layout.m_Control.y + Layout.m_Control.h, Cell.y + Cell.h);
}

TEST(SettingsToggleGrid, NarrowCellKeepsControlInsideItsHorizontalBounds)
{
	const CUIRect Cell{10, 20, 12, 45};
	const auto Layout = ResolveSettingsToggleCell(Cell, 20, 20, 5);
	EXPECT_GE(Layout.m_Control.x, Cell.x);
	EXPECT_LE(Layout.m_Control.x + Layout.m_Control.w, Cell.x + Cell.w);
}

TEST(SettingsToggleGrid, BooleanClicksToggleOnlyTheSelectedConfiguration)
{
	int First = 0;
	int Second = 1;
	const SSettingsToggleEntry Entry{&First, "first", "First"};
	EXPECT_TRUE(ApplySettingsToggleEntry(Entry, true, true, false));
	EXPECT_EQ(First, 1);
	EXPECT_EQ(Second, 1);
	EXPECT_TRUE(ApplySettingsToggleEntry(Entry, true, true, false));
	EXPECT_EQ(First, 0);
	EXPECT_FALSE(ApplySettingsToggleEntry(Entry, false, true, false));
	EXPECT_EQ(First, 0);
}

TEST(SettingsToggleGrid, ReadOnlyAndTemporarilyOverriddenEntriesRejectClicks)
{
	int Value = 1;
	const SSettingsToggleEntry Entry{&Value, "value", "Value"};
	EXPECT_FALSE(ApplySettingsToggleEntry(Entry, true, false, false));
	EXPECT_FALSE(ApplySettingsToggleEntry(Entry, true, true, true));
	EXPECT_EQ(Value, 1);
	EXPECT_TRUE(ApplySettingsToggleEntry(Entry, true, true, false));
	EXPECT_EQ(Value, 0);
}

TEST(SettingsToggleGrid, BitmaskEntriesKeepOtherEffectsAndSupportRepeatedClicks)
{
	int Effects = 1 | 4;
	const SSettingsToggleEntry Entry{&Effects, "gradient", "Gradient", "gradient", 2};
	EXPECT_FALSE(SettingsToggleEntryValue(Entry));
	EXPECT_TRUE(ApplySettingsToggleEntry(Entry, true, true, false));
	EXPECT_EQ(Effects, 1 | 2 | 4);
	EXPECT_TRUE(SettingsToggleEntryValue(Entry));
	EXPECT_TRUE(ApplySettingsToggleEntry(Entry, true, true, false));
	EXPECT_EQ(Effects, 1 | 4);
}
