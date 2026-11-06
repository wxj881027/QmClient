#include <game/client/components/qmclient/friend_last_seen.h>

#include <gtest/gtest.h>
#include <test/support/friend_last_seen_test_support.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>
#include <string>

namespace
{
	constexpr int64_t NOW = 1792800000;
	constexpr const char *OLD_CACHE = R"({"version":1,"players":[{"name":"Alice","date":"2026-10-23","checked_at":"100"}]})";
	constexpr const char *NEW_CACHE = R"({"version":1,"players":[{"name":"Bob","date":"2026-10-24","checked_at":"200"},{"name":"Carol","date":"","checked_at":"200"}]})";

	std::string ManyPlayers()
	{
		std::string Body = R"({"version":1,"players":[)";
		for(int Index = 0; Index < 512; ++Index)
		{
			if(Index != 0)
				Body += ',';
			Body += R"({"name":"P)" + std::to_string(Index) + R"(","date":"2026-10-23","checked_at":"100"})";
		}
		return Body + "]}";
	}
}

TEST(QmFriendLastSeenAllocation, FailedNewEntryAllocationLeavesNoPartialRequestAndCanRetry)
{
	CQmFriendLastSeenCache Cache;
	const SQmFriendLastSeenEntry *pEntry = nullptr;
	bool Triggered = false;
	{
		CPredictionAllocationFailure Failure;
		pEntry = Cache.Get("Alice", NOW);
		Triggered = Failure.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_EQ(pEntry, nullptr);
	EXPECT_EQ(Cache.Find("Alice"), nullptr);
	EXPECT_FALSE(Cache.NextRequest(NOW));
	ASSERT_NE(Cache.Get("Alice", NOW), nullptr);
	ASSERT_TRUE(Cache.NextRequest(NOW));
	EXPECT_EQ(Cache.CurrentRequest()->m_Name, "Alice");
}

TEST(QmFriendLastSeenAllocation, FailedNewQueueAllocationRollsBackOnlyTheNewEntry)
{
	CQmFriendLastSeenCache Cache;
	ASSERT_TRUE(Cache.Load(OLD_CACHE, NOW));
	const auto *pOld = Cache.Find("Alice");
	bool SawQueueFailure = false;
	for(int Index = 0; Index < 512 && !SawQueueFailure; ++Index)
	{
		const std::string Name = "P" + std::to_string(Index);
		const SQmFriendLastSeenEntry *pNew = nullptr;
		bool Triggered = false;
		{
			// 名字均在短字符串容量内；第一次分配给 map 节点，随后才是队列容量。
			CPredictionAllocationFailure Failure(1);
			pNew = Cache.Get(Name.c_str(), NOW);
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
			continue;
		SawQueueFailure = true;
		EXPECT_EQ(pNew, nullptr);
		EXPECT_EQ(Cache.Find(Name.c_str()), nullptr);
		EXPECT_EQ(Cache.Find("Alice"), pOld);
		EXPECT_EQ(pOld->m_Date, "2026-10-23");
		ASSERT_NE(Cache.Get(Name.c_str(), NOW), nullptr);
		EXPECT_TRUE(Cache.Find(Name.c_str())->m_Queued);
	}
	EXPECT_TRUE(SawQueueFailure);
}

TEST(QmFriendLastSeenAllocation, FailedExistingQueueAllocationKeepsHistoryAndVisibilityUntilRetry)
{
	CQmFriendLastSeenCache Cache;
	ASSERT_TRUE(Cache.Load(ManyPlayers(), NOW));
	bool SawQueueFailure = false;
	for(int Index = 0; Index < 512 && !SawQueueFailure; ++Index)
	{
		const std::string Name = "P" + std::to_string(Index);
		const auto *pOld = Cache.Find(Name.c_str());
		ASSERT_NE(pOld, nullptr);
		const SQmFriendLastSeenEntry *pResult = nullptr;
		bool Triggered = false;
		{
			CPredictionAllocationFailure Failure;
			pResult = Cache.Get(Name.c_str(), NOW);
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
			continue;
		SawQueueFailure = true;
		EXPECT_EQ(pResult, pOld);
		EXPECT_EQ(pOld->m_Date, "2026-10-23");
		EXPECT_EQ(pOld->m_VisibleAt, 0);
		EXPECT_FALSE(pOld->m_Queued);
		EXPECT_EQ(Cache.Get(Name.c_str(), NOW), pOld);
		EXPECT_EQ(pOld->m_VisibleAt, NOW);
		EXPECT_TRUE(pOld->m_Queued);
	}
	EXPECT_TRUE(SawQueueFailure);
}

TEST(QmFriendLastSeenAllocation, EveryLoadPreparationFailureKeepsOldHistoryAndActiveRequest)
{
	CQmFriendLastSeenCache Trace;
	size_t Attempts = 0;
	bool Loaded = false;
	{
		CPredictionAllocationFailure Failure(std::numeric_limits<size_t>::max());
		Loaded = Trace.Load(NEW_CACHE, NOW);
		Attempts = Failure.Attempts();
	}
	ASSERT_TRUE(Loaded);
	ASSERT_GT(Attempts, 0u);
	for(size_t Index = 0; Index < Attempts; ++Index)
	{
		SCOPED_TRACE(Index);
		CQmFriendLastSeenCache Cache;
		ASSERT_TRUE(Cache.Load(OLD_CACHE, NOW));
		const auto *pOld = Cache.Get("Alice", NOW);
		ASSERT_TRUE(Cache.NextRequest(NOW));
		const uint64_t Id = Cache.CurrentRequest()->m_Id;
		bool Triggered = false;
		{
			CPredictionAllocationFailure Failure(Index);
			Loaded = Cache.Load(NEW_CACHE, NOW);
			Triggered = Failure.Triggered();
		}
		ASSERT_TRUE(Triggered);
		EXPECT_FALSE(Loaded);
		EXPECT_EQ(Cache.Find("Alice"), pOld);
		EXPECT_EQ(pOld->m_Date, "2026-10-23");
		EXPECT_TRUE(Cache.IsCurrent(Id));
		EXPECT_EQ(Cache.Find("Bob"), nullptr);
		ASSERT_TRUE(Cache.Load(NEW_CACHE, NOW));
		EXPECT_EQ(Cache.Find("Alice"), nullptr);
		EXPECT_FALSE(Cache.IsCurrent(Id));
		EXPECT_EQ(Cache.Find("Bob")->m_Date, "2026-10-24");
	}
}

TEST(QmFriendLastSeenAllocation, EverySerializeFailurePreservesOutputAndCacheForRetry)
{
	CQmFriendLastSeenCache Cache;
	ASSERT_TRUE(Cache.Load(NEW_CACHE, NOW));
	std::string Trace;
	size_t Attempts = 0;
	bool Saved = false;
	{
		CPredictionAllocationFailure Failure(std::numeric_limits<size_t>::max());
		Saved = Cache.TrySerialize(Trace);
		Attempts = Failure.Attempts();
	}
	ASSERT_TRUE(Saved);
	ASSERT_GT(Attempts, 0u);
	const auto *pOld = Cache.Find("Bob");
	for(size_t Index = 0; Index < Attempts; ++Index)
	{
		SCOPED_TRACE(Index);
		std::string Body = "sentinel";
		bool Triggered = false;
		{
			CPredictionAllocationFailure Failure(Index);
			Saved = Cache.TrySerialize(Body);
			Triggered = Failure.Triggered();
		}
		ASSERT_TRUE(Triggered);
		EXPECT_FALSE(Saved);
		EXPECT_EQ(Body, "sentinel");
		EXPECT_EQ(Cache.Find("Bob"), pOld);
		EXPECT_FALSE(Cache.Dirty());
		ASSERT_TRUE(Cache.TrySerialize(Body));
		CQmFriendLastSeenCache Reloaded;
		ASSERT_TRUE(Reloaded.Load(Body, NOW));
		EXPECT_EQ(Reloaded.Find("Bob")->m_Date, "2026-10-24");
	}
}

TEST(QmFriendLastSeenServiceAllocation, FailedRevisionPreparationDoesNotStampOrPruneOldCache)
{
	CFriendLastSeenTestFriends Friends;
	Friends.Set({"Removed", "Keep"});
	CFriendLastSeenTestEngine Engine;
	CFriendLastSeenTestHttp Http;
	CQmFriendLastSeenService Service(CreateFriendLastSeenTestRequest);
	Service.Update(Friends, Engine, Http, false, NOW);
	const auto *pOld = Service.Get("Removed", NOW);
	ASSERT_NE(pOld, nullptr);
	Friends.Set({"Keep"});
	bool Triggered = false;
	{
		CPredictionAllocationFailure Failure;
		Service.Update(Friends, Engine, Http, false, NOW);
		Triggered = Failure.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_EQ(Service.Find("Removed"), pOld);
	EXPECT_FALSE(pOld->m_Queued);
	EXPECT_EQ(Http.m_Submissions, 0);
	Service.Update(Friends, Engine, Http, false, NOW);
	EXPECT_EQ(Service.Find("Removed"), nullptr);
}

TEST(QmFriendLastSeenServiceAllocation, EveryHttpPreparationFailureRetriesTheClaimedRequest)
{
	CFriendLastSeenTestFriends Friends;
	Friends.Set({"Alice"});
	CFriendLastSeenTestEngine Engine;
	size_t Attempts = 0;
	{
		CFriendLastSeenTestHttp Http;
		CQmFriendLastSeenService Service(CreateFriendLastSeenTestRequest);
		Service.Update(Friends, Engine, Http, true, NOW);
		ASSERT_NE(Service.Get("Alice", NOW), nullptr);
		{
			CPredictionAllocationFailure Failure(std::numeric_limits<size_t>::max());
			Service.Update(Friends, Engine, Http, true, NOW);
			Attempts = Failure.Attempts();
		}
		ASSERT_EQ(Http.m_Submissions, 1);
	}
	ASSERT_GE(Attempts, 3u);
	for(size_t Index = 0; Index < Attempts; ++Index)
	{
		SCOPED_TRACE(Index);
		CFriendLastSeenTestHttp Http;
		CQmFriendLastSeenService Service(CreateFriendLastSeenTestRequest);
		Service.Update(Friends, Engine, Http, true, NOW);
		ASSERT_NE(Service.Get("Alice", NOW), nullptr);
		bool Triggered = false;
		{
			CPredictionAllocationFailure Failure(Index);
			Service.Update(Friends, Engine, Http, true, NOW);
			Triggered = Failure.Triggered();
		}
		ASSERT_TRUE(Triggered);
		EXPECT_EQ(Http.m_Submissions, 0);
		EXPECT_EQ(Service.Find("Alice")->m_Status, EQmFriendLastSeenStatus::LOADING);
		Service.Update(Friends, Engine, Http, true, NOW + 1);
		ASSERT_EQ(Http.m_Submissions, 1);
		ASSERT_TRUE(Http.m_pLast->Finish("", 404));
		Service.Update(Friends, Engine, Http, true, NOW + 1);
		EXPECT_EQ(Service.Find("Alice")->m_Status, EQmFriendLastSeenStatus::NO_DATA);
		Service.Update(Friends, Engine, Http, true, NOW + 2);
		EXPECT_EQ(Http.m_Submissions, 1);
	}
}

TEST(QmFriendLastSeenServiceAllocation, FailedParseJobCreationKeepsCompletedResponseForAnotherFrame)
{
	CFriendLastSeenTestFriends Friends;
	Friends.Set({"Alice"});
	CFriendLastSeenTestEngine Engine;
	CFriendLastSeenTestHttp Http;
	CQmFriendLastSeenService Service(CreateFriendLastSeenTestRequest);
	ASSERT_NE(Service.Get("Alice", NOW), nullptr);
	Service.Update(Friends, Engine, Http, true, NOW);
	ASSERT_NE(Http.m_pLast, nullptr);
	const auto pResponse = Http.m_pLast;
	ASSERT_TRUE(pResponse->Finish(R"({"recent_player_info":[{"name":"Alice","last_seen":"2026-10-23"}]})"));
	bool Triggered = false;
	{
		CPredictionAllocationFailure Failure;
		Service.Update(Friends, Engine, Http, true, NOW);
		Triggered = Failure.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_EQ(Engine.m_Offers, 0);
	EXPECT_EQ(pResponse->m_AbortCalls, 0);
	Engine.m_RejectNext = true;
	Service.Update(Friends, Engine, Http, true, NOW + 1);
	EXPECT_EQ(Engine.m_Offers, 1);
	EXPECT_EQ(Engine.m_Accepted, 0);
	Engine.m_RejectNext = true;
	Service.Update(Friends, Engine, Http, true, NOW + 2);
	EXPECT_EQ(Engine.m_Offers, 2);
	EXPECT_EQ(Http.m_Submissions, 1);
	EXPECT_EQ(Http.m_pLast, pResponse);
}

TEST(QmFriendLastSeenServiceAllocation, DisableCancelsBeforeAFailedRevisionAllocation)
{
	CFriendLastSeenTestFriends Friends;
	Friends.Set({"Alice"});
	CFriendLastSeenTestEngine Engine;
	CFriendLastSeenTestHttp Http;
	CQmFriendLastSeenService Service(CreateFriendLastSeenTestRequest);
	ASSERT_NE(Service.Get("Alice", NOW), nullptr);
	Service.Update(Friends, Engine, Http, true, NOW);
	ASSERT_EQ(Http.m_Submissions, 1);
	const auto pOld = Http.m_pLast;
	Friends.Set({"Alice", "Bob"});
	bool Triggered = false;
	{
		CPredictionAllocationFailure Failure;
		Service.Update(Friends, Engine, Http, false, NOW + 1);
		Triggered = Failure.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_EQ(pOld->m_AbortCalls, 1);
	EXPECT_EQ(Http.m_Submissions, 1);
	Service.Update(Friends, Engine, Http, false, NOW + 2);
	EXPECT_EQ(Http.m_Submissions, 1);
	ASSERT_NE(Service.Get("Alice", NOW + 2), nullptr);
	Service.Update(Friends, Engine, Http, true, NOW + 2);
	EXPECT_EQ(Http.m_Submissions, 2);
	EXPECT_NE(Http.m_pLast, pOld);
}
