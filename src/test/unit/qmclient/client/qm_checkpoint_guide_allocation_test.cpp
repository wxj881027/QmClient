#include <game/client/components/qmclient/checkpoint_guide_logic.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>
#include <stdexcept>

namespace
{
	constexpr int WIDTH = 65;
	constexpr int START = 32 * WIDTH + 32;
	constexpr int TARGET = START + 32;

	QmMapProgress::STile ReadSearchTile(int Index)
	{
		if(Index == TARGET)
			return QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELECHECK, 9);
		if(Index == START - 2 * WIDTH)
			return QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELECHECKIN);
		return QmMapProgress::MakeTile(TILE_AIR);
	}

	struct SAttempt
	{
		bool m_Success;
		bool m_Triggered;
		size_t m_Attempts;
	};

	SAttempt SearchWithFailure(QmCheckpointGuide::CPlanner &Planner, size_t Budget)
	{
		CPredictionAllocationFailure Fault(Budget);
		const bool Success = Planner.Update(WIDTH, WIDTH, START, ReadSearchTile);
		return {Success, Fault.Triggered(), Fault.Attempts()};
	}

	void ExpectNoRoute(const QmCheckpointGuide::CPlanner &Planner)
	{
		EXPECT_EQ(Planner.Result().m_Checkpoint, 0);
		EXPECT_EQ(Planner.Result().m_TargetIndex, -1);
		EXPECT_FALSE(Planner.Result().m_Warning);
		EXPECT_TRUE(Planner.Result().m_vRoute.empty());
	}

	// 找到真实历史容器的下一次扩容，不把某个标准库的增长倍率写进测试。
	int NextHistoryGrowth()
	{
		QmCheckpointGuide::CPlanner Planner;
		Planner.ObserveCheckpoint(0);
		for(int Checkpoint = 1; Checkpoint <= 255; ++Checkpoint)
		{
			size_t Attempts = 0;
			bool Success = false;
			{
				CPredictionAllocationFailure Count(std::numeric_limits<size_t>::max());
				Success = Planner.ObserveCheckpoint(Checkpoint);
				Attempts = Count.Attempts();
			}
			if(!Success)
				return 0;
			if(Checkpoint > 2 && Attempts > 0)
				return Checkpoint;
		}
		return 0;
	}
}

TEST(QmCheckpointGuideAllocation, EveryColdSearchAllocationFailureClearsTheResultAndCanRetry)
{
	QmCheckpointGuide::CPlanner Baseline;
	ASSERT_TRUE(Baseline.ObserveCheckpoint(0));
	const auto Counted = SearchWithFailure(Baseline, std::numeric_limits<size_t>::max());
	ASSERT_TRUE(Counted.m_Success);
	ASSERT_GT(Counted.m_Attempts, 0u);
	ASSERT_EQ(Baseline.Result().m_TargetIndex, TARGET);
	ASSERT_EQ(Baseline.Result().m_Checkpoint, 9);
	ASSERT_TRUE(Baseline.Result().m_Warning);
	ASSERT_EQ(Baseline.Result().m_vRoute.size(), 33u);
	const auto Expected = Baseline.Result();
	for(size_t Budget = 0; Budget < Counted.m_Attempts; ++Budget)
	{
		SCOPED_TRACE(Budget);
		QmCheckpointGuide::CPlanner Planner;
		ASSERT_TRUE(Planner.ObserveCheckpoint(0));
		const auto Failed = SearchWithFailure(Planner, Budget);
		ASSERT_TRUE(Failed.m_Triggered);
		EXPECT_FALSE(Failed.m_Success);
		EXPECT_EQ(Planner.Checkpoint(), 0);
		ExpectNoRoute(Planner);
		ASSERT_TRUE(Planner.Update(WIDTH, WIDTH, START, ReadSearchTile));
		EXPECT_EQ(Planner.Result().m_TargetIndex, Expected.m_TargetIndex);
		EXPECT_EQ(Planner.Result().m_Checkpoint, Expected.m_Checkpoint);
		EXPECT_EQ(Planner.Result().m_Warning, Expected.m_Warning);
		EXPECT_EQ(Planner.Result().m_vRoute, Expected.m_vRoute);
	}
}

TEST(QmCheckpointGuideAllocation, WarmSearchReusesItsBuffersAndKeepsTheCompleteRoute)
{
	QmCheckpointGuide::CPlanner Planner;
	ASSERT_TRUE(Planner.ObserveCheckpoint(0));
	ASSERT_TRUE(Planner.Update(WIDTH, WIDTH, START, ReadSearchTile));
	const auto Expected = Planner.Result();
	const auto Attempt = SearchWithFailure(Planner, 0);
	EXPECT_TRUE(Attempt.m_Success);
	EXPECT_FALSE(Attempt.m_Triggered);
	EXPECT_EQ(Attempt.m_Attempts, 0u);
	EXPECT_EQ(Planner.Result().m_TargetIndex, Expected.m_TargetIndex);
	EXPECT_EQ(Planner.Result().m_vRoute, Expected.m_vRoute);
}

