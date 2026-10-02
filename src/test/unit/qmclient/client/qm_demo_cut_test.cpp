#include <game/client/components/qmclient/demo_cut.h>

#include <gtest/gtest.h>

TEST(QmDemoCut, RequiresSelectionAndResolvesOpenEnds)
{
	SDemoSliceSegment Range;
	EXPECT_FALSE(qm_demo_cut::ResolveRange(-1, -1, 100, 500, Range));
	ASSERT_TRUE(qm_demo_cut::ResolveRange(175, -1, 100, 500, Range));
	EXPECT_EQ(Range.m_StartTick, 175);
	EXPECT_EQ(Range.m_EndTick, 500);
	ASSERT_TRUE(qm_demo_cut::ResolveRange(-1, 325, 100, 500, Range));
	EXPECT_EQ(Range.m_StartTick, 100);
	EXPECT_EQ(Range.m_EndTick, 325);
}

TEST(QmDemoCut, RejectsEmptyReversedAndOutOfBoundsCuts)
{
	SDemoSliceSegment Range;
	EXPECT_FALSE(qm_demo_cut::ResolveRange(200, 200, 100, 500, Range));
	EXPECT_FALSE(qm_demo_cut::ResolveRange(300, 200, 100, 500, Range));
	EXPECT_FALSE(qm_demo_cut::ResolveRange(600, -1, 100, 500, Range));
	EXPECT_FALSE(qm_demo_cut::ResolveRange(-1, 50, 100, 500, Range));
	EXPECT_FALSE(qm_demo_cut::ResolveRange(100, 200, 500, 100, Range));
	ASSERT_TRUE(qm_demo_cut::ResolveRange(50, 550, 100, 500, Range));
	EXPECT_EQ(Range.m_StartTick, 100);
	EXPECT_EQ(Range.m_EndTick, 500);
}

TEST(QmDemoCut, TimeDisplayRetainsSubsecondTicks)
{
	EXPECT_EQ(qm_demo_cut::ToCentiseconds(1, 50), 2);
	EXPECT_EQ(qm_demo_cut::ToCentiseconds(25, 50), 50);
	EXPECT_EQ(qm_demo_cut::ToCentiseconds(51, 50), 102);
}

TEST(QmDemoCut, PreviewStopsAtEndAndCanRestart)
{
	qm_demo_cut::CPreview Preview;
	EXPECT_FALSE(Preview.Start({250, 250}));
	ASSERT_TRUE(Preview.Start({175, 325}));
	EXPECT_TRUE(Preview.IsActive());
	EXPECT_FALSE(Preview.Update(324));
	EXPECT_TRUE(Preview.Update(325));
	EXPECT_FALSE(Preview.IsActive());
	EXPECT_TRUE(Preview.IsFinished());
	EXPECT_FALSE(Preview.Update(326));
	ASSERT_TRUE(Preview.Start({200, 275}));
	EXPECT_TRUE(Preview.IsActive());
	Preview.Reset();
	EXPECT_FALSE(Preview.IsFinished());
}
