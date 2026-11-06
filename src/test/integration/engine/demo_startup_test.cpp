#include <gtest/gtest.h>
#include <test/support/demo_storage_state.h>

namespace
{
	class DemoStartupIntegration : public ::testing::Test
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

TEST_F(DemoStartupIntegration, PlayBeforeLoadReportsFailureWithoutReadingAnUnopenedFile)
{
	EXPECT_FALSE(m_Player.Play());
	EXPECT_FALSE(m_Player.IsPlaying());
}

TEST_F(DemoStartupIntegration, RejectedWarmupReportsFailureAndRetainsItsReason)
{
	ASSERT_NO_FATAL_FAILURE(Load({100}));
	EXPECT_FALSE(m_Player.Play());
	EXPECT_FALSE(m_Player.IsPlaying());
	EXPECT_STREQ(m_Player.ErrorMessage(), "Empty demo");
}

TEST_F(DemoStartupIntegration, TwoRecordedTicksCompleteTheExistingWarmup)
{
	ASSERT_NO_FATAL_FAILURE(Load({100, 200}));
	EXPECT_TRUE(m_Player.Play());
	EXPECT_TRUE(m_Player.IsPlaying());
	EXPECT_EQ(m_Player.Info()->m_PreviousTick, 100);
	EXPECT_EQ(m_Player.Info()->m_Info.m_CurrentTick, 200);
	EXPECT_EQ(m_Player.ErrorMessage()[0], '\0');
}

TEST_F(DemoStartupIntegration, RestartingAnAlreadyPrimedPlayerKeepsItsOpenPlayback)
{
	ASSERT_NO_FATAL_FAILURE(Load({100, 200, 300}));
	ASSERT_TRUE(m_Player.Play());
	EXPECT_TRUE(m_Player.Play());
	EXPECT_TRUE(m_Player.IsPlaying());
}

TEST_F(DemoStartupIntegration, PlayAfterStopDoesNotReopenTheFileOrEraseItsReason)
{
	ASSERT_NO_FATAL_FAILURE(Load({100, 200}));
	m_Player.Stop("stopped");
	EXPECT_FALSE(m_Player.Play());
	EXPECT_FALSE(m_Player.IsPlaying());
	EXPECT_STREQ(m_Player.ErrorMessage(), "stopped");
}

TEST_F(DemoStartupIntegration, RepeatedFailedStartDoesNotReviveTheStoppedPlayer)
{
	ASSERT_NO_FATAL_FAILURE(Load({100}));
	EXPECT_FALSE(m_Player.Play());
	EXPECT_FALSE(m_Player.Play());
	EXPECT_STREQ(m_Player.ErrorMessage(), "Empty demo");
}

TEST_F(DemoStartupIntegration, FailedStartCanLoadAndStartAnotherRecording)
{
	ASSERT_NO_FATAL_FAILURE(Load({100}));
	ASSERT_FALSE(m_Player.Play());
	ASSERT_NO_FATAL_FAILURE(Load({100, 200}));
	EXPECT_TRUE(m_Player.Play());
	EXPECT_TRUE(m_Player.IsPlaying());
	EXPECT_EQ(m_Player.ErrorMessage()[0], '\0');
}

TEST_F(DemoStartupIntegration, SeekReturnsFailureWhenItsWarmupStopsThePlayer)
{
	ASSERT_NO_FATAL_FAILURE(Load({100}));
	EXPECT_FALSE(m_Player.SetPos(100));
	EXPECT_FALSE(m_Player.IsPlaying());
	EXPECT_STREQ(m_Player.ErrorMessage(), "Empty demo");
}

TEST_F(DemoStartupIntegration, NextTickSeekFastPathAlsoPropagatesWarmupFailure)
{
	ASSERT_NO_FATAL_FAILURE(Load({0}));
	EXPECT_FALSE(m_Player.SetPos(0));
	EXPECT_FALSE(m_Player.IsPlaying());
	EXPECT_STREQ(m_Player.ErrorMessage(), "Empty demo");
}

TEST_F(DemoStartupIntegration, TruncatedWarmupPayloadReturnsFailureWithAReadError)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200}));
	auto Bytes = m_State.Read("source.demo");
	ASSERT_FALSE(Bytes.empty());
	Bytes.pop_back();
	m_State.Write("source.demo", Bytes);
	ASSERT_EQ(m_Player.Load(m_State.m_pStorage.get(), nullptr, "source.demo", IStorage::TYPE_SAVE, false), 0);
	EXPECT_FALSE(m_Player.Play());
	EXPECT_FALSE(m_Player.IsPlaying());
	EXPECT_STREQ(m_Player.ErrorMessage(), "Error reading chunk data");
}
