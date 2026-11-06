#include <game/client/components/qmclient/ghost_manual.h>
#include <game/client/components/qmclient/online_replay_player.h>

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <limits>

namespace
{
	struct SFrame
	{
		int m_Tick;
		int m_AttackTick = 0;
	};
	struct STrack
	{
		qm_ghost_path::CChunkedPath<SFrame> m_Path;
		int m_StartTick = 0;
	};
	class QmGhostManualPath : public ::testing::Test
	{
	protected:
		std::array<STrack, 2> m_aTracks;
		qm_ghost_manual::CPlan<2> m_Plan;
		void Fill(int Slot, int Start, std::initializer_list<int> Ticks)
		{
			m_aTracks[Slot].m_StartTick = Start;
			for(int Tick : Ticks)
				ASSERT_TRUE(m_aTracks[Slot].m_Path.Add({Tick}));
			ASSERT_TRUE(qm_ghost_timeline::Prepare(m_aTracks[Slot].m_Path, true, m_aTracks[Slot].m_StartTick));
		}
		bool Prepare(int First)
		{
			return m_Plan.Prepare({0, 1}, [this](int Slot) { return &m_aTracks[Slot]; }, First);
		}
	};
}

TEST_F(QmGhostManualPath, SparsePrimaryStepsWithinTheLongestMemberTimeline)
{
	ASSERT_NO_FATAL_FAILURE(Fill(0, 100, {100, 110, 130}));
	ASSERT_NO_FATAL_FAILURE(Fill(1, 200, {200, 260}));
	ASSERT_TRUE(Prepare(1000));
	COnlineReplayClock Clock;
	Clock.Start(5.0, m_Plan.m_Range.Duration(), 50);
	Clock.Seek(10, 5.0);
	Clock.SetPlaying(false, 5.0);
	const int Next = OnlineReplayAdjacentTick(m_Plan.m_vSampleTicks, Clock.Tick(5.0), IDemoPlayer::TICK_NEXT);
	EXPECT_EQ(Next, 30);
	EXPECT_EQ(m_Plan.m_Range.ToAbsolute(Next), 1030);
	Clock.Seek(m_Plan.m_Range.ToRelative(1060), 5.0);
	EXPECT_EQ(Clock.Tick(5.0), 60);
}

TEST_F(QmGhostManualPath, PrerollAdjacentSampleClampsToSourceStartWithoutRebasingMarkers)
{
	ASSERT_NO_FATAL_FAILURE(Fill(0, 100, {90, 100, 120}));
	ASSERT_TRUE(Prepare(500));
	const int Previous = OnlineReplayAdjacentTick(m_Plan.m_vSampleTicks, 0, IDemoPlayer::TICK_PREVIOUS);
	EXPECT_EQ(Previous, -10);
	EXPECT_EQ(m_Plan.m_Range.ToAbsolute(Previous), 500);
	EXPECT_EQ(m_Plan.m_Range.ToRelative(510), 10);
}

TEST_F(QmGhostManualPath, TwoRealSparseRecordsCanReachTheLargestAbsoluteTick)
{
	const int Duration = std::numeric_limits<int>::max() - 100;
	ASSERT_NO_FATAL_FAILURE(Fill(0, 0, {0, Duration}));
	ASSERT_TRUE(Prepare(100));
	COnlineReplayClock Clock;
	Clock.Start(0.0, m_Plan.m_Range.Duration(), 50);
	Clock.Seek(m_Plan.m_Range.ToRelative(std::numeric_limits<int>::max()), 0.0);
	EXPECT_EQ(Clock.Tick(0.0), Duration);
	EXPECT_EQ(m_Plan.m_Range.ToAbsolute(Clock.Tick(0.0)), std::numeric_limits<int>::max());
}

TEST_F(QmGhostManualPath, UnrepresentableMetadataDoesNotReplaceThePreviousRealTrackPlan)
{
	ASSERT_NO_FATAL_FAILURE(Fill(0, 0, {0, 10}));
	ASSERT_TRUE(Prepare(100));
	const int *pOldSamples = m_Plan.m_vSampleTicks.data();
	ASSERT_NO_FATAL_FAILURE(Fill(1, 0, {0, std::numeric_limits<int>::max()}));
	EXPECT_FALSE(Prepare(1));
	EXPECT_EQ(m_Plan.m_vSampleTicks.data(), pOldSamples);
	EXPECT_EQ(m_Plan.m_Range.First(), 100);
	EXPECT_EQ(m_Plan.m_Range.Last(), 110);
	EXPECT_EQ(m_aTracks[1].m_Path.Get(1)->m_Tick, std::numeric_limits<int>::max());
}
