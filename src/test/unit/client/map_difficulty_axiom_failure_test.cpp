#include <game/client/components/qmclient/map_difficulty_catalog.h>

#include <gtest/gtest.h>
#include <test/support/map_difficulty_axiom.h>
#include <test/support/prediction_allocation_failure.h>

namespace
{
	using namespace map_difficulty_test;
	constexpr char NEW_MAP[] = "New_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
}

TEST(MapDifficultyAxiomFailure, EveryInsertionAllocationFailurePreservesOldEntriesAndCanRetry)
{
	const CServerInfo OldServer = Server(), NewServer = Server(NEW_MAP);
	const std::string Message = Motd(NEW_MAP, "★★★", std::string(60, 'C').c_str());
	int Failures = 0;
	bool Completed = false;
	for(size_t Budget = 0; Budget < 64; ++Budget)
	{
		SCOPED_TRACE(Budget);
		CQmMapDifficultyCatalog Catalog;
		ASSERT_TRUE(Catalog.LoadAxiomCacheJson(OLD_CACHE, sizeof(OLD_CACHE) - 1));
		const auto *pOld = Catalog.FindForServer(OldServer);
		ASSERT_NE(pOld, nullptr);
		bool Changed, Triggered;
		{
			CPredictionAllocationFailure Fault(Budget);
			Changed = Catalog.RecordAxiomMotd(NewServer, Message.c_str());
			Triggered = Fault.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_TRUE(Changed);
			Completed = true;
			break;
		}
		++Failures;
		EXPECT_FALSE(Changed);
		EXPECT_EQ(Catalog.FindForServer(OldServer), pOld);
		EXPECT_EQ(Catalog.FindForServer(NewServer), nullptr);
		EXPECT_FALSE(Catalog.AxiomCacheDirty());
		EXPECT_EQ(pOld->m_Stars, 1);
		ASSERT_TRUE(Catalog.RecordAxiomMotd(NewServer, Message.c_str()));
		ASSERT_NE(Catalog.FindForServer(NewServer), nullptr);
		EXPECT_EQ(Catalog.FindForServer(NewServer)->m_Stars, 3);
		EXPECT_TRUE(Catalog.AxiomCacheDirty());
	}
	EXPECT_GT(Failures, 0);
	EXPECT_TRUE(Completed);
}

TEST(MapDifficultyAxiomFailure, FailedRatingPreparationPreservesExistingEntryAndCleanState)
{
	const CServerInfo Current = Server();
	const std::string Message = Motd("Old", "★★★", std::string(60, 'C').c_str());
	CQmMapDifficultyCatalog Catalog;
	ASSERT_TRUE(Catalog.LoadAxiomCacheJson(OLD_CACHE, sizeof(OLD_CACHE) - 1));
	const auto *pOld = Catalog.FindForServer(Current);
	ASSERT_NE(pOld, nullptr);
	bool Changed, Triggered;
	{
		CPredictionAllocationFailure Fault(0);
		Changed = Catalog.RecordAxiomMotd(Current, Message.c_str());
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(Changed);
	EXPECT_EQ(Catalog.FindForServer(Current), pOld);
	EXPECT_EQ(pOld->m_Stars, 1);
	EXPECT_EQ(pOld->m_Category, "Old");
	EXPECT_FALSE(Catalog.AxiomCacheDirty());
	ASSERT_TRUE(Catalog.RecordAxiomMotd(Current, Message.c_str()));
	EXPECT_EQ(Catalog.FindForServer(Current)->m_Stars, 3);
	EXPECT_TRUE(Catalog.AxiomCacheDirty());
}

TEST(MapDifficultyAxiomFailure, EveryCacheCandidateAllocationFailurePreservesPublishedEntriesAndDirtyState)
{
	constexpr char JSON[] = R"({"version":1,"entries":[{"mode":"Gores","map":"New_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789","category":"Long category that needs a separate allocation","stars":3}]})";
	const CServerInfo OldServer = Server(), NewServer = Server(NEW_MAP);
	const std::string OldMessage = Motd("Old", "★☆☆");
	int Failures = 0;
	bool Completed = false;
	for(size_t Budget = 0; Budget < 64; ++Budget)
	{
		SCOPED_TRACE(Budget);
		CQmMapDifficultyCatalog Catalog;
		ASSERT_TRUE(Catalog.RecordAxiomMotd(OldServer, OldMessage.c_str()));
		const auto *pOld = Catalog.FindForServer(OldServer);
		bool Loaded, Triggered;
		{
			CPredictionAllocationFailure Fault(Budget);
			Loaded = Catalog.LoadAxiomCacheJson(JSON, sizeof(JSON) - 1);
			Triggered = Fault.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_TRUE(Loaded);
			Completed = true;
			break;
		}
		++Failures;
		EXPECT_FALSE(Loaded);
		EXPECT_EQ(Catalog.FindForServer(OldServer), pOld);
		EXPECT_EQ(Catalog.FindForServer(NewServer), nullptr);
		EXPECT_TRUE(Catalog.AxiomCacheDirty());
		ASSERT_TRUE(Catalog.LoadAxiomCacheJson(JSON, sizeof(JSON) - 1));
		EXPECT_EQ(Catalog.FindForServer(OldServer), nullptr);
		ASSERT_NE(Catalog.FindForServer(NewServer), nullptr);
		EXPECT_EQ(Catalog.FindForServer(NewServer)->m_Stars, 3);
		EXPECT_FALSE(Catalog.AxiomCacheDirty());
	}
	EXPECT_GT(Failures, 0);
	EXPECT_TRUE(Completed);
}
