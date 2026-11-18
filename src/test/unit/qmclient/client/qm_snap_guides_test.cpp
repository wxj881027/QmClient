#include <game/client/components/qmclient/snap_guides.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
	std::vector<IGraphics::CLineItem> BuildDashedLine(float X0, float Y0, float X1, float Y1, int MaxSegments = 32)
	{
		std::vector<IGraphics::CLineItem> Segments(MaxSegments);
		const int Count = QmBuildDashedLine(X0, Y0, X1, Y1, 6.0f, 4.0f, Segments.data(), MaxSegments);
		Segments.resize(std::max(0, Count));
		return Segments;
	}
} // namespace

TEST(QmSnapAxis, StaysUntouchedOutsideThreshold)
{
	const SQmSnapTarget aTargets[] = {
		{0.0f, 0.0f},
		{50.0f, 50.0f},
	};
	const SQmSnapAxisResult Result = QmSnapAxisToTargets(30.0f, aTargets, (int)std::size(aTargets), 6.0f, -100.0f, 100.0f);
	EXPECT_FLOAT_EQ(Result.m_Value, 30.0f);
	EXPECT_FALSE(Result.m_HasGuide);
}

TEST(QmSnapAxis, PicksNearestTargetInsideThreshold)
{
	const SQmSnapTarget aTargets[] = {
		{10.0f, 100.0f},
		{14.0f, 140.0f},
		{40.0f, 400.0f},
	};
	const SQmSnapAxisResult Result = QmSnapAxisToTargets(12.0f, aTargets, (int)std::size(aTargets), 6.0f, -100.0f, 100.0f);
	EXPECT_FLOAT_EQ(Result.m_Value, 10.0f);
	EXPECT_TRUE(Result.m_HasGuide);
	EXPECT_FLOAT_EQ(Result.m_Guide, 100.0f);
}

TEST(QmSnapAxis, GuidesFollowTheChosenTarget)
{
	const SQmSnapTarget aTargets[] = {
		{0.0f, 0.0f},
		{-200.0f, 20.0f},
	};
	const SQmSnapAxisResult Result = QmSnapAxisToTargets(-199.0f, aTargets, (int)std::size(aTargets), 6.0f, -300.0f, 300.0f);
	EXPECT_FLOAT_EQ(Result.m_Value, -200.0f);
	EXPECT_TRUE(Result.m_HasGuide);
	EXPECT_FLOAT_EQ(Result.m_Guide, 20.0f);
}

TEST(QmSnapAxis, CandidateIsClampedToDragRangeBeforeComparing)
{
	// 目标在允许范围之外时，按撞到限位的值参与吸附，避免把行拖出预览框。
	const SQmSnapTarget aTargets[] = {
		{-80.0f, -80.0f},
	};
	const SQmSnapAxisResult Result = QmSnapAxisToTargets(-40.0f, aTargets, (int)std::size(aTargets), 6.0f, -45.0f, 45.0f);
	EXPECT_FLOAT_EQ(Result.m_Value, -45.0f);
	EXPECT_TRUE(Result.m_HasGuide);
	EXPECT_FLOAT_EQ(Result.m_Guide, -80.0f);
}

TEST(QmSnapAxis, ValueOutsideRangeIsClampedWithoutGuide)
{
	const SQmSnapTarget aTargets[] = {
		{30.0f, 30.0f},
	};
	const SQmSnapAxisResult Result = QmSnapAxisToTargets(120.0f, aTargets, (int)std::size(aTargets), 6.0f, -10.0f, 10.0f);
	EXPECT_FLOAT_EQ(Result.m_Value, 10.0f);
	EXPECT_FALSE(Result.m_HasGuide);
}

TEST(QmRectSnap, AdjacentBlocksSnapBorderToBorder)
{
	// 参考方块占 [-72.5, -43.5]，被拖方块宽 29 且当前居中于 0。
	// 拖到 -30 时最近的有效落点是「边框贴合」的 -29，而不是完全重叠的 -58。
	SQmSnapTarget aTargets[8];
	const int Count = QmAppendRectSnapTargets(aTargets, 0, (int)std::size(aTargets), 0.0f, 29.0f, -72.5f, -43.5f);
	ASSERT_EQ(Count, 5);
	const SQmSnapAxisResult Result = QmSnapAxisToTargets(-30.0f, aTargets, Count, 12.0f, -100.0f, 100.0f);
	EXPECT_FLOAT_EQ(Result.m_Value, -29.0f);
	EXPECT_TRUE(Result.m_HasGuide);
	EXPECT_FLOAT_EQ(Result.m_Guide, -43.5f);
}

