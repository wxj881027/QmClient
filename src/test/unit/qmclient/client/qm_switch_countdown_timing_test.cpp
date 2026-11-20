#include <game/client/components/hud_media_island_logic.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	constexpr int Max = std::numeric_limits<int>::max();
	constexpr int Min = std::numeric_limits<int>::min();
	struct SMapQuery
	{
		int m_Calls = 0;
		int m_Result = 17;
		vec2 m_Position{};
		int GetPureMapIndex(vec2 Position)
		{
			++m_Calls;
			m_Position = Position;
			return m_Result;
		}
	};
}

TEST(QmSwitchCountdownTiming, NormalTriggerRetainsTheExtraTickAndOwner)
{
	SHudSwitchCountdownTracker Tracker;
	ASSERT_TRUE(Tracker.Track(1, 2, 100, 3, 50, 7, 1));
	EXPECT_EQ(Tracker.m_aaEndTick[1][2], 251);
	EXPECT_EQ(Tracker.m_aaTouchTick[1][2], 100);
	EXPECT_EQ(Tracker.m_aaClientId[1][2], 7);
	EXPECT_EQ(Tracker.m_aaConnection[1][2], 1);
}

TEST(QmSwitchCountdownTiming, ZeroDelayStillExpiresOneTickAfterTheTouch)
{
	SHudSwitchCountdownTracker Tracker;
	ASSERT_TRUE(Tracker.Track(1, 2, 100, 0, 50, 7, 0));
	EXPECT_EQ(Tracker.m_aaEndTick[1][2], 101);
}

TEST(QmSwitchCountdownTiming, LateSnapshotTickDoesNotOverflowTheDeadline)
{
	SHudSwitchCountdownTracker Tracker;
	ASSERT_TRUE(Tracker.Track(1, 2, Max, 255, 50, 7, 0));
	EXPECT_EQ(Tracker.m_aaEndTick[1][2], INT64_C(2147496398));
	EXPECT_EQ(Tracker.m_aaTouchTick[1][2], Max);
}

TEST(QmSwitchCountdownTiming, IntegerInputsAreWidenedBeforeBothMultiplyAndAdd)
{
	SHudSwitchCountdownTracker Tracker;
	ASSERT_TRUE(Tracker.Track(1, 2, Max, Max, Max, 7, 0));
	EXPECT_EQ(Tracker.m_aaEndTick[1][2], INT64_C(4611686016279904257));
}

TEST(QmSwitchCountdownTiming, InvalidTrackingDomainDoesNotReplaceTheExistingSlot)
{
	SHudSwitchCountdownTracker Tracker;
	ASSERT_TRUE(Tracker.Track(1, 2, 100, 3, 50, 7, 0));
	EXPECT_FALSE(Tracker.Track(-1, 2, 200, 3, 50, 7, 0));
	EXPECT_FALSE(Tracker.Track(NUM_DDRACE_TEAMS, 2, 200, 3, 50, 7, 0));
	EXPECT_FALSE(Tracker.Track(1, 0, 200, 3, 50, 7, 0));
	EXPECT_FALSE(Tracker.Track(1, 256, 200, 3, 50, 7, 0));
	EXPECT_FALSE(Tracker.Track(1, 2, 200, -1, 50, 7, 0));
	EXPECT_FALSE(Tracker.Track(1, 2, 200, 3, 0, 7, 0));
	EXPECT_FALSE(Tracker.Track(1, 2, 200, 3, 50, MAX_CLIENTS, 0));
	EXPECT_FALSE(Tracker.Track(1, 2, 200, 3, 50, 7, NUM_DUMMIES));
	EXPECT_EQ(Tracker.m_aaEndTick[1][2], 251);
	EXPECT_EQ(Tracker.m_aaTouchTick[1][2], 100);
}

TEST(QmSwitchCountdownTiming, RetouchReplacesOwnerAndDeadlineTogether)
{
	SHudSwitchCountdownTracker Tracker;
	ASSERT_TRUE(Tracker.Track(1, 2, 100, 3, 50, 7, 0));
	ASSERT_TRUE(Tracker.Track(1, 2, 200, 1, 50, 8, 1));
	EXPECT_EQ(Tracker.m_aaEndTick[1][2], 251);
	EXPECT_EQ(Tracker.m_aaTouchTick[1][2], 200);
	EXPECT_EQ(Tracker.m_aaClientId[1][2], 8);
	EXPECT_EQ(Tracker.m_aaConnection[1][2], 1);
}