TEST(QmCheckpointGuideAllocation, HistoryGrowthFailureKeepsTheLastCheckpointAndEarlierCollection)
{
	const int NewCheckpoint = NextHistoryGrowth();
	ASSERT_GT(NewCheckpoint, 2);
	QmCheckpointGuide::CPlanner Planner;
	ASSERT_TRUE(Planner.ObserveCheckpoint(0));
	for(int Checkpoint = 1; Checkpoint < NewCheckpoint; ++Checkpoint)
		ASSERT_TRUE(Planner.ObserveCheckpoint(Checkpoint));
	const auto Read = [NewCheckpoint](int Index) {
		return QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, Index == 0 || Index == 4 ? TILE_TELECHECK : 0, Index == 0 ? 1 : NewCheckpoint);
	};
	ASSERT_TRUE(Planner.Update(5, 1, 1, Read));
	ASSERT_EQ(Planner.Result().m_Checkpoint, NewCheckpoint);
	bool Success = true, Triggered = false;
	{
		CPredictionAllocationFailure Fault;
		Success = Planner.ObserveCheckpoint(NewCheckpoint);
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(Success);
	EXPECT_EQ(Planner.Checkpoint(), NewCheckpoint - 1);
	ExpectNoRoute(Planner);
	ASSERT_TRUE(Planner.Update(5, 1, 1, Read));
	EXPECT_EQ(Planner.Result().m_Checkpoint, NewCheckpoint);
	ASSERT_TRUE(Planner.ObserveCheckpoint(NewCheckpoint));
	ASSERT_TRUE(Planner.Update(5, 1, 1, Read));
	ExpectNoRoute(Planner);
	ASSERT_TRUE(Planner.ObserveCheckpoint(1));
	ASSERT_TRUE(Planner.Update(5, 1, 1, Read));
	EXPECT_EQ(Planner.Result().m_Checkpoint, NewCheckpoint);
}

TEST(QmCheckpointGuideAllocation, FailedFirstCheckpointCanBeObservedAgainAndResetForANewLife)
{
	QmCheckpointGuide::CPlanner Planner;
	ASSERT_TRUE(Planner.ObserveCheckpoint(0));
	bool Success = true, Triggered = false;
	{
		CPredictionAllocationFailure Fault;
		Success = Planner.ObserveCheckpoint(9);
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(Success);
	EXPECT_EQ(Planner.Checkpoint(), 0);
	ExpectNoRoute(Planner);
	ASSERT_TRUE(Planner.ObserveCheckpoint(9));
	EXPECT_EQ(Planner.Checkpoint(), 9);
	Planner.Reset();
	ASSERT_TRUE(Planner.ObserveCheckpoint(0));
	ASSERT_TRUE(Planner.Update(WIDTH, WIDTH, START, ReadSearchTile));
	EXPECT_EQ(Planner.Result().m_Checkpoint, 9);
}

TEST(QmCheckpointGuideAllocation, SearchFailurePreservesCollectedCheckpointsForTheNextAttempt)
{
	QmCheckpointGuide::CPlanner Planner;
	ASSERT_TRUE(Planner.ObserveCheckpoint(1));
	ASSERT_TRUE(Planner.ObserveCheckpoint(2));
	const auto Read = [](int Index) {
		return QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, Index == 0 || Index == 4 ? TILE_TELECHECK : 0, Index == 0 ? 1 : 9);
	};
	bool Success = true, Triggered = false;
	{
		CPredictionAllocationFailure Fault;
		Success = Planner.Update(5, 1, 1, Read);
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(Success);
	EXPECT_EQ(Planner.Checkpoint(), 2);
	ExpectNoRoute(Planner);
	ASSERT_TRUE(Planner.Update(5, 1, 1, Read));
	EXPECT_EQ(Planner.Result().m_Checkpoint, 9);
}

TEST(QmCheckpointGuideAllocation, UnrelatedTileReadExceptionPropagatesWithoutPublishingATarget)
{
	QmCheckpointGuide::CPlanner Planner;
	ASSERT_TRUE(Planner.ObserveCheckpoint(0));
	EXPECT_THROW(Planner.Update(1, 1, 0, [](int) -> QmMapProgress::STile {
		throw std::logic_error("unrelated checkpoint map failure");
	}),
		std::logic_error);
	ExpectNoRoute(Planner);
	ASSERT_TRUE(Planner.Update(WIDTH, WIDTH, START, ReadSearchTile));
	EXPECT_EQ(Planner.Result().m_Checkpoint, 9);
}
