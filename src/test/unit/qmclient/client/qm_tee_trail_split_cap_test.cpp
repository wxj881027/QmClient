#include <game/client/components/tclient/qm_tee_trail.h>

#include <gtest/gtest.h>

TEST(QmTeeTrailState, SplitPreservesTheHeadWhenItReturnsToTheLastSamplePosition)
{
	qm_tee_trail::CTrailState State;
	State.Update(vec2(0, 0), 100.0, 6.0f, 30.0f);
	State.Update(vec2(6, 0), 101.0, 6.0f, 30.0f);
	// 头端折返后回到原采样位置，途中没有跨过下一个采样间距。
	State.Update(vec2(8, 0), 102.0, 2.0f, 25.0f);
	State.Update(vec2(6, 0), 103.0, 2.0f, 25.0f);

	std::vector<CTrailPart> vBefore;
	State.Export(vBefore);
	ASSERT_EQ(vBefore.size(), 2u);
	const CTrailPart &Before = vBefore.front();
	ASSERT_EQ(Before.m_Pos, vec2(6, 0));
	ASSERT_DOUBLE_EQ(Before.m_Time, 103.0);

	State.Update(vec2(80, 0), 104.0, 2.0f, 25.0f, qm_tee_trail::EUpdateMode::KEEP_HISTORY);
	std::vector<CTrailPart> vAfter;
	State.Export(vAfter);
	ASSERT_EQ(vAfter.size(), vBefore.size() + 1);
	const CTrailPart &After = vAfter[1];
	EXPECT_NE(vAfter.front().m_Segment, After.m_Segment);
	EXPECT_EQ(After.m_Segment, Before.m_Segment);
	EXPECT_EQ(After.m_Pos, Before.m_Pos);
	EXPECT_DOUBLE_EQ(After.m_Time, Before.m_Time);
	EXPECT_EQ(After.m_Tick, Before.m_Tick);
	EXPECT_DOUBLE_EQ(After.m_Distance, Before.m_Distance);
	EXPECT_FLOAT_EQ(After.m_Speed, Before.m_Speed);
	EXPECT_FLOAT_EQ(After.m_Life, Before.m_Life);
}
