#include <game/client/components/tclient/fast_practice.h>

#include <gtest/gtest.h>

TEST(QmFastPracticeRescueHistory, RetainsMostRecentDistinctPositionsInOrder)
{
	qm_fast_practice::CRescueHistory History;
	ASSERT_TRUE(History.Add(vec2(0.0f, 8.0f)));
	EXPECT_FALSE(History.Add(vec2(47.0f, 8.0f)));
	for(int Index = 1; Index <= 256; ++Index)
		ASSERT_TRUE(History.Add(vec2(Index * 48.0f, 8.0f)));

	ASSERT_EQ(History.Size(), 256);
	EXPECT_EQ(History.NewestAt(0), vec2(256 * 48.0f, 8.0f));
	EXPECT_EQ(History.NewestAt(255), vec2(48.0f, 8.0f));
}

TEST(QmFastPracticeRescueHistory, ResetLetsTheSamePositionBeRecordedAgain)
{
	qm_fast_practice::CRescueHistory History;
	const vec2 Position(64.0f, 32.0f);
	ASSERT_TRUE(History.Add(Position));

	History = {};
	EXPECT_EQ(History.Size(), 0);
	ASSERT_TRUE(History.Add(Position));
	EXPECT_EQ(History.NewestAt(0), Position);
}
