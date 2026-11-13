#include <game/client/components/qmclient/collision_position.h>

#include <gtest/gtest.h>

#include <limits>

TEST(QmSnapshotSamplePosition, NormalGroundProbeKeepsTheIntegerOffset)
{
	const auto Position = QmCollision::OffsetSnapshotPosition(ivec2(100, 200), ivec2(0, 16));
	ASSERT_TRUE(Position);
	EXPECT_EQ(*Position, vec2(100, 216));
}

TEST(QmSnapshotSamplePosition, WideAdditionPreservesIntegerQuantizationBeforeFloatConversion)
{
	const auto Position = QmCollision::OffsetSnapshotPosition(ivec2(-100, 536870929), ivec2(0, 16));
	ASSERT_TRUE(Position);
	EXPECT_EQ(*Position, vec2(-100, 536870976.0f));
}

TEST(QmSnapshotSamplePosition, PositiveIntegerOverflowDoesNotProduceASample)
{
	const int Max = std::numeric_limits<int>::max();
	EXPECT_FALSE(QmCollision::OffsetSnapshotPosition(ivec2(0, Max), ivec2(0, 16)));
	EXPECT_FALSE(QmCollision::OffsetSnapshotPosition(ivec2(Max, 0), ivec2(1, 0)));
}

TEST(QmSnapshotSamplePosition, NegativeIntegerOverflowDoesNotProduceASample)
{
	const int Min = std::numeric_limits<int>::min();
	EXPECT_FALSE(QmCollision::OffsetSnapshotPosition(ivec2(0, Min), ivec2(0, -1)));
	EXPECT_FALSE(QmCollision::OffsetSnapshotPosition(ivec2(Min, 0), ivec2(-1, 0)));
}

TEST(QmSnapshotSamplePosition, IntegerValuesThatRoundUpToTheUnsafeFloatUpperBoundAreRejected)
{
	const int Max = std::numeric_limits<int>::max();
	EXPECT_FALSE(QmCollision::OffsetSnapshotPosition(ivec2(Max, 0), ivec2(0, 0)));
	EXPECT_FALSE(QmCollision::OffsetSnapshotPosition(ivec2(0, Max - 16), ivec2(0, 16)));
}

TEST(QmSnapshotSamplePosition, RepresentableFloatBoundariesRemainAvailable)
{
	const auto Upper = QmCollision::OffsetSnapshotPosition(ivec2(0, 2147483520), ivec2(0, 16));
	ASSERT_TRUE(Upper);
	EXPECT_EQ(*Upper, vec2(0, 2147483520.0f));
	const auto Lower = QmCollision::OffsetSnapshotPosition(ivec2(std::numeric_limits<int>::min(), 0), ivec2(0, 16));
	ASSERT_TRUE(Lower);
	EXPECT_EQ(*Lower, vec2(static_cast<float>(std::numeric_limits<int>::min()), 16));
}
