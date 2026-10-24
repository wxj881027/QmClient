#include <game/client/components/qmclient/stutter_diagnostics.h>

#include <gtest/gtest.h>

#include <limits>

TEST(QmStutterEpisode, InvalidAndFastFramesDoNotStartAnEpisode)
{
	CQmStutterEpisodeTracker Tracker;
	for(double Duration : {0.0, -1.0, 1.0, QmStutterFrameBudgetMs(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		EXPECT_FALSE(Tracker.RecordFrame(1, Duration).m_Started);
		EXPECT_FALSE(Tracker.Active());
	}
}

TEST(QmStutterEpisode, RecoveryRequiresOneThousandContinuousFastMilliseconds)
{
	CQmStutterEpisodeTracker Tracker;
	EXPECT_TRUE(Tracker.RecordFrame(1, 6.0).m_Started);
	for(uint64_t Frame = 2; Frame < 335; ++Frame)
		EXPECT_FALSE(Tracker.RecordFrame(Frame, 3.0).m_FlushWindow);
	const auto Recovered = Tracker.RecordFrame(335, 3.0);
	EXPECT_TRUE(Recovered.m_Ended);
	EXPECT_EQ(Recovered.m_Reason, EQmStutterFlushReason::RECOVERED);
	EXPECT_FALSE(Tracker.Active());
}

TEST(QmStutterEpisode, AnotherSlowFrameRestartsTheRecoveryInterval)
{
	CQmStutterEpisodeTracker Tracker;
	Tracker.RecordFrame(1, 6.0);
	for(uint64_t Frame = 2; Frame <= 301; ++Frame)
		Tracker.RecordFrame(Frame, 3.0);
	Tracker.RecordFrame(302, 6.0);
	for(uint64_t Frame = 303; Frame < 636; ++Frame)
		EXPECT_FALSE(Tracker.RecordFrame(Frame, 3.0).m_FlushWindow);
	EXPECT_TRUE(Tracker.RecordFrame(636, 3.0).m_Ended);
	EXPECT_EQ(Tracker.LastBelowTargetFrame(), 302u);
}

TEST(QmStutterEpisode, PeriodicBoundaryKeepsEpisodeAndAdvancesSegment)
{
	CQmStutterEpisodeTracker Tracker;
	Tracker.RecordFrame(1, 9999.0);
	const auto Periodic = Tracker.RecordFrame(2, 6.0);
	EXPECT_TRUE(Periodic.m_FlushWindow);
	EXPECT_FALSE(Periodic.m_Ended);
	EXPECT_EQ(Periodic.m_Reason, EQmStutterFlushReason::PERIODIC);
	EXPECT_EQ(Periodic.m_Segment, 0u);
	EXPECT_EQ(Tracker.Segment(), 1u);
	EXPECT_EQ(Tracker.WindowStartFrame(), 3u);
	EXPECT_TRUE(Tracker.Active());
}

TEST(QmStutterEpisode, FinishingTwiceDoesNotCreateAnotherReportAndLaterEpisodeHasNewId)
{
	CQmStutterEpisodeTracker Tracker;
	Tracker.RecordFrame(1, 6.0);
	const auto First = Tracker.Flush(EQmStutterFlushReason::SHUTDOWN);
	EXPECT_TRUE(First.m_Ended);
	EXPECT_EQ(First.m_StutterId, 1u);
	EXPECT_FALSE(Tracker.Flush(EQmStutterFlushReason::SHUTDOWN).m_FlushWindow);
	const auto Next = Tracker.RecordFrame(2, 6.0);
	EXPECT_TRUE(Next.m_Started);
	EXPECT_EQ(Next.m_StutterId, 2u);
}
