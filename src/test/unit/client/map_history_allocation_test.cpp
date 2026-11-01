#include <game/client/components/tclient/map_history.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>
#include <string>

using namespace QmMapHistory;

namespace
{
	constexpr const char *JSON = R"({"entries":[
		{"map_name":"First map name longer than small string capacity","map_id":"first-map-identity-longer-than-small-string-capacity","last_entered":100,"last_played_date":"a long saved date string that needs its own allocation","deaths":10},
		{"map_name":"Newer map name longer than small string capacity","map_id":"first-map-identity-longer-than-small-string-capacity","last_entered":200,"last_played_date":"a newer long saved date string that needs its own allocation","deaths":2},
		{"map_name":"Second map name longer than small string capacity","map_id":"second-map-identity-longer-than-small-string-capacity","last_entered":300}
	]})";
}

TEST(MapHistoryAllocation, EveryCppPreparationAllocationFailurePreservesTheOldCollectionWithoutRecoveryAllocation)
{
	size_t Allocations = 0;
	bool Loaded = false;
	{
		CMapHistory Probe;
		CPredictionAllocationFailure Count(std::numeric_limits<size_t>::max());
		Loaded = Probe.FromJson(JSON);
		Allocations = Count.Attempts();
	}
	ASSERT_TRUE(Loaded);
	ASSERT_GT(Allocations, 0u);
	for(size_t BeforeFailure = 0; BeforeFailure < Allocations; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		CMapHistory History;
		History.RecordVisit("Original map name longer than small string capacity", "original", 17, "2026-11-01");
		History.MarkFinished("original", 100, 200);
		History.AddDeath("original", 3);
		const auto *pOriginal = History.Find("original");
		const std::string Before = History.ToJson();
		char aError[128]{};
		bool Parsed = true, Threw = false, Triggered = false;
		size_t Attempts = 0;
		{
			CPredictionAllocationFailure Failure(BeforeFailure);
			try
			{
				Parsed = History.FromJson(JSON, aError, sizeof(aError));
			}
			catch(...)
			{
				Threw = true;
			}
			Triggered = Failure.Triggered();
			Attempts = Failure.Attempts();
		}
		EXPECT_TRUE(Triggered);
		EXPECT_FALSE(Threw);
		EXPECT_FALSE(Parsed);
		EXPECT_EQ(Attempts, BeforeFailure + 1);
		EXPECT_NE(aError[0], '\0');
		EXPECT_EQ(History.Find("original"), pOriginal);
		EXPECT_EQ(History.ToJson(), Before);
	}
}

TEST(MapHistoryAllocation, SuccessfulRetryCanReplaceTheCollectionAfterAllocationFailure)
{
	CMapHistory History;
	History.RecordVisit("Keep", "keep", 1, "2026-11-01");
	bool Parsed = true, Triggered = false;
	{
		CPredictionAllocationFailure Failure;
		Parsed = History.FromJson(JSON);
		Triggered = Failure.Triggered();
	}
	ASSERT_TRUE(Triggered);
	ASSERT_FALSE(Parsed);
	ASSERT_NE(History.Find("keep"), nullptr);
	ASSERT_TRUE(History.FromJson(JSON));
	EXPECT_EQ(History.Size(), 2u);
	EXPECT_EQ(History.Find("keep"), nullptr);
	const auto *pFirst = History.Find("first-map-identity-longer-than-small-string-capacity");
	ASSERT_NE(pFirst, nullptr);
	EXPECT_EQ(pFirst->m_MapName, "Newer map name longer than small string capacity");
	EXPECT_EQ(pFirst->m_DeathCount, 10);
}
