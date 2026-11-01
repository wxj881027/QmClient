#include <game/client/components/qmclient/map_history_rows.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>
#include <string>
#include <vector>

namespace
{
	struct SRows
	{
		QmMapHistory::CMapHistory m_History;
		std::vector<size_t> m_vIndices;
		std::vector<int> m_vIds;
		std::vector<CButtonContainer> m_vButtons;
		bool Prepare()
		{
			return QmMapHistoryUi::PrepareRows(m_History, QmMapHistory::EMapHistoryFilter::RECENT, m_vIndices, m_vIds, m_vButtons);
		}
	};

	bool Initialize(SRows &Rows, bool Warm)
	{
		Rows.m_History.RecordVisit("A", "a", 1, "2026-11-01");
		Rows.m_History.RecordVisit("B", "b", 2, "2026-11-01");
		if(Warm && !Rows.Prepare())
			return false;
		Rows.m_History.RecordVisit("C", "c", 3, "2026-11-01");
		return true;
	}

	void ExpectEveryPreparationFailureAndRetry(bool Warm)
	{
		size_t Allocations = 0;
		bool Ready = false;
		{
			SRows Probe;
			ASSERT_TRUE(Initialize(Probe, Warm));
			CPredictionAllocationFailure Count(std::numeric_limits<size_t>::max());
			Ready = Probe.Prepare();
			Allocations = Count.Attempts();
		}
		ASSERT_TRUE(Ready);
		ASSERT_GT(Allocations, 0u);
		for(size_t BeforeFailure = 0; BeforeFailure < Allocations; ++BeforeFailure)
		{
			SCOPED_TRACE(BeforeFailure);
			SRows Rows;
			ASSERT_TRUE(Initialize(Rows, Warm));
			const std::string Before = Rows.m_History.ToJson();
			const auto *pOriginal = Rows.m_History.Find("a");
			bool Triggered = false;
			size_t Attempts = 0;
			{
				CPredictionAllocationFailure Failure(BeforeFailure);
				Ready = Rows.Prepare();
				Triggered = Failure.Triggered();
				Attempts = Failure.Attempts();
			}
			EXPECT_TRUE(Triggered);
			EXPECT_FALSE(Ready);
			EXPECT_EQ(Attempts, BeforeFailure + 1);
			EXPECT_TRUE(Rows.m_vIndices.empty());
			EXPECT_EQ(Rows.m_History.ToJson(), Before);
			EXPECT_EQ(Rows.m_History.Find("a"), pOriginal);
			ASSERT_TRUE(Rows.m_History.Remove("a"));
			ASSERT_TRUE(Rows.Prepare());
			ASSERT_EQ(Rows.m_vIndices.size(), 2u);
			EXPECT_EQ(Rows.m_History.Entries()[Rows.m_vIndices[0]].m_MapId, "c");
			EXPECT_EQ(Rows.m_History.Entries()[Rows.m_vIndices[1]].m_MapId, "b");
			EXPECT_EQ(Rows.m_vIds.size(), 2u);
			EXPECT_EQ(Rows.m_vButtons.size(), 2u);
		}
	}
}

TEST(MapHistoryRowsAllocation, ColdPreparationFailuresDiscardIndicesAndRetryAfterModelRemoval)
{
	ExpectEveryPreparationFailureAndRetry(false);
}

TEST(MapHistoryRowsAllocation, GrowthFailuresDiscardPreviouslyPreparedIndicesAndAllowRetry)
{
	ExpectEveryPreparationFailureAndRetry(true);
}

TEST(MapHistoryRowsAllocation, ReadyCapacityRequiresNoAllocationOnTheNextFrame)
{
	SRows Rows;
	ASSERT_TRUE(Initialize(Rows, false));
	ASSERT_TRUE(Rows.Prepare());
	const int *pIds = Rows.m_vIds.data();
	const CButtonContainer *pButtons = Rows.m_vButtons.data();
	bool Ready = false, Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		Ready = Rows.Prepare();
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Ready);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	EXPECT_EQ(Rows.m_vIds.data(), pIds);
	EXPECT_EQ(Rows.m_vButtons.data(), pButtons);
}
