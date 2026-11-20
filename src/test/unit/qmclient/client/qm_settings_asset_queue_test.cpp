#include <game/client/components/qmclient/settings_asset_queue.h>
#include <game/client/components/settings_resource_jobs.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/support/rejectable_allocation_resource.h>

#include <deque>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
	const auto Equal = [](const auto &Left, const auto &Right) { return Left == Right; };
	const auto Key = [](const auto &Entry) { return Entry; };
	using TQueue = std::deque<std::string, CBorrowedAllocator<std::string>>;
	using TSet = std::unordered_set<std::string, std::hash<std::string>, std::equal_to<std::string>, CBorrowedAllocator<std::string>>;
}

TEST(QmSettingsAssetQueue, HandleAllocationFailureDoesNotCallStartOrChangeOldRecords)
{
	std::deque<std::string> Queue{"old"};
	const std::string Name(256, 'x');
	int Starts = 0;
	bool Tracked, Failed;
	{
		CPredictionAllocationFailure Failure;
		Tracked = SettingsAssetQueueTryStart(Queue, false, [&] { return Name; }, Equal, [&] { ++Starts; return true; });
		Failed = Failure.Triggered();
	}
	EXPECT_TRUE(Failed);
	EXPECT_FALSE(Tracked);
	EXPECT_EQ(Starts, 0);
	EXPECT_EQ(Queue, (std::deque<std::string>{"old"}));
}

TEST(QmSettingsAssetQueue, StartObservesPreparedRecordAndReturnsAcceptedOwner)
{
	std::deque<std::string> Queue{"old"};
	auto pOwner = std::make_shared<int>(7);
	bool Observed = false;
	auto pAccepted = SettingsAssetQueueTryStart(Queue, false, [] { return std::string("new"); }, Equal, [&] {
		Observed = Queue.size() == 2 && Queue.back() == "new";
		return pOwner; });
	EXPECT_TRUE(Observed);
	EXPECT_EQ(pAccepted, pOwner);
	EXPECT_EQ(Queue, (std::deque<std::string>{"old", "new"}));
}

TEST(QmSettingsAssetQueue, NewHighPriorityRecordPrecedesExistingRecords)
{
	std::deque<std::string> Queue{"first", "second"};
	ASSERT_TRUE(SettingsAssetQueueTryStart(Queue, true, [] { return std::string("visible"); }, Equal, [] { return true; }));
	EXPECT_EQ(Queue, (std::deque<std::string>{"visible", "first", "second"}));
}

TEST(QmSettingsAssetQueue, RejectionRemovesOnlyNewBackRecord)
{
	std::deque<std::string> Queue{"old"};
	EXPECT_FALSE(SettingsAssetQueueTryStart(Queue, false, [] { return std::string("new"); }, Equal, [] { return std::shared_ptr<int>(); }));
	EXPECT_EQ(Queue, (std::deque<std::string>{"old"}));
}

TEST(QmSettingsAssetQueue, RejectionRemovesOnlyNewFrontRecord)
{
	std::deque<std::string> Queue{"old"};
	EXPECT_FALSE(SettingsAssetQueueTryStart(Queue, true, [] { return std::string("new"); }, Equal, [] { return false; }));
	EXPECT_EQ(Queue, (std::deque<std::string>{"old"}));
}

TEST(QmSettingsAssetQueue, SubmissionAllocationFailureRollsBackAndAllowsRetry)
{
	std::deque<std::string> Queue{"old"};
	EXPECT_FALSE(SettingsAssetQueueTryStart(Queue, false, [] { return std::string("new"); }, Equal, []() -> bool { throw std::bad_alloc(); }));
	EXPECT_EQ(Queue, (std::deque<std::string>{"old"}));
	ASSERT_TRUE(SettingsAssetQueueTryStart(Queue, false, [] { return std::string("new"); }, Equal, [] { return true; }));
	EXPECT_EQ(Queue, (std::deque<std::string>{"old", "new"}));
}

