#include <gtest/gtest.h>
#include <test/support/demo_storage_state.h>

#include <limits>

namespace
{
	class DemoSeekIntegration : public ::testing::Test
	{
	protected:
		CDemoStorageState m_State;
		CDemoPlayer m_Player{&*m_State.m_pDelta, &*m_State.m_pDeltaSixup, false, nullptr, CDemoPlayer::ETimeMode::UNCACHED};
		void SetUp() override
		{
			ASSERT_NE(m_State.m_pStorage, nullptr);
			CNetBase::Init();
		}
		void TearDown() override { m_Player.Stop(); }
		void Load(std::initializer_list<int> Ticks)
		{
			ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline(Ticks));
			ASSERT_EQ(m_Player.Load(m_State.m_pStorage.get(), nullptr, "source.demo", IStorage::TYPE_SAVE, false), 0);
		}
	};
}

TEST_F(DemoSeekIntegration, SeekingNearTheBeginningOfATwoTickDemoUsesAValidKeyframe)
{
	ASSERT_NO_FATAL_FAILURE(Load({0, 1}));
	ASSERT_TRUE(m_Player.SetPos(1));
	EXPECT_TRUE(m_Player.IsPlaying());
	EXPECT_EQ(m_Player.Info()->m_Info.m_CurrentTick, 1);
}

TEST_F(DemoSeekIntegration, AZeroSpanWithRepeatedTicksDoesNotProduceANonFiniteIndex)
{
	ASSERT_NO_FATAL_FAILURE(Load({100, 100}));
	ASSERT_TRUE(m_Player.SetPos(100));
	EXPECT_TRUE(m_Player.IsPlaying());
	EXPECT_EQ(m_Player.Info()->m_Info.m_CurrentTick, 100);
}

TEST_F(DemoSeekIntegration, SeekingBeforeTheFirstTickKeepsTheExistingClamp)
{
	ASSERT_NO_FATAL_FAILURE(Load({100, 101}));
	ASSERT_TRUE(m_Player.SetPos(std::numeric_limits<int>::min()));
	EXPECT_TRUE(m_Player.IsPlaying());
	EXPECT_EQ(m_Player.Info()->m_Info.m_FirstTick, 100);
	EXPECT_EQ(m_Player.Info()->m_Info.m_LastTick, 101);
}

TEST_F(DemoSeekIntegration, OrdinarySeekingStillPlaysTheWantedTick)
{
	ASSERT_NO_FATAL_FAILURE(Load({100, 150, 200, 250, 300}));
	ASSERT_TRUE(m_Player.SetPos(201));
	EXPECT_TRUE(m_Player.IsPlaying());
	EXPECT_EQ(m_Player.Info()->m_Info.m_CurrentTick, 200);
}

TEST_F(DemoSeekIntegration, RepeatedBackwardSeekingKeepsTheFileUsable)
{
	ASSERT_NO_FATAL_FAILURE(Load({100, 101}));
	ASSERT_TRUE(m_Player.SetPos(101));
	ASSERT_TRUE(m_Player.SetPos(100));
	EXPECT_TRUE(m_Player.IsPlaying());
	EXPECT_EQ(m_Player.ErrorMessage()[0], '\0');
}

TEST_F(DemoSeekIntegration, SeekingNearTheMaximumTickKeepsAValidKeyframe)
{
	ASSERT_NO_FATAL_FAILURE(Load({MAX_TICK - 2, MAX_TICK - 1}));
	ASSERT_TRUE(m_Player.SetPos(MAX_TICK - 1));
	EXPECT_TRUE(m_Player.IsPlaying());
	EXPECT_EQ(m_Player.Info()->m_Info.m_CurrentTick, MAX_TICK - 1);
}
