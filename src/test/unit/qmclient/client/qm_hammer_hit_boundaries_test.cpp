#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/client/components/qmclient/hammer_hit_detection.h>

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <limits>

namespace
{
	SQmHammerHitRecord Hit(int Tick = 100, int Ordinal = 0, int Connection = 0)
	{
		return {1, 2, Tick, Ordinal, vec2(12, 34), Connection, false};
	}

	SQmHammerHitMatch Match(int EventTick, int AttackTick)
	{
		SQmHammerAttackSample Attack;
		Attack.m_ClientId = 1;
		Attack.m_AttackTick = AttackTick;
		Attack.m_Weapon = WEAPON_HAMMER;
		Attack.m_Direction = vec2(1, 0);
		SQmHammerTargetSample Target;
		Target.m_ClientId = 2;
		Target.m_PrevPos = Target.m_CurPos = vec2(21, 0);
		return QmMatchHammerHitEvent(vec2(21, 0), EventTick, &Attack, 1, &Target, 1);
	}
}

TEST(QmHammerHitBoundaries, EmptyQueriesLeaveCallerStorageUnchanged)
{
	CQmHammerHitTracker Tracker;
	SQmHammerHitRecord Output = Hit(77, 9);
	EXPECT_FALSE(Tracker.FindLatest(1, 2, 0, &Output));
	EXPECT_EQ(Tracker.FindLatestTargets(1, 0, &Output, 1), 0);
	EXPECT_EQ(Tracker.FindTargetHitsAtTick(2, 100, &Output, 1), 0);
	EXPECT_EQ(Output.m_SnapshotTick, 77);
	EXPECT_EQ(Output.m_EventOrdinal, 9);
}

TEST(QmHammerHitBoundaries, RejectedRecordsDoNotConsumeIdentityOrReplaceHistory)
{
	std::array<SQmHammerHitRecord, 8> aInvalid;
	aInvalid.fill(Hit(101, 1));
	aInvalid[0].m_SnapshotTick = -1;
	aInvalid[1].m_EventOrdinal = -1;
	aInvalid[2].m_AttackerId = -2;
	aInvalid[3].m_AttackerId = MAX_CLIENTS;
	aInvalid[4].m_TargetId = -2;
	aInvalid[5].m_TargetId = MAX_CLIENTS;
	aInvalid[6].m_Connection = -2;
	aInvalid[7].m_TargetId = 1;
	for(std::size_t Index = 0; Index < aInvalid.size(); ++Index)
	{
		SCOPED_TRACE(Index);
		CQmHammerHitTracker Tracker;
		ASSERT_TRUE(Tracker.Record(Hit()));
		EXPECT_FALSE(Tracker.Record(aInvalid[Index]));
		SQmHammerHitRecord Output;
		ASSERT_TRUE(Tracker.FindLatest(1, 2, 0, &Output));
		EXPECT_EQ(Output.m_SnapshotTick, 100);
		EXPECT_EQ(Output.m_EventOrdinal, 0);
		EXPECT_TRUE(Tracker.Record(Hit(101, 1)));
	}
}

TEST(QmHammerHitBoundaries, UnknownIdentityRemainsDistinctFromWildcardFilters)
{
	CQmHammerHitTracker Tracker;
	auto Unknown = Hit();
	Unknown.m_AttackerId = Unknown.m_TargetId = -1;
	Unknown.m_Connection = -1;
	ASSERT_TRUE(Tracker.Record(Unknown));
	SQmHammerHitRecord Output;
	ASSERT_TRUE(Tracker.FindLatest(-1, -1, 0, &Output));
	EXPECT_EQ(Output.m_AttackerId, -1);
	EXPECT_EQ(Output.m_TargetId, -1);
	ASSERT_TRUE(Tracker.Record(Hit(101, 1)));
	ASSERT_TRUE(Tracker.FindLatest(CQmHammerHitTracker::ANY_CLIENT, CQmHammerHitTracker::ANY_CLIENT, 0, &Output));
	EXPECT_EQ(Output.m_AttackerId, 1);
	EXPECT_EQ(Tracker.FindLatestTargets(1, 0, &Output, 1), 1);
	EXPECT_EQ(Output.m_TargetId, 2);
}

TEST(QmHammerHitBoundaries, DuplicateIdentityKeepsFirstEvidence)
{
	CQmHammerHitTracker Tracker;
	ASSERT_TRUE(Tracker.Record(Hit()));
	auto Changed = Hit();
	Changed.m_AttackerId = 3;
	Changed.m_TargetId = 4;
	Changed.m_Pos = vec2(50, 60);
	Changed.m_TargetWoke = true;
	EXPECT_FALSE(Tracker.Record(Changed));
	SQmHammerHitRecord Output;
	ASSERT_TRUE(Tracker.FindLatest(1, 2, 100, &Output));
	EXPECT_EQ(Output.m_Pos, vec2(12, 34));
	EXPECT_FALSE(Output.m_TargetWoke);
	EXPECT_FALSE(Tracker.FindLatest(3, 4, 100));
}

