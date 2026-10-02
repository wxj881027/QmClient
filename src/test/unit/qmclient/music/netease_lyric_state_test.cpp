#include <game/client/components/qmclient/netease/netease_lyric_state.h>

#include <gtest/gtest.h>

using namespace NeteaseLyrics;

namespace
{
	STimeline OneLine(const char *pText)
	{
		STimeline Timeline;
		Timeline.m_HasTiming = true;
		Timeline.m_vLines.push_back({0, 5000, pText, {}});
		return Timeline;
	}
}

TEST(NeteaseLyricState, ClearsImmediatelyWhenSongChanges)
{
	CLyricState State;
	ASSERT_TRUE(State.UpdateSong(100));
	ASSERT_TRUE(State.ApplyTimeline(ESource::Frontend, OneLine("song A"), 1000));
	EXPECT_TRUE(State.Snapshot().m_LyricValid);
	ASSERT_TRUE(State.UpdateSong(200));
	EXPECT_FALSE(State.Snapshot().m_LyricValid);
	EXPECT_TRUE(State.Snapshot().m_CurrentLyric.empty());
	EXPECT_EQ(State.Snapshot().m_SongId, 200u);
}

TEST(NeteaseLyricState, HigherPrioritySourceCannotBeOverwrittenByFallback)
{
	CLyricState State;
	State.UpdateSong(1);
	ASSERT_TRUE(State.ApplyTimeline(ESource::Frontend, OneLine("frontend"), 100));
	EXPECT_FALSE(State.ApplyTimeline(ESource::DesktopLyricsFallback, OneLine("gdi"), 100));
	EXPECT_EQ(State.Snapshot().m_CurrentLyric, "frontend");
}

TEST(NeteaseLyricState, FrontendCanReplaceGdiFallback)
{
	CLyricState State;
	State.UpdateSong(1);
	ASSERT_TRUE(State.ApplyTimeline(ESource::DesktopLyricsFallback, OneLine("gdi"), 100));
	ASSERT_TRUE(State.ApplyTimeline(ESource::Frontend, OneLine("frontend"), 100));
	EXPECT_EQ(State.Snapshot().m_CurrentLyric, "frontend");
}

TEST(NeteaseLyricState, PauseKeepsCurrentLineAndStopClearsIt)
{
	CLyricState State;
	State.UpdateSong(1);
	State.ApplyTimeline(ESource::Frontend, OneLine("line"), 100);
	State.UpdatePosition(200);
	EXPECT_TRUE(State.Snapshot().m_LyricValid);
	State.MarkStopped();
	EXPECT_FALSE(State.Snapshot().m_LyricValid);
	EXPECT_FALSE(State.Snapshot().m_HasSong);
}

TEST(NeteaseLyricState, AcceptsKnownStartWithUnknownLastLineEnd)
{
	CLyricState State;
	State.UpdateSong(1);
	ASSERT_TRUE(State.ApplyCurrentLine(ESource::Frontend, "last line", 5000, -1, 6000));
	EXPECT_TRUE(State.Snapshot().m_LyricValid);
	EXPECT_EQ(State.Snapshot().m_LineStartMs, 5000);
	EXPECT_EQ(State.Snapshot().m_LineEndMs, -1);
}

TEST(NeteaseLyricState, DefersMismatchedLyricsWhileProgressIdentityIsFresh)
{
	EXPECT_EQ(DecideLyricSongReport(100, true, 200, 1000, 1500, 3000), ELyricSongDecision::Defer);
	EXPECT_EQ(DecideLyricSongReport(100, true, 100, 1000, 1500, 3000), ELyricSongDecision::ApplyCurrent);
	EXPECT_EQ(DecideLyricSongReport(100, true, 200, 1000, 5001, 3000), ELyricSongDecision::SwitchSong);
}

TEST(NeteaseLyricState, LyricsCanEstablishIdentityWithoutProgress)
{
	EXPECT_EQ(DecideLyricSongReport(0, false, 200, 0, 1000, 3000), ELyricSongDecision::SwitchSong);
	EXPECT_EQ(DecideLyricSongReport(0, false, 0, 0, 1000, 3000), ELyricSongDecision::Reject);
	EXPECT_EQ(DecideLyricSongReport(100, true, 0, 1000, 1500, 3000), ELyricSongDecision::ApplyCurrent);
}

TEST(NeteaseLyricState, DetectsStableSmtcMediaIdentityTransitions)
{
	EXPECT_FALSE(IsMeaningfulMediaIdentityChange("", "", "song A", ""));
	EXPECT_FALSE(IsMeaningfulMediaIdentityChange("song A", "", "song A", "artist A"));
	EXPECT_TRUE(IsMeaningfulMediaIdentityChange("song A", "artist A", "song B", ""));
	EXPECT_TRUE(IsMeaningfulMediaIdentityChange("song A", "artist A", "song A", "artist B"));
	EXPECT_FALSE(IsMeaningfulMediaIdentityChange("song A", "artist A", "", ""));
}

