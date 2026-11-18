#include <game/editor/editor_ui.h>

#include <gtest/gtest.h>

#include <limits>

TEST(EditorValueSelector, LargeIncrementsAndDecrementsStayInRange)
{
	constexpr int Min = std::numeric_limits<int>::min();
	constexpr int Max = std::numeric_limits<int>::max();

	EXPECT_EQ(EditorValueSelector::Adjust(Max, Min, Max, 1, 1.0), Max);
	EXPECT_EQ(EditorValueSelector::Adjust(Min, Min, Max, 1, -1.0), Min);
	EXPECT_EQ(EditorValueSelector::Adjust(0, -100, 100, 1, 1e14), 100);
}

TEST(EditorValueSelector, SteppingKeepsAngleAlignment)
{
	EXPECT_EQ(EditorValueSelector::Adjust(50, 0, 359, 45, 1), 90);
	EXPECT_EQ(EditorValueSelector::Adjust(50, 0, 359, 45, -1), 45);
	EXPECT_EQ(EditorValueSelector::Adjust(92, 91, 359, 45, 1), 135);
	EXPECT_EQ(EditorValueSelector::Adjust(92, 91, 100, 45, 1), 100);
}
