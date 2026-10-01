#include <game/client/components/hud_media_island_logic.h>

#include <gtest/gtest.h>

TEST(QmHudRecordingPresentation, OpenScoreboardKeepsRecordingDetailsAboveTimeAndFrozenSummary)
{
	for(const bool Time : {false, true})
		for(const bool Frozen : {false, true})
		{
			SCOPED_TRACE(Time);
			SCOPED_TRACE(Frozen);
			const auto Open = QmHudRecordingPresentation(true, true, Time, Frozen);
			EXPECT_TRUE(Open.m_ShowRecordingText);
			EXPECT_FALSE(Open.m_ShowInfoStack);
			EXPECT_FLOAT_EQ(Open.RecordingTextAlpha(1.0f), 1.0f);
		}
}

TEST(QmHudRecordingPresentation, CloseAndRapidReopenCannotBorrowInfoTextAlpha)
{
	const auto Info = QmHudRecordingPresentation(false, true, true, false);
	EXPECT_TRUE(Info.m_ShowInfoStack);
	EXPECT_FLOAT_EQ(Info.RecordingTextAlpha(1.0f), 0.0f);
	for(const bool Expanded : {true, false, true, false})
	{
		SCOPED_TRACE(Expanded);
		const auto Frame = QmHudRecordingPresentation(true, Expanded, Expanded, false);
		EXPECT_EQ(Frame.m_ShowRecordingText, Expanded);
		EXPECT_FALSE(Frame.m_ShowInfoStack);
		EXPECT_FLOAT_EQ(Frame.RecordingTextAlpha(0.8f), Expanded ? 0.8f : 0.0f);
	}
}

TEST(QmHudRecordingPresentation, StoppedRecordingRestoresInfoAndSuppressesOutgoingRecordingText)
{
	const auto Recording = QmHudRecordingPresentation(true, true, true, true);
	ASSERT_TRUE(Recording.m_ShowRecordingText);
	const auto Stopped = QmHudRecordingPresentation(false, true, true, true);
	EXPECT_TRUE(Stopped.m_ShowInfoStack);
	EXPECT_FALSE(Stopped.m_ShowRecordingText);
	EXPECT_FLOAT_EQ(Stopped.RecordingTextAlpha(1.0f), 0.0f);
	const auto ClosedWithClock = QmHudRecordingPresentation(true, false, true, false);
	EXPECT_TRUE(ClosedWithClock.m_ShowInfoStack);
	EXPECT_FLOAT_EQ(ClosedWithClock.RecordingTextAlpha(1.0f), 0.0f);
}
