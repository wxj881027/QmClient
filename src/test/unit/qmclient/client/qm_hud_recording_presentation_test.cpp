#include <game/client/components/hud_media_island_logic.h>

#include <gtest/gtest.h>

TEST(QmHudRecordingPresentation, OpenScoreboardKeepsClockInsteadOfReplacingItWithRecording)
{
	for(const bool Time : {false, true})
		for(const bool Frozen : {false, true})
		{
			SCOPED_TRACE(Time);
			SCOPED_TRACE(Frozen);
			const auto Open = QmHudRecordingPresentation(true, true, Time, Frozen);
			EXPECT_TRUE(Open.m_ShowRecordingText);
			// 录像详情并入信息栈，不再独占状态区：三者任一存在就保留状态区，
			// 本地时间因此不会在计分板展开时消失。
			EXPECT_TRUE(Open.m_ShowInfoStack);
			EXPECT_FLOAT_EQ(Open.RecordingTextAlpha(1.0f), 1.0f);
		}
}

TEST(QmHudRecordingPresentation, ClosedScoreboardHidesRecordingTextButKeepsClock)
{
	const auto ClosedWithClock = QmHudRecordingPresentation(true, false, true, false);
	EXPECT_TRUE(ClosedWithClock.m_ShowInfoStack);
	EXPECT_FALSE(ClosedWithClock.m_ShowRecordingText);
	EXPECT_FLOAT_EQ(ClosedWithClock.RecordingTextAlpha(1.0f), 0.0f);

	const auto ClosedBare = QmHudRecordingPresentation(true, false, false, false);
	EXPECT_FALSE(ClosedBare.m_ShowInfoStack);
	EXPECT_FALSE(ClosedBare.m_ShowRecordingText);
}

TEST(QmHudRecordingPresentation, RapidReopenCannotBorrowInfoTextAlpha)
{
	const auto Info = QmHudRecordingPresentation(false, true, true, false);
	EXPECT_TRUE(Info.m_ShowInfoStack);
	EXPECT_FLOAT_EQ(Info.RecordingTextAlpha(1.0f), 0.0f);
	for(const bool Expanded : {true, false, true, false})
	{
		SCOPED_TRACE(Expanded);
		const auto Frame = QmHudRecordingPresentation(true, Expanded, true, false);
		EXPECT_EQ(Frame.m_ShowRecordingText, Expanded);
		EXPECT_TRUE(Frame.m_ShowInfoStack);
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
}

TEST(QmHudInfoStackLines, TimeStaysPrimaryWhenRecordingJoins)
{
	const auto Lines = QmHudInfoStackLines("13:22", "", "Auto 00:20");
	ASSERT_NE(Lines.m_pPrimary, nullptr);
	EXPECT_STREQ(Lines.m_pPrimary, "13:22");
	EXPECT_TRUE(Lines.m_HasSecondary);
	EXPECT_STREQ(Lines.m_aSecondary, "Auto 00:20");
}

TEST(QmHudInfoStackLines, FrozenSummaryRanksAfterTimeAndBeforeRecording)
{
	const auto Both = QmHudInfoStackLines("13:22", "12/16", "Auto 00:20");
	EXPECT_STREQ(Both.m_pPrimary, "13:22");
	EXPECT_TRUE(Both.m_HasSecondary);
	EXPECT_STREQ(Both.m_aSecondary, "12/16 · Auto 00:20");

	const auto FrozenOnly = QmHudInfoStackLines("", "12/16", "Auto 00:20");
	EXPECT_STREQ(FrozenOnly.m_pPrimary, "12/16");
	EXPECT_TRUE(FrozenOnly.m_HasSecondary);
	EXPECT_STREQ(FrozenOnly.m_aSecondary, "Auto 00:20");
}

TEST(QmHudInfoStackLines, SingleEntryStaysOnOneLine)
{
	const auto RecordingOnly = QmHudInfoStackLines("", "", "Auto 00:20");
	EXPECT_STREQ(RecordingOnly.m_pPrimary, "Auto 00:20");
	EXPECT_FALSE(RecordingOnly.m_HasSecondary);

	const auto FrozenOnly = QmHudInfoStackLines("", "12/16", "");
	EXPECT_STREQ(FrozenOnly.m_pPrimary, "12/16");
	EXPECT_FALSE(FrozenOnly.m_HasSecondary);

	const auto Empty = QmHudInfoStackLines("", "", "");
	EXPECT_EQ(Empty.m_pPrimary, nullptr);
	EXPECT_FALSE(Empty.m_HasSecondary);
}
