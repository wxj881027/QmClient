#include <game/mapitems.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/support/prediction_world.h>

#include <limits>

namespace
{
	class PredictionSwitchState : public ::testing::Test
	{
	protected:
		CPredictionTestWorld m_World;

		void SetUp() override
		{
			m_World.Switchers().resize(3);
			for(auto &Switcher : m_World.Switchers())
			{
				Switcher.m_Initial = true;
				for(int Team = 0; Team < NUM_DDRACE_TEAMS; ++Team)
				{
					Switcher.m_aStatus[Team] = true;
					Switcher.m_aEndTick[Team] = 123;
					Switcher.m_aType[Team] = TILE_SWITCHOPEN;
					Switcher.m_aLastUpdateTick[Team] = 70;
				}
			}
		}

		void ExpectUnchanged(const std::vector<SSwitchers> &Before, int UpdatedNumber = -1, int UpdatedTeam = -1)
		{
			ASSERT_EQ(m_World.Switchers().size(), Before.size());
			for(size_t Number = 0; Number < Before.size(); ++Number)
			{
				SCOPED_TRACE(Number);
				const auto &Actual = m_World.Switchers()[Number];
				EXPECT_EQ(Actual.m_Initial, Before[Number].m_Initial);
				for(int Team = 0; Team < NUM_DDRACE_TEAMS; ++Team)
				{
					if(static_cast<int>(Number) == UpdatedNumber && Team == UpdatedTeam)
						continue;
					SCOPED_TRACE(Team);
					EXPECT_EQ(Actual.m_aStatus[Team], Before[Number].m_aStatus[Team]);
					EXPECT_EQ(Actual.m_aEndTick[Team], Before[Number].m_aEndTick[Team]);
					EXPECT_EQ(Actual.m_aType[Team], Before[Number].m_aType[Team]);
					EXPECT_EQ(Actual.m_aLastUpdateTick[Team], Before[Number].m_aLastUpdateTick[Team]);
				}
			}
		}
	};
}

TEST_F(PredictionSwitchState, PermanentOpenClearsOldDeadline)
{
	m_World.Switchers()[1].m_aStatus[2] = false;
	ASSERT_TRUE(m_World.TrySetSwitchState(1, 2, TILE_SWITCHOPEN, 255));
	const auto &Switcher = m_World.Switchers()[1];
	EXPECT_TRUE(Switcher.m_aStatus[2]);
	EXPECT_EQ(Switcher.m_aEndTick[2], 0);
	EXPECT_EQ(Switcher.m_aType[2], TILE_SWITCHOPEN);
	EXPECT_EQ(Switcher.m_aLastUpdateTick[2], 100);
}

TEST_F(PredictionSwitchState, PermanentCloseClearsOldDeadline)
{
	ASSERT_TRUE(m_World.TrySetSwitchState(1, 2, TILE_SWITCHCLOSE, -1));
	const auto &Switcher = m_World.Switchers()[1];
	EXPECT_FALSE(Switcher.m_aStatus[2]);
	EXPECT_EQ(Switcher.m_aEndTick[2], 0);
	EXPECT_EQ(Switcher.m_aType[2], TILE_SWITCHCLOSE);
	EXPECT_EQ(Switcher.m_aLastUpdateTick[2], 100);
}

TEST_F(PredictionSwitchState, TimedOpenAtZeroDelayRemainsOpenUntilNextTick)
{
	ASSERT_TRUE(m_World.TrySetSwitchState(1, 2, TILE_SWITCHTIMEDOPEN, 0));
	m_World.Tick();
	ASSERT_TRUE(m_World.Switchers()[1].m_aStatus[2]);
	EXPECT_EQ(m_World.Switchers()[1].m_aEndTick[2], 101);
	m_World.m_GameTick = 101;
	m_World.Tick();
	EXPECT_FALSE(m_World.Switchers()[1].m_aStatus[2]);
	EXPECT_EQ(m_World.Switchers()[1].m_aEndTick[2], 0);
	EXPECT_EQ(m_World.Switchers()[1].m_aType[2], TILE_SWITCHCLOSE);
	EXPECT_EQ(m_World.Switchers()[1].m_aLastUpdateTick[2], 100);
}

TEST_F(PredictionSwitchState, TimedCloseReopensExactlyAtDeadline)
{
	ASSERT_TRUE(m_World.TrySetSwitchState(1, 2, TILE_SWITCHTIMEDCLOSE, 2));
	m_World.m_GameTick = 200;
	m_World.Tick();
	ASSERT_FALSE(m_World.Switchers()[1].m_aStatus[2]);
	EXPECT_EQ(m_World.Switchers()[1].m_aEndTick[2], 201);
	m_World.m_GameTick = 201;
	m_World.Tick();
	EXPECT_TRUE(m_World.Switchers()[1].m_aStatus[2]);
	EXPECT_EQ(m_World.Switchers()[1].m_aType[2], TILE_SWITCHOPEN);
	EXPECT_EQ(m_World.Switchers()[1].m_aLastUpdateTick[2], 100);
}

TEST_F(PredictionSwitchState, OverflowingTimedOpenKeepsWholeSwitchTable)
{
	m_World.m_GameTick = std::numeric_limits<int>::max();
	const auto Before = m_World.Switchers();
	EXPECT_FALSE(m_World.TrySetSwitchState(1, 2, TILE_SWITCHTIMEDOPEN, 0));
	ExpectUnchanged(Before);
}