TEST(QmSwitchCountdownTiming, ResetClearsWideDeadlinesAndAllOwnerSlots)
{
	SHudSwitchCountdownTracker Tracker;
	ASSERT_TRUE(Tracker.Track(0, 1, Max, 255, 50, 7, 0));
	ASSERT_TRUE(Tracker.Track(NUM_DDRACE_TEAMS - 1, 255, 100, 0, 50, 8, 1));
	Tracker.Reset();
	for(int Team = 0; Team < NUM_DDRACE_TEAMS; ++Team)
		for(int Number = 0; Number < 256; ++Number)
		{
			ASSERT_EQ(Tracker.m_aaEndTick[Team][Number], 0);
			ASSERT_EQ(Tracker.m_aaTouchTick[Team][Number], 0);
			ASSERT_EQ(Tracker.m_aaClientId[Team][Number], -1);
			ASSERT_EQ(Tracker.m_aaConnection[Team][Number], -1);
		}
}

TEST(QmSwitchCountdownTiming, SecondsKeepCeilingAndExactExpiry)
{
	EXPECT_EQ(QmHudSwitchCountdownSecondsLeft(251, 100, 50), 4u);
	EXPECT_EQ(QmHudSwitchCountdownSecondsLeft(251, 201, 50), 1u);
	EXPECT_EQ(QmHudSwitchCountdownSecondsLeft(251, 250, 50), 1u);
	EXPECT_EQ(QmHudSwitchCountdownSecondsLeft(251, 251, 50), 0u);
	EXPECT_EQ(QmHudSwitchCountdownSecondsLeft(251, 252, 50), 0u);
}

TEST(QmSwitchCountdownTiming, WideRemainingIntervalDoesNotNarrowBeforeCeiling)
{
	EXPECT_EQ(QmHudSwitchCountdownSecondsLeft(INT64_C(2147496398), Min, 50), UINT64_C(85899601));
}

TEST(QmSwitchCountdownTiming, InvalidTickSpeedHasNoVisibleSeconds)
{
	EXPECT_EQ(QmHudSwitchCountdownSecondsLeft(251, 100, 0), 0u);
	EXPECT_EQ(QmHudSwitchCountdownSecondsLeft(251, 100, -1), 0u);
}

TEST(QmSwitchCountdownTiming, SecondsCanRepresentTheCompleteAcceptedEndTickDomain)
{
	EXPECT_EQ(QmHudSwitchCountdownSecondsLeft(std::numeric_limits<int64_t>::max(), Min, 1), UINT64_C(9223372039002259455));
}

TEST(QmSwitchCountdownTiming, InvalidPositionDoesNotEnterTheMapQuery)
{
	SMapQuery Query;
	const float Inf = std::numeric_limits<float>::infinity();
	for(vec2 Position : {vec2(static_cast<float>(Max), 0), vec2(0, Inf), vec2(std::numeric_limits<float>::quiet_NaN(), 0), vec2(std::nextafter(static_cast<float>(Min), -Inf), 0)})
		EXPECT_EQ(QmHudSwitchCountdownMapIndex(Query, Position), -1);
	EXPECT_EQ(Query.m_Calls, 0);
}

TEST(QmSwitchCountdownTiming, ValidPositionKeepsTheOriginalMapQueryAndCoordinates)
{
	SMapQuery Query;
	for(vec2 Position : {vec2(48.25f, -10.5f), vec2(2147483520.0f, 0), vec2(static_cast<float>(Min), 0)})
	{
		EXPECT_EQ(QmHudSwitchCountdownMapIndex(Query, Position), 17);
		EXPECT_EQ(Query.m_Position, Position);
	}
	EXPECT_EQ(Query.m_Calls, 3);
}

TEST(QmSwitchCountdownTiming, MissingMapIndexKeepsItsOriginalFailureResult)
{
	SMapQuery Query;
	Query.m_Result = -1;
	EXPECT_EQ(QmHudSwitchCountdownMapIndex(Query, vec2(48, 48)), -1);
	EXPECT_EQ(Query.m_Calls, 1);
}
