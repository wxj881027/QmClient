#include <gtest/gtest.h>
#include <test/support/demo_storage_state.h>

#include <limits>

namespace
{
	class CSeekReadOperations : public CDemoPlayer::CMapDataOperations
	{
	public:
		bool m_EofOnNextRead = false;
		bool m_ReportError = false;

		unsigned ReadChunkHeaderBytes(IOHANDLE File, void *pData, unsigned Size) noexcept override
		{
			if(m_EofOnNextRead)
			{
				m_EofOnNextRead = false;
				return 0;
			}
			return CMapDataOperations::ReadChunkHeaderBytes(File, pData, Size);
		}
		int Error(IOHANDLE File) noexcept override { return m_ReportError ? 1 : CMapDataOperations::Error(File); }
	};

	class DemoSeekIntegration : public ::testing::Test
	{
	protected:
		CDemoStorageState m_State;
		CSeekReadOperations m_ReadOperations;
		CDemoPlayer m_Player{&*m_State.m_pDelta, &*m_State.m_pDeltaSixup, false, &m_ReadOperations, CDemoPlayer::ETimeMode::UNCACHED};
		void SetUp() override
		{
			ASSERT_NE(m_State.m_pStorage, nullptr);
			CNetBase::Init();
		}
		void TearDown() override { m_Player.Stop(); }
		void Load(std::initializer_list<int> Ticks)
		{
			ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline(Ticks));
			ASSERT_EQ(m_Player.Load(m_State.m_pStorage.get(), "source.demo", IStorage::TYPE_SAVE, false), 0);
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

TEST_F(DemoSeekIntegration, SeekingWhileManuallyPausedDoesNotResumePlayback)
{
	ASSERT_NO_FATAL_FAILURE(Load({100, 200, 300, 400}));
	ASSERT_TRUE(m_Player.Play());
	m_Player.Pause();
	ASSERT_TRUE(m_Player.SetPos(301));
	EXPECT_TRUE(m_Player.Info()->m_Info.m_Paused);
	const int PausedTick = m_Player.Info()->m_Info.m_CurrentTick;
	m_Player.Update(false);
	EXPECT_EQ(m_Player.Info()->m_Info.m_CurrentTick, PausedTick);
	m_Player.Unpause();
	m_Player.Update(false);
	EXPECT_EQ(m_Player.Info()->m_Info.m_CurrentTick, 400);
}

TEST_F(DemoSeekIntegration, EarlyEndWhileSeekingReturnsFailureAndAllowsRetry)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200, 300, 400, 500}));
	for(const int Target : {301, 302})
	{
		SCOPED_TRACE(Target);
		ASSERT_EQ(m_Player.Load(m_State.m_pStorage.get(), "source.demo", IStorage::TYPE_SAVE, false), 0);
		ASSERT_TRUE(m_Player.Play());
		m_ReadOperations.m_EofOnNextRead = true;
		EXPECT_FALSE(m_Player.SetPos(Target));
		ASSERT_TRUE(m_Player.IsPlaying());
		EXPECT_TRUE(m_Player.BaseInfo()->m_Paused);
		EXPECT_TRUE(m_Player.SetPos(Target));
		m_Player.Stop();
	}
}

TEST_F(DemoSeekIntegration, ReadErrorAfterIndexedKeyframesRejectsLoadAndAllowsRetry)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200, 300}));
	m_ReadOperations.m_ReportError = true;
	EXPECT_NE(m_Player.Load(m_State.m_pStorage.get(), "source.demo", IStorage::TYPE_SAVE, false), 0);
	ASSERT_FALSE(m_Player.IsPlaying());
	m_ReadOperations.m_ReportError = false;
	EXPECT_EQ(m_Player.Load(m_State.m_pStorage.get(), "source.demo", IStorage::TYPE_SAVE, false), 0);
}

TEST_F(DemoSeekIntegration, SeekingAfterEndKeepsThePauseUntilExplicitlyResumed)
{
	ASSERT_NO_FATAL_FAILURE(Load({100, 200, 300, 400}));
	ASSERT_TRUE(m_Player.Play());
	m_Player.Update(false);
	ASSERT_TRUE(m_Player.Info()->m_Info.m_Paused);
	ASSERT_TRUE(m_Player.SetPos(201));
	EXPECT_TRUE(m_Player.Info()->m_Info.m_Paused);
	EXPECT_EQ(m_Player.Info()->m_Info.m_CurrentTick, 200);
	m_Player.Unpause();
	m_Player.Update(false);
	EXPECT_EQ(m_Player.Info()->m_Info.m_CurrentTick, 400);
}