TEST(QmHammerHitBoundaries, FullRingEvictsOnlyOldestRetainedRecord)
{
	CQmHammerHitTracker Tracker;
	for(int Index = 0; Index <= CQmHammerHitTracker::MAX_RECORDS; ++Index)
		ASSERT_TRUE(Tracker.Record(Hit(100 + Index, Index)));
	SQmHammerHitRecord Output;
	EXPECT_EQ(Tracker.FindTargetHitsAtTick(2, 100, &Output, 1), 0);
	ASSERT_EQ(Tracker.FindTargetHitsAtTick(2, 101, &Output, 1), 1);
	EXPECT_EQ(Output.m_EventOrdinal, 1);
	ASSERT_TRUE(Tracker.FindLatest(1, 2, 0, &Output));
	EXPECT_EQ(Output.m_EventOrdinal, CQmHammerHitTracker::MAX_RECORDS);
}

TEST(QmHammerHitBoundaries, RejectionAtCapacityDoesNotEvictOldestRecord)
{
	CQmHammerHitTracker Tracker;
	for(int Index = 0; Index < CQmHammerHitTracker::MAX_RECORDS; ++Index)
		ASSERT_TRUE(Tracker.Record(Hit(100 + Index, Index)));
	EXPECT_FALSE(Tracker.Record(Hit()));
	EXPECT_FALSE(Tracker.Record(Hit(-1, 999)));
	SQmHammerHitRecord Output;
	ASSERT_EQ(Tracker.FindTargetHitsAtTick(2, 100, &Output, 1), 1);
	EXPECT_EQ(Output.m_EventOrdinal, 0);
}

TEST(QmHammerHitBoundaries, ResetDropsHistoryAndPermitsSameIdentityAgain)
{
	CQmHammerHitTracker Tracker;
	ASSERT_TRUE(Tracker.Record(Hit()));
	Tracker.Reset();
	Tracker.Reset();
	SQmHammerHitRecord Output = Hit(77, 9);
	EXPECT_FALSE(Tracker.FindLatest(1, 2, 0, &Output));
	EXPECT_EQ(Output.m_SnapshotTick, 77);
	EXPECT_TRUE(Tracker.Record(Hit()));
	EXPECT_TRUE(Tracker.FindLatest(1, 2, 100));
}

TEST(QmHammerHitBoundaries, LatestUsesSnapshotTickBeforeArrivalAndThenBreaksTiesByArrival)
{
	CQmHammerHitTracker Tracker;
	ASSERT_TRUE(Tracker.Record(Hit(300, 1)));
	ASSERT_TRUE(Tracker.Record(Hit(100, 2)));
	SQmHammerHitRecord Output;
	ASSERT_TRUE(Tracker.FindLatest(1, 2, 0, &Output));
	EXPECT_EQ(Output.m_EventOrdinal, 1);
	ASSERT_TRUE(Tracker.Record(Hit(300, 3)));
	ASSERT_TRUE(Tracker.FindLatest(1, 2, 0, &Output));
	EXPECT_EQ(Output.m_EventOrdinal, 3);
}

TEST(QmHammerHitBoundaries, LatestTargetsKeepIdOrderAndDoNotWritePastCapacity)
{
	CQmHammerHitTracker Tracker;
	auto High = Hit();
	High.m_TargetId = MAX_CLIENTS - 1;
	ASSERT_TRUE(Tracker.Record(High));
	ASSERT_TRUE(Tracker.Record(Hit(101, 1)));
	SQmHammerHitRecord aOutput[] = {Hit(77, 9), Hit(88, 10)};
	ASSERT_EQ(Tracker.FindLatestTargets(1, 0, aOutput, 1), 1);
	EXPECT_EQ(aOutput[0].m_TargetId, 2);
	EXPECT_EQ(aOutput[1].m_SnapshotTick, 88);
	EXPECT_EQ(aOutput[1].m_EventOrdinal, 10);
}

