#include <game/client/components/qmclient/map_history_session.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>
#include <string>
#include <utility>

using namespace QmMapHistory;

namespace
{
	constexpr const char *ID = "path:maps/a-valid-map-with-a-long-identity.map";
	constexpr const char *NAME = "A valid map name longer than small string capacity";

	CMapHistory PreviousHistory(bool Existing)
	{
		CMapHistory History;
		if(Existing)
		{
			History.RecordVisit("Prior map", ID, 17, "2026-10-31");
			History.AddDeath(ID, 3);
			History.MarkFinished(ID, 1000, 2000);
		}
		return History;
	}

	void ExpectFailedPreparationAndRetry(bool Existing)
	{
		size_t Allocations = 0;
		bool Started = false;
		{
			CMapHistory Probe = PreviousHistory(Existing);
			CSession Session;
			std::string Id = ID;
			CPredictionAllocationFailure Count(std::numeric_limits<size_t>::max());
			Started = Session.TryStart(Probe, NAME, std::move(Id), 100, "2026-11-01", []() noexcept { return INT64_C(1234); });
			Allocations = Count.Attempts();
		}
		ASSERT_TRUE(Started);
		ASSERT_GT(Allocations, 0u);
		for(size_t BeforeFailure = 0; BeforeFailure < Allocations; ++BeforeFailure)
		{
			SCOPED_TRACE(BeforeFailure);
			CMapHistory History = PreviousHistory(Existing);
			const std::string Before = History.ToJson();
			const auto *pOriginal = History.Find(ID);
			CSession Session;
			std::string Id = ID;
			int ClockCalls = 0;
			bool Triggered = false;
			size_t Attempts = 0;
			{
				CPredictionAllocationFailure Failure(BeforeFailure);
				Started = Session.TryStart(History, NAME, std::move(Id), 100, "2026-11-01", [&]() noexcept { ++ClockCalls; return INT64_C(1234); });
				Triggered = Failure.Triggered();
				Attempts = Failure.Attempts();
			}
			EXPECT_TRUE(Triggered);
			EXPECT_FALSE(Started);
			EXPECT_EQ(Attempts, BeforeFailure + 1);
			EXPECT_EQ(ClockCalls, 0);
			EXPECT_FALSE(Session.Active());
			EXPECT_EQ(Session.StartTime(), 0);
			EXPECT_TRUE(Session.MapId().empty());
			EXPECT_EQ(Id, ID);
			EXPECT_EQ(History.ToJson(), Before);
			EXPECT_EQ(History.Find(ID), pOriginal);
			ASSERT_TRUE(Session.TryStart(History, NAME, std::move(Id), 100, "2026-11-01", []() noexcept { return INT64_C(1234); }));
			EXPECT_TRUE(Session.Active());
			EXPECT_EQ(Session.MapId(), ID);
			EXPECT_EQ(Session.StartTime(), 1234);
			EXPECT_EQ(History.Size(), 1u);
		}
	}
}

TEST(MapHistorySessionAllocation, NewVisitPreparationFailuresKeepHistoryAndSessionInactiveUntilRetry)
{
	ExpectFailedPreparationAndRetry(false);
}

TEST(MapHistorySessionAllocation, ExistingVisitPreparationFailuresKeepTheCompleteOldRecordUntilRetry)
{
	ExpectFailedPreparationAndRetry(true);
}
