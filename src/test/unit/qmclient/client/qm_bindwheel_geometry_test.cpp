#include <game/client/components/qmclient/bindwheel_state.h>

#include <gtest/gtest.h>

#include <limits>

TEST(QmBindWheelGeometry, NanCoordinatesHaveNoSelectedBinding)
{
	const float Nan = std::numeric_limits<float>::quiet_NaN();
	EXPECT_EQ(CQmBindWheelState::SelectedBind(vec2(Nan, 170), 64), -1);
	EXPECT_EQ(CQmBindWheelState::SelectedBind(vec2(170, Nan), 64), -1);
}

TEST(QmBindWheelGeometry, InfiniteCoordinatesHaveNoSelectedBinding)
{
	const float Inf = std::numeric_limits<float>::infinity();
	for(const vec2 Mouse : {vec2(Inf, 0), vec2(-Inf, 0), vec2(0, Inf), vec2(0, -Inf)})
	{
		SCOPED_TRACE(::testing::Message() << Mouse.x << ", " << Mouse.y);
		EXPECT_EQ(CQmBindWheelState::SelectedBind(Mouse, 64), -1);
	}
}

TEST(QmBindWheelGeometry, FullPositiveCountDomainDoesNotOverflowIndexWrapping)
{
	const int Count = std::numeric_limits<int>::max();
	EXPECT_EQ(CQmBindWheelState::SelectedBind(vec2(0, 170), Count), 536870912);
	EXPECT_EQ(CQmBindWheelState::SelectedBind(vec2(-170, 0), Count), 1073741824);
	EXPECT_EQ(CQmBindWheelState::SelectedBind(vec2(0, -170), Count), 1610612735);
}

TEST(QmBindWheelGeometry, NonpositiveCountsHaveNoSelection)
{
	for(int Count : {0, -1, std::numeric_limits<int>::min()})
	{
		SCOPED_TRACE(Count);
		EXPECT_EQ(CQmBindWheelState::SelectedBind(vec2(170, 0), Count), -1);
	}
}

TEST(QmBindWheelGeometry, OrdinaryCursorMovementRetainsTheOriginalAddition)
{
	EXPECT_EQ(CQmBindWheelState::AdvanceCursor(vec2(10, -20), vec2(5, 7)), vec2(15, -13));
	EXPECT_EQ(CQmBindWheelState::AdvanceCursor(vec2(10, -20), vec2(0, 0)), vec2(10, -20));
}

TEST(QmBindWheelGeometry, InvalidMovementDoesNotReplaceTheLastValidCursor)
{
	const float Nan = std::numeric_limits<float>::quiet_NaN(), Inf = std::numeric_limits<float>::infinity();
	for(const vec2 Offset : {vec2(Nan, 1), vec2(1, Nan), vec2(Inf, 0), vec2(0, -Inf)})
	{
		SCOPED_TRACE(::testing::Message() << Offset.x << ", " << Offset.y);
		EXPECT_EQ(CQmBindWheelState::AdvanceCursor(vec2(10, 20), Offset), vec2(10, 20));
	}
}

TEST(QmBindWheelGeometry, CursorAdditionOutsideTheFloatDomainKeepsTheOldPosition)
{
	const float Max = std::numeric_limits<float>::max();
	EXPECT_EQ(CQmBindWheelState::AdvanceCursor(vec2(Max, 20), vec2(Max, 1)), vec2(Max, 20));
	EXPECT_EQ(CQmBindWheelState::AdvanceCursor(vec2(10, -Max), vec2(1, -Max)), vec2(10, -Max));
}

TEST(QmBindWheelGeometry, InvalidStoredCursorReturnsToTheCenter)
{
	const float Nan = std::numeric_limits<float>::quiet_NaN(), Inf = std::numeric_limits<float>::infinity();
	for(const vec2 Cursor : {vec2(Nan, 20), vec2(10, Nan), vec2(Inf, 20), vec2(10, -Inf)})
	{
		SCOPED_TRACE(::testing::Message() << Cursor.x << ", " << Cursor.y);
		EXPECT_EQ(CQmBindWheelState::AdvanceCursor(Cursor, vec2(10, 20)), vec2(0, 0));
	}
}

TEST(QmBindWheelGeometry, OppositeLargeFiniteCoordinatesCanCancelNormally)
{
	const float Max = std::numeric_limits<float>::max();
	EXPECT_EQ(CQmBindWheelState::AdvanceCursor(vec2(Max, -Max), vec2(-Max, Max)), vec2(0, 0));
}

TEST(QmBindWheelGeometry, TheActualMaximumWheelSizeKeepsItsCardinalSectors)
{
	EXPECT_EQ(CQmBindWheelState::SelectedBind(vec2(170, 0), 64), 0);
	EXPECT_EQ(CQmBindWheelState::SelectedBind(vec2(0, 170), 64), 16);
	EXPECT_EQ(CQmBindWheelState::SelectedBind(vec2(-170, 0), 64), 32);
	EXPECT_EQ(CQmBindWheelState::SelectedBind(vec2(0, -170), 64), 48);
}

TEST(QmBindWheelGeometry, InvalidCursorReleaseDoesNotPoisonTheNextOpen)
{
	CQmBindWheelState State;
	State.Open();
	State.Release(CQmBindWheelState::SelectedBind(vec2(std::numeric_limits<float>::quiet_NaN(), 170), 64));
	EXPECT_EQ(State.TakePendingBind(), -1);
	State.Open();
	State.Release(CQmBindWheelState::SelectedBind(vec2(170, 0), 64));
	EXPECT_EQ(State.TakePendingBind(), 0);
}