TEST(QmSettingsAssetQueue, SubmissionLengthFailureRollsBackNewRecord)
{
	std::deque<std::string> Queue{"old"};
	EXPECT_FALSE(SettingsAssetQueueTryStart(Queue, true, [] { return std::string("new"); }, Equal, []() -> bool { throw std::length_error("full"); }));
	EXPECT_EQ(Queue, (std::deque<std::string>{"old"}));
}

TEST(QmSettingsAssetQueue, UnknownSubmissionExceptionRollsBackBeforePropagating)
{
	std::deque<std::string> Queue{"old"};
	EXPECT_THROW(SettingsAssetQueueTryStart(Queue, false, [] { return std::string("new"); }, Equal, []() -> bool { throw 7; }), int);
	EXPECT_EQ(Queue, (std::deque<std::string>{"old"}));
}

TEST(QmSettingsAssetQueue, ExistingOwnerCanBeRetrackedWithoutDuplicateRecord)
{
	std::deque<std::string> Queue{"asset"};
	auto pOwner = std::make_shared<int>(9);
	auto pTracked = SettingsAssetQueueTryStart(Queue, false, [] { return std::string("asset"); }, Equal, [&] { return pOwner; });
	EXPECT_EQ(pTracked, pOwner);
	EXPECT_EQ(Queue, (std::deque<std::string>{"asset"}));
}

TEST(QmSettingsAssetQueue, RejectedExistingRecordKeepsItsOldOrder)
{
	std::deque<std::string> Queue{"old", "asset", "last"};
	EXPECT_FALSE(SettingsAssetQueueTryStart(Queue, true, [] { return std::string("asset"); }, Equal, [] { return false; }));
	EXPECT_EQ(Queue, (std::deque<std::string>{"old", "asset", "last"}));
}

TEST(QmSettingsAssetQueue, AcceptedExistingRecordPromotesWithoutDuplicatingOthers)
{
	std::deque<std::string> Queue{"old", "asset", "last"};
	ASSERT_TRUE(SettingsAssetQueueTryStart(Queue, true, [] { return std::string("asset"); }, Equal, [] { return true; }));
	EXPECT_EQ(Queue, (std::deque<std::string>{"asset", "old", "last"}));
}

TEST(QmSettingsAssetQueue, ExistingHighPriorityFrontCanRetainItsPlace)
{
	std::deque<std::string> Queue{"visible-first", "visible-second"};
	ASSERT_TRUE(SettingsAssetQueueTryStart(Queue, true, [] { return std::string("visible-second"); }, Equal, [] { return true; }, false));
	EXPECT_EQ(Queue, (std::deque<std::string>{"visible-first", "visible-second"}));
}

TEST(QmSettingsAssetQueue, PromotionDoesNotAllocateOrLoseLongNames)
{
	const std::string First(256, 'a'), Selected(256, 'b'), Last(256, 'c');
	std::deque<std::string> Queue{First, Selected, Last};
	size_t Attempts;
	{
		CPredictionAllocationFailure Failure;
		SettingsAssetQueuePromote(Queue, std::next(Queue.begin()));
		Attempts = Failure.Attempts();
	}
	EXPECT_EQ(Attempts, 0u);
	EXPECT_EQ(Queue, (std::deque<std::string>{Selected, First, Last}));
}

TEST(QmSettingsAssetQueue, MissingPromotionTargetKeepsQueueUnchanged)
{
	std::deque<std::string> Queue{"old"};
	SettingsAssetQueuePromote(Queue, Queue.end());
	EXPECT_EQ(Queue, (std::deque<std::string>{"old"}));
}

TEST(QmSettingsAssetQueue, MarkerAllocationFailureDoesNotCallStart)
{
	CRejectableAllocationResource Memory;
	TSet Markers(0, std::hash<std::string>(), std::equal_to<std::string>(), &Memory);
	std::deque<std::string> Queue{"old"};
	Memory.m_Reject = true;
	int Starts = 0;
	EXPECT_FALSE(SettingsAssetQueueTryStartKeyed(Queue, Markers, false, [] { return std::string("new"); }, Key, Equal, [&] { ++Starts; return true; }));
	EXPECT_TRUE(Markers.empty());
	EXPECT_EQ(Queue, (std::deque<std::string>{"old"}));
	EXPECT_EQ(Starts, 0);
}