TEST(NeteaseLyricState, BlocksOldBridgeIdentityAfterSmtcSongTransition)
{
	EXPECT_TRUE(IsBridgeIdentityStillBlocked(100, 5, 100, 5));
	EXPECT_TRUE(IsBridgeIdentityStillBlocked(100, 0, 100, 9));
	EXPECT_TRUE(IsBridgeIdentityStillBlocked(100, 5, 0, 5));
	EXPECT_TRUE(IsBridgeIdentityStillBlocked(100, 5, 0, 0));
	EXPECT_FALSE(IsBridgeIdentityStillBlocked(100, 5, 0, 6));
	EXPECT_TRUE(IsBridgeIdentityStillBlocked(100, 6, 100, 5));
	EXPECT_FALSE(IsBridgeIdentityStillBlocked(100, 6, 100, 7));
	EXPECT_FALSE(IsBridgeIdentityStillBlocked(100, 5, 200, 5));
	EXPECT_FALSE(IsBridgeIdentityStillBlocked(100, 5, 100, 6));
	EXPECT_FALSE(IsBridgeIdentityStillBlocked(0, 0, 100, 1));
}

TEST(NeteaseLyricState, BridgeAdvanceBeforeSmtcMetadataDoesNotBlockTheNewSong)
{
	EXPECT_EQ(DecideSmtcBridgeSync(100, 5, 200, 6), ESmtcBridgeSyncDecision::BRIDGE_ALREADY_ADVANCED);
	EXPECT_EQ(DecideSmtcBridgeSync(100, 5, 100, 6), ESmtcBridgeSyncDecision::BRIDGE_ALREADY_ADVANCED);
	EXPECT_EQ(DecideSmtcBridgeSync(0, 0, 200, 1), ESmtcBridgeSyncDecision::BRIDGE_ALREADY_ADVANCED);
}

TEST(NeteaseLyricState, SmtcAdvanceBeforeBridgeWaitsForTheOldIdentityToChange)
{
	EXPECT_EQ(DecideSmtcBridgeSync(100, 5, 100, 5), ESmtcBridgeSyncDecision::WAIT_FOR_BRIDGE_ADVANCE);
	EXPECT_EQ(DecideSmtcBridgeSync(100, 0, 100, 9), ESmtcBridgeSyncDecision::WAIT_FOR_BRIDGE_ADVANCE);
	EXPECT_EQ(DecideSmtcBridgeSync(100, 5, 0, 6), ESmtcBridgeSyncDecision::WAIT_FOR_BRIDGE_ADVANCE);
}

TEST(NeteaseLyricState, BridgeIdentityWaitHasAFiniteDeadline)
{
	EXPECT_TRUE(ShouldWaitForBridgeIdentity(100, 5, 100, 5, 1000, 4000, 3000));
	EXPECT_FALSE(ShouldWaitForBridgeIdentity(100, 5, 100, 5, 1000, 4001, 3000));
	EXPECT_FALSE(ShouldWaitForBridgeIdentity(100, 5, 200, 6, 1000, 1100, 3000));
	EXPECT_FALSE(ShouldWaitForBridgeIdentity(100, 5, 100, 5, 1000, 999, 3000));
}

TEST(NeteaseLyricState, PausedLyricRetentionHasFiniteGraceWindow)
{
	EXPECT_TRUE(ShouldPreservePausedLyric(true, true, true, 1000, 2500, 1500));
	EXPECT_FALSE(ShouldPreservePausedLyric(true, true, true, 1000, 2501, 1500));
	EXPECT_FALSE(ShouldPreservePausedLyric(false, true, true, 1000, 1100, 1500));
	EXPECT_FALSE(ShouldPreservePausedLyric(true, false, true, 1000, 1100, 1500));
	EXPECT_FALSE(ShouldPreservePausedLyric(true, true, true, 1000, 900, 1500));
}

TEST(NeteaseLyricState, TemporaryBridgeReadFailurePreservesPlayingOrPausedState)
{
	EXPECT_TRUE(ShouldPreserveBridgeState(true, 1000, 2500, 1500));
	EXPECT_FALSE(ShouldPreserveBridgeState(true, 1000, 2501, 1500));
	EXPECT_FALSE(ShouldPreserveBridgeState(false, 1000, 1100, 1500));
	EXPECT_FALSE(ShouldPreserveBridgeState(true, 0, 1100, 1500));
	EXPECT_FALSE(ShouldPreserveBridgeState(true, 1000, 900, 1500));
}

TEST(NeteaseLyricState, PausedLyricSeekOnlyWhenPositionAnchorChanges)
{
	EXPECT_TRUE(HasPausedLyricSeek(true, true, true, 2500, true, 1000));
	EXPECT_FALSE(HasPausedLyricSeek(true, true, false, 2500, true, 1000));
	EXPECT_FALSE(HasPausedLyricSeek(true, true, true, 1000, true, 1000));
	EXPECT_FALSE(HasPausedLyricSeek(false, true, true, 2500, true, 1000));
	EXPECT_FALSE(HasPausedLyricSeek(true, false, true, 2500, true, 1000));
	EXPECT_FALSE(HasPausedLyricSeek(true, true, true, 2500, false, 0));
}

TEST(NeteaseLyricState, UnknownFrontendPlaybackStateDoesNotClearConnectedLyrics)
{
	EXPECT_TRUE(ShouldPreserveConnectedLyric(true, false, false, true, true));
	EXPECT_TRUE(ShouldPreserveConnectedLyric(true, true, false, true, true));
	EXPECT_FALSE(ShouldPreserveConnectedLyric(true, true, true, true, true));
	EXPECT_FALSE(ShouldPreserveConnectedLyric(false, false, false, true, true));
	EXPECT_FALSE(ShouldPreserveConnectedLyric(true, false, false, false, true));
	EXPECT_FALSE(ShouldPreserveConnectedLyric(true, false, false, true, false));
}