TEST_F(PredictionSwitchState, OverflowingTimedCloseKeepsWholeSwitchTable)
{
	m_World.m_GameTick = std::numeric_limits<int>::max() - 50;
	const auto Before = m_World.Switchers();
	EXPECT_FALSE(m_World.TrySetSwitchState(1, 2, TILE_SWITCHTIMEDCLOSE, 1));
	ExpectUnchanged(Before);
}

TEST_F(PredictionSwitchState, ExactMaximumDeadlineExpiresNormally)
{
	m_World.m_GameTick = std::numeric_limits<int>::max() - 1;
	ASSERT_TRUE(m_World.TrySetSwitchState(1, 2, TILE_SWITCHTIMEDOPEN, 0));
	EXPECT_EQ(m_World.Switchers()[1].m_aEndTick[2], std::numeric_limits<int>::max());
	m_World.m_GameTick = std::numeric_limits<int>::max();
	m_World.Tick();
	EXPECT_FALSE(m_World.Switchers()[1].m_aStatus[2]);
}

TEST_F(PredictionSwitchState, FullMapDelayCanReachExactMaximumDeadline)
{
	m_World.m_GameTick = std::numeric_limits<int>::max() - 12751;
	ASSERT_TRUE(m_World.TrySetSwitchState(1, 2, TILE_SWITCHTIMEDCLOSE, 255));
	EXPECT_EQ(m_World.Switchers()[1].m_aEndTick[2], std::numeric_limits<int>::max());
}

TEST_F(PredictionSwitchState, PermanentSwitchAtMaximumTickDoesNotNeedFutureDeadline)
{
	m_World.m_GameTick = std::numeric_limits<int>::max();
	ASSERT_TRUE(m_World.TrySetSwitchState(1, 2, TILE_SWITCHOPEN, 0));
	EXPECT_EQ(m_World.Switchers()[1].m_aEndTick[2], 0);
	EXPECT_EQ(m_World.Switchers()[1].m_aLastUpdateTick[2], std::numeric_limits<int>::max());
}

TEST_F(PredictionSwitchState, InvalidNumbersDoNotWriteAnyEntry)
{
	const auto Before = m_World.Switchers();
	for(const int Number : {-1, 0, 3, std::numeric_limits<int>::max()})
	{
		EXPECT_FALSE(m_World.TrySetSwitchState(Number, 2, TILE_SWITCHOPEN, 0));
		ExpectUnchanged(Before);
	}
}

TEST_F(PredictionSwitchState, InvalidTeamDoesNotWriteAnyEntry)
{
	const auto Before = m_World.Switchers();
	for(const int Team : {-1, NUM_DDRACE_TEAMS, std::numeric_limits<int>::max()})
	{
		EXPECT_FALSE(m_World.TrySetSwitchState(1, Team, TILE_SWITCHOPEN, 0));
		ExpectUnchanged(Before);
	}
}

TEST_F(PredictionSwitchState, UnsupportedTypeKeepsWholeSwitchTable)
{
	const auto Before = m_World.Switchers();
	EXPECT_FALSE(m_World.TrySetSwitchState(1, 2, TILE_FREEZE, 0));
	ExpectUnchanged(Before);
}

TEST_F(PredictionSwitchState, NegativeTimedDelayKeepsWholeSwitchTable)
{
	const auto Before = m_World.Switchers();
	EXPECT_FALSE(m_World.TrySetSwitchState(1, 2, TILE_SWITCHTIMEDOPEN, -1));
	ExpectUnchanged(Before);
}

TEST_F(PredictionSwitchState, LargeDelayIsCheckedBeforeMultiplicationCanNarrow)
{
	const auto Before = m_World.Switchers();
	EXPECT_FALSE(m_World.TrySetSwitchState(1, 2, TILE_SWITCHTIMEDCLOSE, std::numeric_limits<int>::max()));
	ExpectUnchanged(Before);
}

TEST_F(PredictionSwitchState, UpdateLeavesOtherTeamsAndSwitchesAlone)
{
	const auto Before = m_World.Switchers();
	ASSERT_TRUE(m_World.TrySetSwitchState(1, 2, TILE_SWITCHCLOSE, 0));
	ExpectUnchanged(Before, 1, 2);
}

TEST_F(PredictionSwitchState, FailedUpdateCanBeRetriedAtRepresentableTick)
{
	m_World.m_GameTick = std::numeric_limits<int>::max();
	EXPECT_FALSE(m_World.TrySetSwitchState(1, 2, TILE_SWITCHTIMEDCLOSE, 0));
	m_World.m_GameTick = 100;
	ASSERT_TRUE(m_World.TrySetSwitchState(1, 2, TILE_SWITCHTIMEDCLOSE, 0));
	EXPECT_FALSE(m_World.Switchers()[1].m_aStatus[2]);
	EXPECT_EQ(m_World.Switchers()[1].m_aEndTick[2], 101);
}

TEST_F(PredictionSwitchState, PreparedSwitchUpdateDoesNotAllocate)
{
	bool Updated, Allocated;
	{
		CPredictionAllocationFailure Failure;
		Updated = m_World.TrySetSwitchState(1, 2, TILE_SWITCHTIMEDOPEN, 2);
		Allocated = Failure.Triggered();
	}
	EXPECT_TRUE(Updated);
	EXPECT_FALSE(Allocated);
	EXPECT_EQ(m_World.Switchers()[1].m_aEndTick[2], 201);
}