TEST(QmSettingsAssetQueue, QueueNodeFailureRollsBackOnlyItsNewMarker)
{
	CRejectableAllocationResource Memory;
	TQueue Queue(&Memory);
	std::unordered_set<std::string> Markers;
	Memory.m_Reject = true;
	bool Rejected = false;
	for(int Index = 0; Index < 1024 && !Rejected; ++Index)
	{
		const std::string Name = std::to_string(Index);
		const size_t Before = Queue.size();
		int Starts = 0;
		const bool Tracked = SettingsAssetQueueTryStartKeyed(Queue, Markers, false, [&] { return Name; }, Key, Equal, [&] { ++Starts; return true; });
		if(!Tracked)
		{
			Rejected = true;
			EXPECT_EQ(Starts, 0);
			EXPECT_EQ(Queue.size(), Before);
			EXPECT_EQ(Markers.size(), Before);
			EXPECT_EQ(Markers.count(Name), 0u);
		}
	}
	EXPECT_TRUE(Rejected);
}

TEST(QmSettingsAssetQueue, KeyPreparationFailurePreservesQueueAndMarkers)
{
	std::deque<std::string> Queue{"old"};
	std::unordered_set<std::string> Markers{"old"};
	const std::string LongKey(256, 'k');
	bool Tracked, Failed;
	int Starts = 0;
	{
		CPredictionAllocationFailure Failure;
		Tracked = SettingsAssetQueueTryStartKeyed(Queue, Markers, false, [] { return std::string("new"); }, [&](const auto &) { return LongKey; }, Equal, [&] { ++Starts; return true; });
		Failed = Failure.Triggered();
	}
	EXPECT_TRUE(Failed);
	EXPECT_FALSE(Tracked);
	EXPECT_EQ(Starts, 0);
	EXPECT_EQ(Queue, (std::deque<std::string>{"old"}));
	EXPECT_EQ(Markers, (std::unordered_set<std::string>{"old"}));
}

TEST(QmSettingsAssetQueue, KeyedStartObservesBothRecordAndMarker)
{
	std::deque<std::string> Queue;
	std::unordered_set<std::string> Markers;
	bool Observed = false;
	ASSERT_TRUE(SettingsAssetQueueTryStartKeyed(Queue, Markers, true, [] { return std::string("new"); }, Key, Equal, [&] {
		Observed = Queue.size() == 1 && Queue.front() == "new" && Markers.count("new") == 1;
		return true; }));
	EXPECT_TRUE(Observed);
}

TEST(QmSettingsAssetQueue, KeyedRejectionAllowsRetryWithSameIdentity)
{
	std::deque<std::string> Queue{"old"};
	std::unordered_set<std::string> Markers{"old"};
	EXPECT_FALSE(SettingsAssetQueueTryStartKeyed(Queue, Markers, true, [] { return std::string("new"); }, Key, Equal, [] { return false; }));
	EXPECT_EQ(Queue, (std::deque<std::string>{"old"}));
	EXPECT_EQ(Markers, (std::unordered_set<std::string>{"old"}));
	ASSERT_TRUE(SettingsAssetQueueTryStartKeyed(Queue, Markers, true, [] { return std::string("new"); }, Key, Equal, [] { return true; }));
	EXPECT_EQ(Queue, (std::deque<std::string>{"new", "old"}));
	EXPECT_EQ(Markers.size(), 2u);
}

TEST(QmSettingsAssetQueue, KeyedUnknownExceptionRestoresBothContainers)
{
	std::deque<std::string> Queue{"old"};
	std::unordered_set<std::string> Markers{"old"};
	EXPECT_THROW(SettingsAssetQueueTryStartKeyed(Queue, Markers, false, [] { return std::string("new"); }, Key, Equal, []() -> bool { throw 7; }), int);
	EXPECT_EQ(Queue, (std::deque<std::string>{"old"}));
	EXPECT_EQ(Markers, (std::unordered_set<std::string>{"old"}));
}

