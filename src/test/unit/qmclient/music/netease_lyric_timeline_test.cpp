#include <game/client/components/qmclient/netease/netease_lyric_timeline.h>

#include <gtest/gtest.h>

using namespace NeteaseLyrics;

namespace
{
	STimeline MakeTimeline()
	{
		STimeline Timeline;
		Timeline.m_HasTiming = true;
		Timeline.m_vLines = {{1000, 2000, "one", {}}, {3000, 4000, "two", {}}, {5000, -1, "three", {}}};
		return Timeline;
	}
}

TEST(NeteaseLyricTimeline, SelectsBoundedLinesAndGaps)
{
	const STimeline Timeline = MakeTimeline();
	EXPECT_EQ(SelectCurrentLine(Timeline, 999).m_Index, -1);
	EXPECT_EQ(SelectCurrentLine(Timeline, 1000).m_Index, 0);
	EXPECT_EQ(SelectCurrentLine(Timeline, 1999).m_Index, 0);
	EXPECT_EQ(SelectCurrentLine(Timeline, 2000).m_Index, -1);
	EXPECT_EQ(SelectCurrentLine(Timeline, 2999).m_Index, -1);
	EXPECT_EQ(SelectCurrentLine(Timeline, 3000).m_Index, 1);
	EXPECT_EQ(SelectCurrentLine(Timeline, 5000).m_Index, 2);
	EXPECT_EQ(SelectCurrentLine(Timeline, 100000).m_Index, 2);
}

TEST(NeteaseLyricTimeline, SelectsLatestStartedLineAcrossInterludes)
{
	const STimeline Timeline = MakeTimeline();
	EXPECT_EQ(SelectLatestStartedLine(Timeline, 999).m_Index, -1);
	EXPECT_EQ(SelectLatestStartedLine(Timeline, 2000).m_Index, 0);
	EXPECT_FALSE(SelectLatestStartedLine(Timeline, 2000).m_InTimedRange);
	EXPECT_EQ(SelectLatestStartedLine(Timeline, 2999).m_Index, 0);
	EXPECT_EQ(SelectLatestStartedLine(Timeline, 3000).m_Index, 1);
	EXPECT_EQ(SelectLatestStartedLine(Timeline, 100000).m_Index, 2);
}

TEST(NeteaseLyricTimeline, DetectsEquivalentTimelineReports)
{
	STimeline Left = MakeTimeline();
	Left.m_vLines[0].m_vWords = {{1000, 1200, "o"}, {1200, 1500, "ne"}};
	const STimeline Right = Left;
	EXPECT_TRUE(AreTimelinesEquivalent(Left, Right));
}

TEST(NeteaseLyricTimeline, DetectsEveryDisplayRelevantTimelineChange)
{
	const STimeline Original = MakeTimeline();

	STimeline Changed = Original;
	Changed.m_HasTiming = false;
	EXPECT_FALSE(AreTimelinesEquivalent(Original, Changed));

	Changed = Original;
	Changed.m_vLines[0].m_Text = "changed";
	EXPECT_FALSE(AreTimelinesEquivalent(Original, Changed));

	Changed = Original;
	Changed.m_vLines[0].m_StartMs += 1;
	EXPECT_FALSE(AreTimelinesEquivalent(Original, Changed));

	Changed = Original;
	Changed.m_vLines[0].m_EndMs += 1;
	EXPECT_FALSE(AreTimelinesEquivalent(Original, Changed));

	Changed = Original;
	Changed.m_vLines[0].m_vWords.push_back({1000, 1200, "one"});
	EXPECT_FALSE(AreTimelinesEquivalent(Original, Changed));
}

TEST(NeteaseLyricTimeline, PlaybackAnchorPausesAndResumesWithoutRenderClockGuess)
{
	SPlaybackAnchor Anchor;
	const auto Start = std::chrono::steady_clock::time_point(std::chrono::milliseconds(100));
	Anchor.Update(1000, true, Start);
	EXPECT_EQ(Anchor.Estimate(Start + std::chrono::milliseconds(250)), 1250);
	Anchor.Update(1250, false, Start + std::chrono::milliseconds(250));
	EXPECT_EQ(Anchor.Estimate(Start + std::chrono::seconds(10)), 1250);
	Anchor.Update(1250, true, Start + std::chrono::seconds(10));
	EXPECT_EQ(Anchor.Estimate(Start + std::chrono::milliseconds(10500)), 1750);
}

TEST(NeteaseLyricTimeline, GenerationChangesImmediatelyOnSongSwitch)
{
	SGenerationState State;
	EXPECT_TRUE(State.UpdateSong(11));
	EXPECT_EQ(State.m_Generation, 1u);
	EXPECT_FALSE(State.UpdateSong(11));
	EXPECT_TRUE(State.UpdateSong(12));
	EXPECT_EQ(State.m_Generation, 2u);
	EXPECT_TRUE(State.UpdateSong(0));
	EXPECT_FALSE(State.m_HasSong);
}

TEST(NeteaseLyricTimeline, StaleTimestampIsRejected)
{
	EXPECT_TRUE(IsSnapshotStale(0, 100, 1000));
	EXPECT_TRUE(IsSnapshotStale(100, 1201, 1000));
	EXPECT_TRUE(IsSnapshotStale(200, 100, 1000));
	EXPECT_FALSE(IsSnapshotStale(100, 1100, 1000));
}
