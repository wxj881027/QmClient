#include <game/client/components/system_media_controls_mpris_policy.h>

#include <gtest/gtest.h>

#include <limits>
using namespace SystemMediaControls;
TEST(QmMprisPolicy, SameBusNameWithNewOwnerRejectsOldCommands)
{
	CMprisSessionIdentity Identity;
	EXPECT_TRUE(Identity.Update("player", ":1.1"));
	const uint64_t Original = Identity.Generation();
	EXPECT_FALSE(Identity.Update("player", ":1.1"));
	EXPECT_TRUE(Identity.Accepts(Original));
	EXPECT_TRUE(Identity.Update("player", ":1.2"));
	EXPECT_FALSE(Identity.Accepts(Original));
	EXPECT_TRUE(Identity.Accepts(Identity.Generation()));
	Identity.Clear();
	EXPECT_FALSE(Identity.Accepts(Identity.Generation()));
	EXPECT_TRUE(Identity.Update("player", ":1.2"));
	EXPECT_GT(Identity.Generation(), Original);
}
TEST(QmMprisPolicy, ExternalTimelineValuesCannotOverflowConversion)
{
	EXPECT_EQ(MprisMicrosecondsTo100ns(-1), 0);
	EXPECT_EQ(MprisMicrosecondsTo100ns(std::numeric_limits<int64_t>::min()), 0);
	EXPECT_EQ(MprisMicrosecondsTo100ns(123), 1230);
	EXPECT_EQ(MprisMicrosecondsTo100ns(std::numeric_limits<int64_t>::max()), (std::numeric_limits<int64_t>::max() / 10) * 10);
}
TEST(QmMprisPolicy, PositionReadFailureKeepsOnlySameSessionThenRecovers)
{
	const SMprisPosition Previous{60000, 100, 4};
	const SMprisPosition Current{65000, 150, 5};
	EXPECT_EQ(MprisResolvePosition(false, false, Previous, {}).m_PositionMs, 60000);
	EXPECT_EQ(MprisResolvePosition(false, false, Previous, {}).m_UpdatedTick, 100);
	EXPECT_EQ(MprisResolvePosition(true, false, Previous, {}).m_PositionMs, 0);
	EXPECT_EQ(MprisResolvePosition(false, true, Previous, Current).m_PositionMs, 65000);
}
TEST(QmMprisPolicy, WaitingUsesShortSlicesAndOneSharedPollBudget)
{
	int64_t Now = 0;
	const CMprisPollBudget Budget(0, 45);
	int Calls = 0;
	EXPECT_FALSE(MprisWaitForReply(Budget, [] { return false; }, [&] { return Now; }, [] { return false; }, [&](int Slice) {
		EXPECT_LE(Slice, 20); Now += Slice; ++Calls; return true; }));
	EXPECT_EQ(Calls, 3);
	EXPECT_EQ(Now, 45);
	EXPECT_FALSE(MprisWaitForReply(Budget, [] { return false; }, [&] { return Now; }, [] { return false; }, [&](int) { ADD_FAILURE(); return true; }));
}
TEST(QmMprisPolicy, StopDuringFirstSlicePreventsNextDispatch)
{
	int64_t Now = 0;
	bool Stop = false;
	int Calls = 0;
	const CMprisPollBudget Budget(0, 1000);
	EXPECT_FALSE(MprisWaitForReply(Budget, [&] { return Stop; }, [&] { return Now; }, [] { return false; }, [&](int Slice) { ++Calls; Now += Slice; Stop = true; return true; }));
	EXPECT_EQ(Calls, 1);
}
TEST(QmMprisPolicy, DisconnectAbortsReplyWaitAndCompletedReplyReturns)
{
	const CMprisPollBudget Budget(0, 1000);
	EXPECT_FALSE(MprisWaitForReply(Budget, [] { return false; }, [] { return 0; }, [] { return false; }, [](int) { return false; }));
	EXPECT_TRUE(MprisWaitForReply(Budget, [] { return false; }, [] { return 0; }, [] { return true; }, [](int) { ADD_FAILURE(); return false; }));
}

TEST(QmMprisPolicy, TrackChangeWithinSameOwnerDoesNotReuseFailedOldPosition)
{
	CMprisTrackIdentity Track;
	SMprisPropertiesSnapshot First;
	First.m_TrackId = "/track/one";
	EXPECT_TRUE(Track.Update(First));
	EXPECT_FALSE(Track.Update(First));
	const SMprisPosition Previous{60000, 100, 4};
	EXPECT_EQ(MprisResolvePosition(Track.Update(First), false, Previous, {}).m_PositionMs, 60000);
	SMprisPropertiesSnapshot Second;
	Second.m_TrackId = "/track/two";
	EXPECT_EQ(MprisResolvePosition(Track.Update(Second), false, Previous, {}).m_PositionMs, 0);
	EXPECT_EQ(MprisResolvePosition(Track.Update(Second), true, {}, {5000, 200, 1}).m_PositionMs, 5000);
	Track.Clear();
	EXPECT_TRUE(Track.Update(Second));
}