TEST(QmSettingsAssetQueue, PreexistingMarkerWithoutNodeCanBeRepaired)
{
	std::deque<std::string> Queue;
	std::unordered_set<std::string> Markers{"asset"};
	ASSERT_TRUE(SettingsAssetQueueTryStartKeyed(Queue, Markers, false, [] { return std::string("asset"); }, Key, Equal, [] { return true; }));
	ASSERT_TRUE(SettingsAssetQueueTryStartKeyed(Queue, Markers, true, [] { return std::string("asset"); }, Key, Equal, [] { return true; }));
	EXPECT_EQ(Queue, (std::deque<std::string>{"asset"}));
	EXPECT_EQ(Markers.size(), 1u);
}

TEST(QmSettingsAssetQueue, RejectedRepairRetainsOldMarkerForLaterRetry)
{
	std::deque<std::string> Queue;
	std::unordered_set<std::string> Markers{"asset"};
	EXPECT_FALSE(SettingsAssetQueueTryStartKeyed(Queue, Markers, false, [] { return std::string("asset"); }, Key, Equal, [] { return false; }));
	EXPECT_TRUE(Queue.empty());
	EXPECT_EQ(Markers.count("asset"), 1u);
	ASSERT_TRUE(SettingsAssetQueueTryStartKeyed(Queue, Markers, false, [] { return std::string("asset"); }, Key, Equal, [] { return true; }));
	EXPECT_EQ(Queue, (std::deque<std::string>{"asset"}));
}

TEST(QmSettingsAssetQueue, DeferredRecordMovesToBackWithoutChangingItsIdentity)
{
	const std::string Name(256, 'n');
	std::deque<std::string> Queue{Name, "second", "last"};
	ASSERT_TRUE(SettingsAssetQueueDeferFront(Queue));
	EXPECT_EQ(Queue, (std::deque<std::string>{"second", "last", Name}));
}

TEST(QmSettingsAssetQueue, DeferralAllocationFailureRetainsOriginalOrderAndNames)
{
	CRejectableAllocationResource Memory;
	TQueue Queue(&Memory);
	Queue.emplace_back(256, 'a');
	Queue.emplace_back(256, 'b');
	bool Rejected = false;
	for(int Index = 0; Index < 1024 && !Rejected; ++Index)
	{
		const std::vector<std::string> Before(Queue.begin(), Queue.end());
		Memory.m_Reject = true;
		Rejected = !SettingsAssetQueueDeferFront(Queue);
		Memory.m_Reject = false;
		if(Rejected)
			EXPECT_EQ((std::vector<std::string>(Queue.begin(), Queue.end())), Before);
		else
			Queue.push_back(std::to_string(Index));
	}
	EXPECT_TRUE(Rejected);
}

