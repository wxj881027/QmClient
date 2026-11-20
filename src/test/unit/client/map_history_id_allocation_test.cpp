#include <game/client/components/qmclient/map_history_session.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>
#include <string>

namespace
{
	void ExpectEveryAllocationFailurePreservesOutput(const char *pName, const char *pPath)
	{
		size_t Allocations = 0;
		bool Built = false;
		{
			std::string Probe;
			CPredictionAllocationFailure Count(std::numeric_limits<size_t>::max());
			Built = QmMapHistory::TrySessionMapId(pName, pPath, Probe);
			Allocations = Count.Attempts();
		}
		ASSERT_TRUE(Built);
		ASSERT_GT(Allocations, 0u);
		for(size_t BeforeFailure = 0; BeforeFailure < Allocations; ++BeforeFailure)
		{
			SCOPED_TRACE(BeforeFailure);
			const std::string Before = "previous identity longer than small string capacity";
			std::string Output = Before;
			const char *pOriginal = Output.data();
			bool Triggered = false;
			size_t Attempts = 0;
			{
				CPredictionAllocationFailure Failure(BeforeFailure);
				Built = QmMapHistory::TrySessionMapId(pName, pPath, Output);
				Triggered = Failure.Triggered();
				Attempts = Failure.Attempts();
			}
			EXPECT_TRUE(Triggered);
			EXPECT_FALSE(Built);
			EXPECT_EQ(Attempts, BeforeFailure + 1);
			EXPECT_EQ(Output, Before);
			EXPECT_EQ(Output.data(), pOriginal);
			EXPECT_TRUE(QmMapHistory::TrySessionMapId(pName, pPath, Output));
			EXPECT_NE(Output, Before);
		}
	}
}

TEST(MapHistoryIdAllocation, PathAllocationFailureRetainsTheOldIdentityAndAllowsRetry)
{
	ExpectEveryAllocationFailurePreservesOutput("Map", "maps/long-map-path-that-needs-an-allocation.map");
}

TEST(MapHistoryIdAllocation, NameAllocationFailureRetainsTheOldIdentityAndAllowsRetry)
{
	ExpectEveryAllocationFailurePreservesOutput("A valid map name longer than small string capacity", nullptr);
}