TEST(QmMprisPolicy, MissingTrackIdUsesSeparateMetadataFieldsForIdentity)
{
	CMprisTrackIdentity Track;
	SMprisPropertiesSnapshot First;
	First.m_Title = "ab";
	First.m_Artist = "c";
	EXPECT_TRUE(Track.Update(First));
	EXPECT_FALSE(Track.Update(First));
	SMprisPropertiesSnapshot Second;
	Second.m_Title = "a";
	Second.m_Artist = "bc";
	EXPECT_TRUE(Track.Update(Second));
	EXPECT_TRUE(Track.Update({}));
	EXPECT_FALSE(Track.Update({}));
}

TEST(QmMprisPolicy, FreshPropertiesClearOldMetadataAndControlAvailability)
{
	SMprisPropertiesSnapshot Published;
	Published.m_Title = "old";
	Published.m_Artist = "artist";
	Published.m_Album = "album";
	Published.m_CanControl = true;
	Published.m_CanNext = true;
	Published.ApplyControlAvailability();
	EXPECT_TRUE(Published.m_CanNext);
	SMprisPropertiesSnapshot Received;
	Received.m_CanNext = true;
	Received.ApplyControlAvailability();
	EXPECT_FALSE(Received.m_CanNext);
	Published = Received;
	EXPECT_TRUE(Published.m_Title.empty());
	EXPECT_TRUE(Published.m_Artist.empty());
	EXPECT_TRUE(Published.m_Album.empty());
	EXPECT_FALSE(Published.m_CanNext);
}

TEST(QmMprisPolicy, NoTrackSentinelUsesMetadataAndResetsFailedOldPosition)
{
	CMprisTrackIdentity Track;
	SMprisPropertiesSnapshot First;
	First.m_TrackId = "/org/mpris/MediaPlayer2/TrackList/NoTrack";
	First.m_Title = "first";
	ASSERT_TRUE(Track.Update(First));
	ASSERT_FALSE(Track.Update(First));
	SMprisPropertiesSnapshot Second = First;
	Second.m_Title = "second";
	const bool Changed = Track.Update(Second);
	ASSERT_TRUE(Changed);
	EXPECT_EQ(MprisResolvePosition(Changed, false, {60000, 100, 4}, {}).m_PositionMs, 0);
	EXPECT_EQ(MprisResolvePosition(Track.Update(Second), true, {}, {5000, 200, 1}).m_PositionMs, 5000);
	Second.m_TrackId.clear();
	EXPECT_FALSE(Track.Update(Second));
}

TEST(QmMprisPolicy, UnresponsiveFirstPlayerDoesNotStarveHealthyPlayer)
{
	CMprisCandidateScheduler Scheduler;
	const std::vector<std::string> Names{"hung", "healthy"};
	int64_t Now = 0;
	const CMprisPollBudget Budget(0, 1000);
	std::vector<std::string> Visited;
	std::string Selected;
	Scheduler.Visit(Names, "", Budget, [&] { return Now; }, [] { return false; }, [&](const std::string &Name) {
		Visited.push_back(Name);
		if(Name == "hung")
		{
			const CMprisPollBudget Request(Now, MprisRequestTimeout(Budget.Remaining(Now, false), 200));
			EXPECT_FALSE(MprisWaitForReply(Request, [] { return false; }, [&] { return Now; }, [] { return false; }, [&](int Slice) { Now += Slice; return true; }));
			return true;
		}
		Selected = Name;
		return false; });
	EXPECT_EQ(Selected, "healthy");
	EXPECT_EQ(Visited, Names);
	EXPECT_GE(Budget.Remaining(Now, false), 200);
}

TEST(QmMprisPolicy, SharedBudgetRotatesAcrossManyFailuresAndReservesPositionTime)
{
	CMprisCandidateScheduler Scheduler;
	const std::vector<std::string> Names{"bad0", "bad1", "bad2", "bad3", "healthy"};
	std::string Selected;
	std::vector<std::string> Visited;
	for(int Round = 0; Round < 3 && Selected.empty(); ++Round)
	{
		int64_t Now = 0;
		const CMprisPollBudget Budget(0, 1000);
		Scheduler.Visit(Names, "", Budget, [&] { return Now; }, [] { return false; }, [&](const std::string &Name) {
			Visited.push_back(Name);
			if(Name == "healthy") { Selected = Name; return false; }
			const CMprisPollBudget Request(Now, MprisRequestTimeout(Budget.Remaining(Now, false), 200));
			EXPECT_FALSE(MprisWaitForReply(Request, [] { return false; }, [&] { return Now; }, [] { return false; }, [&](int Slice) { Now += Slice; return true; }));
			return true; });
		EXPECT_GE(Budget.Remaining(Now, false), 200);
	}
	EXPECT_EQ(Selected, "healthy");
	EXPECT_EQ(Visited, Names);
}

TEST(QmMprisPolicy, ActiveSourceIsFirstAndStopPreventsAnyLaterCandidates)
{
	CMprisCandidateScheduler Scheduler;
	int64_t Now = 0;
	bool Stop = false;
	const CMprisPollBudget Budget(0, 1000);
	std::vector<std::string> Visited;
	Scheduler.Visit({"other", "active", "later"}, "active", Budget, [&] { return Now; }, [&] { return Stop; }, [&](const std::string &Name) {
		Visited.push_back(Name); Stop = true; return true; });
	EXPECT_EQ(Visited, (std::vector<std::string>{"active"}));
}