TEST(QmSettingsAssetQueue, EmptyAndSingleRecordDeferralsNeedNoAllocation)
{
	std::deque<std::string> Empty, Single{std::string(256, 's')};
	bool EmptyOk, SingleOk;
	size_t Attempts;
	{
		CPredictionAllocationFailure Failure;
		EmptyOk = SettingsAssetQueueDeferFront(Empty);
		SingleOk = SettingsAssetQueueDeferFront(Single);
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(EmptyOk);
	EXPECT_TRUE(SingleOk);
	EXPECT_TRUE(Empty.empty());
	EXPECT_EQ(Single, (std::deque<std::string>{std::string(256, 's')}));
	EXPECT_EQ(Attempts, 0u);
}

TEST(QmSettingsAssetQueue, EmptyFrontDoesNotCallConsumer)
{
	std::deque<std::string> Queue;
	std::unordered_set<std::string> Markers;
	bool Called = false;
	EXPECT_FALSE(SettingsAssetQueueTryConsumeFront(Queue, Markers, Key, [&](const auto &) { Called = true; return true; }));
	EXPECT_FALSE(Called);
}

TEST(QmSettingsAssetQueue, DeferredFrontKeepsItsNodeAndMarker)
{
	std::deque<std::string> Queue{"first", "second"};
	std::unordered_set<std::string> Markers{"first", "second"};
	EXPECT_FALSE(SettingsAssetQueueTryConsumeFront(Queue, Markers, Key, [](const auto &) { return false; }));
	EXPECT_EQ(Queue, (std::deque<std::string>{"first", "second"}));
	EXPECT_EQ(Markers, (std::unordered_set<std::string>{"first", "second"}));
}

TEST(QmSettingsAssetQueue, AcceptedFrontRemovesOnlyItsOwnNodeAndMarker)
{
	std::deque<std::string> Queue{"first", "second"};
	std::unordered_set<std::string> Markers{"first", "second"};
	std::string Accepted;
	ASSERT_TRUE(SettingsAssetQueueTryConsumeFront(Queue, Markers, Key, [&](const auto &Name) { Accepted = Name; return true; }));
	EXPECT_EQ(Accepted, "first");
	EXPECT_EQ(Queue, (std::deque<std::string>{"second"}));
	EXPECT_EQ(Markers, (std::unordered_set<std::string>{"second"}));
}

TEST(QmSettingsAssetQueue, FrontKeyAllocationFailureKeepsRecordAndDoesNotCallConsumer)
{
	const std::string Name(256, 'n');
	std::deque<std::string> Queue{Name};
	std::unordered_set<std::string> Markers{Name};
	bool Consumed, Failed, Called = false;
	{
		CPredictionAllocationFailure Failure;
		Consumed = SettingsAssetQueueTryConsumeFront(Queue, Markers, Key, [&](const auto &) { Called = true; return true; });
		Failed = Failure.Triggered();
	}
	EXPECT_TRUE(Failed);
	EXPECT_FALSE(Consumed);
	EXPECT_FALSE(Called);
	EXPECT_EQ(Queue, (std::deque<std::string>{Name}));
	EXPECT_EQ(Markers.count(Name), 1u);
}

TEST(QmSettingsAssetQueue, FrontPreparationAllocationFailureCanRetrySameRecord)
{
	std::deque<std::string> Queue{"first", "second"};
	std::unordered_set<std::string> Markers{"first", "second"};
	EXPECT_FALSE(SettingsAssetQueueTryConsumeFront(Queue, Markers, Key, [](const auto &) -> bool { throw std::bad_alloc(); }));
	EXPECT_EQ(Queue, (std::deque<std::string>{"first", "second"}));
	ASSERT_TRUE(SettingsAssetQueueTryConsumeFront(Queue, Markers, Key, [](const auto &Name) { return Name == "first"; }));
	EXPECT_EQ(Queue, (std::deque<std::string>{"second"}));
	EXPECT_EQ(Markers, (std::unordered_set<std::string>{"second"}));
}

TEST(QmSettingsAssetQueue, FrontPreparationLengthFailureKeepsRecord)
{
	std::deque<std::string> Queue{"first"};
	std::unordered_set<std::string> Markers{"first"};
	EXPECT_FALSE(SettingsAssetQueueTryConsumeFront(Queue, Markers, Key, [](const auto &) -> bool { throw std::length_error("full"); }));
	EXPECT_EQ(Queue, (std::deque<std::string>{"first"}));
	EXPECT_EQ(Markers.count("first"), 1u);
}

TEST(QmSettingsAssetQueue, UnknownFrontPreparationExceptionPropagatesWithoutRemovingRecord)
{
	std::deque<std::string> Queue{"first"};
	std::unordered_set<std::string> Markers{"first"};
	EXPECT_THROW(SettingsAssetQueueTryConsumeFront(Queue, Markers, Key, [](const auto &) -> bool { throw 7; }), int);
	EXPECT_EQ(Queue, (std::deque<std::string>{"first"}));
	EXPECT_EQ(Markers.count("first"), 1u);
}

TEST(QmSettingsAssetQueue, RepeatedDeferralDoesNotDuplicateOrSkipFollowingRecord)
{
	std::deque<std::string> Queue{"first", "second"};
	std::unordered_set<std::string> Markers{"first", "second"};
	for(int Frame = 0; Frame < 3; ++Frame)
		EXPECT_FALSE(SettingsAssetQueueTryConsumeFront(Queue, Markers, Key, [](const auto &) { return false; }));
	ASSERT_TRUE(SettingsAssetQueueTryConsumeFront(Queue, Markers, Key, [](const auto &Name) { return Name == "first"; }));
	EXPECT_EQ(Queue, (std::deque<std::string>{"second"}));
	EXPECT_EQ(Markers.size(), 1u);
}

TEST(QmSettingsAssetQueue, SuccessfulFrontCommitDoesNotAllocateAnotherNodeOrMarker)
{
	std::deque<std::string> Queue{"first", "second"};
	std::unordered_set<std::string> Markers{"first", "second"};
	bool Consumed;
	size_t Attempts;
	{
		CPredictionAllocationFailure Failure;
		Consumed = SettingsAssetQueueTryConsumeFront(Queue, Markers, Key, [](const auto &) { return true; });
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Consumed);
	EXPECT_EQ(Attempts, 0u);
	EXPECT_EQ(Queue, (std::deque<std::string>{"second"}));
	EXPECT_EQ(Markers, (std::unordered_set<std::string>{"second"}));
}

TEST(QmSettingsAssetQueue, FrontWithoutMarkerCanBeDiscardedSafely)
{
	std::deque<std::string> Queue{"stale"};
	std::unordered_set<std::string> Markers;
	EXPECT_TRUE(SettingsAssetQueueTryConsumeFront(Queue, Markers, Key, [](const auto &) { return true; }));
	EXPECT_TRUE(Queue.empty());
	EXPECT_TRUE(Markers.empty());
}

TEST(QmSettingsAssetQueue, DiscardAssetRemovesBothInputPhasesAndKeepsOtherTabsAndNames)
{
	std::deque<SSettingsAssetPreviewHandle> Decode{{2, 1, 3, "removed"}, {2, 1, 4, "kept"}, {3, 1, 3, "removed"}};
	std::deque<SSettingsAssetPreviewHandle> Ready{{2, 1, 3, "removed"}, {2, 1, 4, "kept"}, {3, 1, 3, "removed"}};
	std::unordered_set<std::string> Markers{"2:1:3:removed", "2:1:4:kept", "3:1:3:removed"};
	SettingsAssetQueueDiscardAsset(Decode, Ready, Markers, 2, "removed");
	ASSERT_EQ(Decode.size(), 2u);
	ASSERT_EQ(Ready.size(), 2u);
	EXPECT_EQ(Decode.front().m_Name, "kept");
	EXPECT_EQ(Ready.front().m_Name, "kept");
	EXPECT_EQ(Decode.back().m_Tab, 3);
	EXPECT_EQ(Ready.back().m_Tab, 3);
	EXPECT_EQ(Markers, (std::unordered_set<std::string>{"2:1:4:kept", "3:1:3:removed"}));
}

TEST(QmSettingsAssetQueue, DiscardAssetRemovesAllOldEpochsAndIndicesOfSameName)
{
	std::deque<SSettingsAssetPreviewHandle> Decode{{2, 1, 3, "removed"}, {2, 9, 17, "removed"}};
	std::deque<SSettingsAssetPreviewHandle> Ready = Decode;
	std::unordered_set<std::string> Markers{"2:1:3:removed", "2:9:17:removed"};
	SettingsAssetQueueDiscardAsset(Decode, Ready, Markers, 2, "removed");
	EXPECT_TRUE(Decode.empty());
	EXPECT_TRUE(Ready.empty());
	EXPECT_TRUE(Markers.empty());
}

TEST(QmSettingsAssetQueue, DiscardAssetMatchesCompleteNameIncludingColons)
{
	std::deque<SSettingsAssetPreviewHandle> Decode{{2, 1, 3, "prefix:removed"}, {2, 1, 4, "removed"}};
	std::deque<SSettingsAssetPreviewHandle> Ready = Decode;
	std::unordered_set<std::string> Markers{"2:1:3:prefix:removed", "2:1:4:removed"};
	SettingsAssetQueueDiscardAsset(Decode, Ready, Markers, 2, "prefix:removed");
	ASSERT_EQ(Decode.size(), 1u);
	EXPECT_EQ(Decode.front().m_Name, "removed");
	EXPECT_EQ(Markers, (std::unordered_set<std::string>{"2:1:4:removed"}));
}

TEST(QmSettingsAssetQueue, DiscardAssetClearsOrphanedMarkerWithoutQueueNode)
{
	std::deque<SSettingsAssetPreviewHandle> Decode, Ready;
	std::unordered_set<std::string> Markers{"2:1:3:removed", "2:1:4:kept"};
	SettingsAssetQueueDiscardAsset(Decode, Ready, Markers, 2, "removed");
	EXPECT_EQ(Markers, (std::unordered_set<std::string>{"2:1:4:kept"}));
}

TEST(QmSettingsAssetQueue, DiscardAssetDoesNotAllocateDuringLongNameCleanup)
{
	const std::string Name(256, 'n');
	std::deque<SSettingsAssetPreviewHandle> Decode{{2, 1, 3, Name}, {2, 1, 4, "kept"}};
	std::deque<SSettingsAssetPreviewHandle> Ready = Decode;
	std::unordered_set<std::string> Markers{"2:1:3:" + Name, "2:1:4:kept"};
	size_t Attempts;
	{
		CPredictionAllocationFailure Failure;
		SettingsAssetQueueDiscardAsset(Decode, Ready, Markers, 2, Name);
		Attempts = Failure.Attempts();
	}
	EXPECT_EQ(Attempts, 0u);
	ASSERT_EQ(Decode.size(), 1u);
	ASSERT_EQ(Ready.size(), 1u);
	EXPECT_EQ(Decode.front().m_Name, "kept");
	EXPECT_EQ(Markers, (std::unordered_set<std::string>{"2:1:4:kept"}));
}

TEST(QmSettingsAssetQueue, RepeatedDiscardKeepsOtherResourcesInOriginalOrder)
{
	std::deque<SSettingsAssetPreviewHandle> Decode{{2, 1, 1, "first"}, {2, 1, 2, "removed"}, {2, 1, 3, "last"}};
	std::deque<SSettingsAssetPreviewHandle> Ready = Decode;
	std::unordered_set<std::string> Markers{"2:1:1:first", "2:1:2:removed", "2:1:3:last"};
	SettingsAssetQueueDiscardAsset(Decode, Ready, Markers, 2, "removed");
	SettingsAssetQueueDiscardAsset(Decode, Ready, Markers, 2, "removed");
	ASSERT_EQ(Decode.size(), 2u);
	ASSERT_EQ(Ready.size(), 2u);
	EXPECT_EQ(Decode.front().m_Name, "first");
	EXPECT_EQ(Decode.back().m_Name, "last");
	EXPECT_EQ(Ready.front().m_Name, "first");
	EXPECT_EQ(Ready.back().m_Name, "last");
	EXPECT_EQ(Markers.size(), 2u);
}

TEST(QmSettingsAssetQueue, MarkerIdentityRequiresCompleteTabAndCompleteName)
{
	EXPECT_TRUE(SettingsAssetQueueKeyReferencesAsset("2:1:3:removed", 2, "removed"));
	EXPECT_FALSE(SettingsAssetQueueKeyReferencesAsset("21:1:3:removed", 2, "removed"));
	EXPECT_FALSE(SettingsAssetQueueKeyReferencesAsset("3:1:3:removed", 2, "removed"));
	EXPECT_FALSE(SettingsAssetQueueKeyReferencesAsset("2:1:3:prefix:removed", 2, "removed"));
	EXPECT_FALSE(SettingsAssetQueueKeyReferencesAsset("2:1:3:removed-more", 2, "removed"));
}

TEST(QmSettingsAssetQueue, InvalidMarkerShapeDoesNotMatchDeletion)
{
	EXPECT_FALSE(SettingsAssetQueueKeyReferencesAsset("removed", 2, "removed"));
	EXPECT_FALSE(SettingsAssetQueueKeyReferencesAsset("2:1:removed", 2, "removed"));
	EXPECT_FALSE(SettingsAssetQueueKeyReferencesAsset("invalid:1:3:removed", 2, "removed"));
	EXPECT_FALSE(SettingsAssetQueueKeyReferencesAsset("999999999999999999999:1:3:removed", 2, "removed"));
}