TEST(QmRectSnap, AppendingRespectsBufferCapacity)
{
	SQmSnapTarget aTargets[3];
	const int Count = QmAppendRectSnapTargets(aTargets, 0, (int)std::size(aTargets), 0.0f, 10.0f, -10.0f, 10.0f);
	EXPECT_EQ(Count, 3);
	EXPECT_EQ(QmAppendRectSnapTargets(aTargets, Count, (int)std::size(aTargets), 0.0f, 10.0f, -10.0f, 10.0f), 3);
}

TEST(QmBoundsSnap, FrameOnlyOffersAlignmentLines)
{
	// 预览框不产生「贴框外沿」的落点，否则夹到边界后四个角落会特别粘。
	SQmSnapTarget aTargets[8];
	const int Count = QmAppendAlignSnapTargets(aTargets, 0, (int)std::size(aTargets), -14.5f, 14.5f, -200.0f, 200.0f);
	ASSERT_EQ(Count, 3);
	EXPECT_FLOAT_EQ(aTargets[0].m_Value, -185.5f);
	EXPECT_FLOAT_EQ(aTargets[1].m_Value, 185.5f);
	EXPECT_FLOAT_EQ(aTargets[2].m_Value, 0.0f);
}

TEST(QmBoundsSnap, FilterDropsUnreachableAndDuplicateTargets)
{
	SQmSnapTarget aTargets[] = {
		{100.0f, 1.0f},
		{-40.0f, 2.0f},
		{100.2f, 3.0f},
		{20.0f, 4.0f},
	};
	const int Count = QmFilterSnapTargets(aTargets, (int)std::size(aTargets), -50.0f, 30.0f);
	ASSERT_EQ(Count, 2);
	EXPECT_FLOAT_EQ(aTargets[0].m_Value, -40.0f);
	EXPECT_FLOAT_EQ(aTargets[1].m_Value, 20.0f);
}

TEST(QmDashLine, VerticalLineKeepsDashesOnTheSameColumn)
{
	const auto Segments = BuildDashedLine(5.0f, 0.0f, 5.0f, 20.0f);
	ASSERT_EQ(Segments.size(), 2u);
	EXPECT_FLOAT_EQ(Segments[0].m_X0, 5.0f);
	EXPECT_FLOAT_EQ(Segments[0].m_Y0, 0.0f);
	EXPECT_FLOAT_EQ(Segments[0].m_X1, 5.0f);
	EXPECT_FLOAT_EQ(Segments[0].m_Y1, 6.0f);
	EXPECT_FLOAT_EQ(Segments[1].m_Y0, 10.0f);
	EXPECT_FLOAT_EQ(Segments[1].m_Y1, 16.0f);
}

TEST(QmDashLine, HorizontalLineKeepsDashesOnTheSameRow)
{
	const auto Segments = BuildDashedLine(10.0f, 3.0f, 0.0f, 3.0f);
	ASSERT_EQ(Segments.size(), 1u);
	EXPECT_FLOAT_EQ(Segments[0].m_X0, 10.0f);
	EXPECT_FLOAT_EQ(Segments[0].m_X1, 4.0f);
	EXPECT_FLOAT_EQ(Segments[0].m_Y0, 3.0f);
	EXPECT_FLOAT_EQ(Segments[0].m_Y1, 3.0f);
}

TEST(QmDashLine, ShortLineBecomesSingleSegment)
{
	const auto Segments = BuildDashedLine(0.0f, 0.0f, 0.0f, 3.0f);
	ASSERT_EQ(Segments.size(), 1u);
	EXPECT_FLOAT_EQ(Segments[0].m_Y1, 3.0f);
}

TEST(QmDashLine, SegmentBufferLimitIsRespected)
{
	const auto Segments = BuildDashedLine(0.0f, 0.0f, 0.0f, 1000.0f, 4);
	EXPECT_EQ(Segments.size(), 4u);
	EXPECT_FLOAT_EQ(Segments[3].m_Y1, 36.0f);
}

TEST(QmDashLine, DiagonalFallsBackToSingleSegment)
{
	const auto Segments = BuildDashedLine(0.0f, 0.0f, 10.0f, 10.0f);
	ASSERT_EQ(Segments.size(), 1u);
	EXPECT_FLOAT_EQ(Segments[0].m_X1, 10.0f);
	EXPECT_FLOAT_EQ(Segments[0].m_Y1, 10.0f);
}
