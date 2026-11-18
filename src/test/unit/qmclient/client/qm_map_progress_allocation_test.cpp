#include <game/client/components/qmclient/map_progress.h>
#include <game/client/components/qmclient/route_render_cache.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

namespace
{
	QmMapProgress::CMap MakeProgressLine(int Width)
	{
		QmMapProgress::CMap Map(Width, 1);
		Map.SetTile(0, QmMapProgress::MakeTile(TILE_START));
		Map.SetTile(Width - 1, QmMapProgress::MakeTile(TILE_FINISH));
		Map.Finalize();
		return Map;
	}
}

TEST(QmMapProgressAllocation, InterruptedTileIndexingCanFinishWithoutPublishingPartialLists)
{
	QmMapProgress::CMap Map(3, 1);
	Map.SetTile(0, QmMapProgress::MakeTile(TILE_START, TILE_FINISH));
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_FINISH));
	bool Triggered;
	{
		CPredictionAllocationFailure Fault(1);
		Map.FinalizeStep(1);
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(Map.Finalized());
	EXPECT_FALSE(Map.Ready());
	EXPECT_TRUE(Map.Sources(0).empty());
	EXPECT_TRUE(Map.Targets(-1).empty());
	Map.Finalize();
	EXPECT_TRUE(Map.Ready());
	EXPECT_EQ(Map.Sources(0), std::vector<int>{0});
	EXPECT_EQ(Map.Targets(-1), (std::vector<int>{0, 2}));
}

TEST(QmMapProgressAllocation, FailedFieldRestartHidesOldEstimateAndRecovers)
{
	const auto Map = MakeProgressLine(5);
	QmMapProgress::CPlayer Player;
	Player.Update(Map, 2, 0, 64);
	ASSERT_TRUE(Player.Estimate().m_Valid);
	const auto Revision = Player.RouteRevision();
	bool Triggered;
	{
		CPredictionAllocationFailure Fault;
		Player.Update(Map, 2, 1, 64);
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(Player.Estimate().m_Valid);
	EXPECT_GT(Player.RouteRevision(), Revision);
	std::vector<int> vRoute;
	EXPECT_FALSE(Player.BuildRoute(vRoute));
	EXPECT_TRUE(vRoute.empty());
	Player.Update(Map, 2, 1, 64);
	ASSERT_TRUE(Player.Estimate().m_Valid);
	EXPECT_FLOAT_EQ(Player.Estimate().m_Progress, 0.5f);
	EXPECT_TRUE(Player.BuildRoute(vRoute));
	EXPECT_EQ(vRoute, (std::vector<int>{2, 3, 4}));
}

TEST(QmMapProgressAllocation, FailedFrontierExpansionCanRestartAtTheSameTile)
{
	QmMapProgress::CMap Map(3, 3);
	Map.SetTile(0, QmMapProgress::MakeTile(TILE_START));
	Map.SetTile(4, QmMapProgress::MakeTile(TILE_FINISH));
	Map.Finalize();
	QmMapProgress::CPlayer Player;
	Player.Update(Map, 1, 0, 0);
	bool Triggered;
	{
		CPredictionAllocationFailure Fault;
		Player.Update(Map, 1, 0, 64);
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(Player.Estimate().m_Valid);
	std::vector<int> vRoute;
	EXPECT_FALSE(Player.BuildRoute(vRoute));
	Player.Update(Map, 1, 0, 64);
	ASSERT_TRUE(Player.Estimate().m_Valid);
	EXPECT_FLOAT_EQ(Player.Estimate().m_Progress, 0.5f);
	EXPECT_TRUE(Player.BuildRoute(vRoute));
	EXPECT_EQ(vRoute, (std::vector<int>{1, 4}));
}

TEST(QmMapProgressAllocation, FailedDebugRouteOutputIsRetriedByCacheWithTheSameKey)
{
	std::vector<int> vRoute;
	vRoute.reserve(1);
	const int Width = static_cast<int>(vRoute.capacity()) + 3;
	const auto Map = MakeProgressLine(Width);
	QmMapProgress::CPlayer Player;
	Player.Update(Map, 1, 0, Width);
	ASSERT_TRUE(Player.Estimate().m_Valid);
	CQmRouteRenderCache Cache;
	const CQmRouteRenderCache::SKey Key{Player.RouteRevision(), Player.RouteIndex(), 0, 0, true, Player.UsesSegmentRoute()};
	int Builds = 0;
	const auto Build = [&](std::vector<vec2> &vPoints) {
		++Builds;
		if(!Player.BuildRoute(vRoute))
			return false;
		for(const int Index : vRoute)
			vPoints.emplace_back(static_cast<float>(Index), 0.0f);
		return true;
	};
	bool Built, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Built = Cache.Update(Key, Build);
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(Built);
	EXPECT_EQ(Builds, 1);
	EXPECT_TRUE(vRoute.empty());
	EXPECT_TRUE(Player.Estimate().m_Valid);
	EXPECT_TRUE(Cache.QueryVisible({0.0f, -1.0f, static_cast<float>(Width), 1.0f}).empty());
	Player.Update(Map, 1, 0, Width);
	ASSERT_EQ(Player.RouteRevision(), Key.m_Revision);
	ASSERT_EQ(Player.RouteIndex(), Key.m_StartIndex);
	ASSERT_TRUE(Cache.Update(Key, Build));
	EXPECT_EQ(Builds, 2);
	EXPECT_EQ(vRoute.front(), 1);
	EXPECT_EQ(vRoute.back(), Width - 1);
	EXPECT_EQ(Cache.QueryVisible({0.0f, -1.0f, static_cast<float>(Width), 1.0f}).size(), static_cast<size_t>(Width - 1));
	EXPECT_TRUE(Cache.Update(Key, Build));
	EXPECT_EQ(Builds, 2);
}
