#include <game/client/prediction/tick_shift.h>

#include <gtest/gtest.h>

#include <limits>

TEST(PredictionTickShift, AcceptsExactIntBoundaries)
{
	EXPECT_TRUE(QmTickShift::CanShift(0, std::numeric_limits<int>::min()));
	EXPECT_TRUE(QmTickShift::CanShift(0, std::numeric_limits<int>::max()));
	EXPECT_FALSE(QmTickShift::CanShift(0, static_cast<int64_t>(std::numeric_limits<int>::min()) - 1));
	EXPECT_FALSE(QmTickShift::CanShift(0, static_cast<int64_t>(std::numeric_limits<int>::max()) + 1));
}

TEST(PredictionTickShift, HandlesFullInt64DeltaWithoutOverflowingValidation)
{
	EXPECT_FALSE(QmTickShift::CanShift(1, std::numeric_limits<int64_t>::max()));
	EXPECT_FALSE(QmTickShift::CanShift(-1, std::numeric_limits<int64_t>::min()));
}

TEST(PredictionTickShift, CheckModeDoesNotChangeFields)
{
	int First = 10, Last = 30;
	ASSERT_TRUE(QmTickShift::TryShiftFields(5, QmTickShift::EMode::CHECK, {&First, &Last}));
	EXPECT_EQ(First, 10);
	EXPECT_EQ(Last, 30);
}

TEST(PredictionTickShift, CommitShiftsFieldsAroundAbsentEntry)
{
	int First = 10, Last = 30;
	ASSERT_TRUE(QmTickShift::TryShiftFields(5, QmTickShift::EMode::COMMIT, {&First, nullptr, &Last}));
	EXPECT_EQ(First, 15);
	EXPECT_EQ(Last, 35);
}

TEST(PredictionTickShift, LateOverflowLeavesEarlierFieldUnchanged)
{
	int First = 10, Last = std::numeric_limits<int>::max();
	EXPECT_FALSE(QmTickShift::TryShiftFields(1, QmTickShift::EMode::COMMIT, {&First, &Last}));
	EXPECT_EQ(First, 10);
	EXPECT_EQ(Last, std::numeric_limits<int>::max());
}

TEST(PredictionTickShift, LateUnderflowLeavesEarlierFieldUnchanged)
{
	int First = 10, Last = std::numeric_limits<int>::min();
	EXPECT_FALSE(QmTickShift::TryShiftFields(-1, QmTickShift::EMode::COMMIT, {&First, &Last}));
	EXPECT_EQ(First, 10);
	EXPECT_EQ(Last, std::numeric_limits<int>::min());
}

TEST(PredictionTickShift, WideDeltaCanMapOneIntBoundaryToTheOther)
{
	int Tick = std::numeric_limits<int>::min();
	ASSERT_TRUE(QmTickShift::TryShiftFields(4294967295LL, QmTickShift::EMode::COMMIT, {&Tick}));
	EXPECT_EQ(Tick, std::numeric_limits<int>::max());
}

TEST(PredictionTickShift, FailedShiftCanBeRetriedWithRepresentableDelta)
{
	int First = 10, Last = std::numeric_limits<int>::max() - 1;
	EXPECT_FALSE(QmTickShift::TryShiftFields(2, QmTickShift::EMode::COMMIT, {&First, &Last}));
	ASSERT_TRUE(QmTickShift::TryShiftFields(1, QmTickShift::EMode::COMMIT, {&First, &Last}));
	EXPECT_EQ(First, 11);
	EXPECT_EQ(Last, std::numeric_limits<int>::max());
}