TEST(QmHammerHitBoundaries, LatestTargetsApplyConnectionAndMinimumTickTogether)
{
	CQmHammerHitTracker Tracker;
	ASSERT_TRUE(Tracker.Record(Hit(100, 1, 0)));
	ASSERT_TRUE(Tracker.Record(Hit(101, 2, 1)));
	SQmHammerHitRecord Output = Hit(77, 9);
	EXPECT_EQ(Tracker.FindLatestTargets(1, 101, &Output, 1, 0), 0);
	EXPECT_EQ(Output.m_SnapshotTick, 77);
	ASSERT_EQ(Tracker.FindLatestTargets(1, 101, &Output, 1, 1), 1);
	EXPECT_EQ(Output.m_Connection, 1);
	EXPECT_EQ(Output.m_EventOrdinal, 2);
}

TEST(QmHammerHitBoundaries, ExactTickQueryStopsAtCapacityAndPreservesTail)
{
	CQmHammerHitTracker Tracker;
	ASSERT_TRUE(Tracker.Record(Hit(100, 1)));
	ASSERT_TRUE(Tracker.Record(Hit(100, 2)));
	SQmHammerHitRecord aOutput[] = {Hit(77, 9), Hit(88, 10)};
	ASSERT_EQ(Tracker.FindTargetHitsAtTick(2, 100, aOutput, 1, 0), 1);
	EXPECT_EQ(aOutput[0].m_SnapshotTick, 100);
	EXPECT_EQ(aOutput[0].m_TargetId, 2);
	EXPECT_EQ(aOutput[1].m_SnapshotTick, 88);
	EXPECT_EQ(aOutput[1].m_EventOrdinal, 10);
	EXPECT_EQ(Tracker.FindTargetHitsAtTick(2, 100, aOutput, 1, 1), 0);
}

TEST(QmHammerHitBoundaries, InvalidQueryBoundsReturnNoRecordsWithoutWriting)
{
	CQmHammerHitTracker Tracker;
	ASSERT_TRUE(Tracker.Record(Hit()));
	SQmHammerHitRecord Output = Hit(77, 9);
	for(const int Id : {-1, static_cast<int>(MAX_CLIENTS)})
	{
		SCOPED_TRACE(Id);
		EXPECT_EQ(Tracker.FindLatestTargets(Id, 0, &Output, 1), 0);
		EXPECT_EQ(Tracker.FindTargetHitsAtTick(Id, 100, &Output, 1), 0);
	}
	for(const int Capacity : {0, -1})
	{
		SCOPED_TRACE(Capacity);
		EXPECT_EQ(Tracker.FindLatestTargets(1, 0, &Output, Capacity), 0);
		EXPECT_EQ(Tracker.FindTargetHitsAtTick(2, 100, &Output, Capacity), 0);
	}
	EXPECT_EQ(Tracker.FindLatestTargets(1, 0, nullptr, 1), 0);
	EXPECT_EQ(Tracker.FindTargetHitsAtTick(2, 100, nullptr, 1), 0);
	EXPECT_EQ(Tracker.FindTargetHitsAtTick(2, -1, &Output, 1), 0);
	EXPECT_EQ(Output.m_SnapshotTick, 77);
	EXPECT_EQ(Output.m_EventOrdinal, 9);
}

TEST(QmHammerHitBoundaries, NonnegativeExtremeAttackAgesRejectStaleAndFutureCandidates)
{
	const int Max = std::numeric_limits<int>::max();
	EXPECT_EQ(Match(Max, 0).m_AttackerId, -1);
	EXPECT_EQ(Match(0, Max).m_AttackerId, -1);
}

TEST(QmHammerHitBoundaries, InclusiveThreeTickAgeWorksAtBothNonnegativeDomainEnds)
{
	for(const int EventTick : {3, std::numeric_limits<int>::max()})
	{
		SCOPED_TRACE(EventTick);
		EXPECT_EQ(Match(EventTick, EventTick - 3).m_AttackerId, 1);
		EXPECT_EQ(Match(EventTick, EventTick).m_TargetId, 2);
	}
}

TEST(QmHammerHitBoundaries, DuplicateDeliveryRejectsOppositeNonnegativeTickExtremes)
{
	auto First = Hit(0, 0, 0);
	auto Second = Hit(std::numeric_limits<int>::max(), 0, 1);
	EXPECT_FALSE(QmIsDuplicateHammerHitEffect(First, Second, 1));
	EXPECT_FALSE(QmIsDuplicateHammerHitEffect(Second, First, 1));
}

TEST(QmHammerHitBoundaries, DuplicateDeliveryKeepsThreeTickBoundaryNearMaximum)
{
	const int Max = std::numeric_limits<int>::max();
	EXPECT_TRUE(QmIsDuplicateHammerHitEffect(Hit(Max - 3, 0, 0), Hit(Max, 0, 1), 1));
	EXPECT_FALSE(QmIsDuplicateHammerHitEffect(Hit(Max - 4, 0, 0), Hit(Max, 0, 1), 1));
}
