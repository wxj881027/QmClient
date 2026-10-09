#include <game/client/components/qmclient/map_progress.h>
#include <game/client/components/qmclient/route_render_cache.h>

#include <gtest/gtest.h>

namespace
{
	QmMapProgress::CMap MakeRouteMap()
	{
		QmMapProgress::CMap Map(5, 1);
		for(int Index = 0; Index < 5; ++Index)
			Map.SetTile(Index, QmMapProgress::MakeTile(Index == 0 ? TILE_START : Index == 4 ? TILE_FINISH :
													  TILE_AIR));
		Map.Finalize();
		return Map;
	}

	CQmRouteRenderCache::SKey RouteKey(const QmMapProgress::CPlayer &Player)
	{
		return {Player.RouteRevision(), Player.RouteIndex(), 0, 0, true, Player.UsesSegmentRoute()};
	}

	bool PublishRoute(CQmRouteRenderCache &Cache, const QmMapProgress::CPlayer &Player, int &Builds)
	{
		return Cache.Update(RouteKey(Player), [&](std::vector<vec2> &vPoints) {
			++Builds;
			std::vector<int> vIndices;
			if(!Player.BuildRoute(vIndices))
				return false;
			for(const int Index : vIndices)
				vPoints.emplace_back(static_cast<float>(Index), 0.0f);
			return true;
		});
	}
}

TEST(QmMapRouteCache, CompletedFieldAndUnchangedPlayerReuseThePublishedRoute)
{
	auto Map = MakeRouteMap();
	QmMapProgress::CPlayer Player;
	CQmRouteRenderCache Cache;
	int Builds = 0;
	Player.Observe(Map.Tile(0), 0);
	Player.Update(Map, 0, 0, 128);
	ASSERT_TRUE(PublishRoute(Cache, Player, Builds));
	for(int Frame = 0; Frame < 128; ++Frame)
	{
		Player.Update(Map, 0, 0, 128);
		ASSERT_TRUE(PublishRoute(Cache, Player, Builds));
	}
	EXPECT_EQ(Builds, 1);
	EXPECT_EQ(Cache.QueryVisible({-1.0f, -1.0f, 5.0f, 1.0f}).size(), 5u);
}

TEST(QmMapRouteCache, MovingToAnotherTileChangesTheRouteOrigin)
{
	auto Map = MakeRouteMap();
	QmMapProgress::CPlayer Player;
	CQmRouteRenderCache Cache;
	int Builds = 0;
	Player.Update(Map, 0, 0, 128);
	ASSERT_TRUE(PublishRoute(Cache, Player, Builds));
	Player.Update(Map, 2, 0, 128);
	ASSERT_TRUE(PublishRoute(Cache, Player, Builds));
	EXPECT_EQ(Builds, 2);
	EXPECT_EQ(Cache.Point(0), vec2(2.0f, 0.0f));
	EXPECT_EQ(Cache.QueryVisible({-1.0f, -1.0f, 5.0f, 1.0f}).size(), 3u);
}

TEST(QmMapRouteCache, IncrementalFieldCompletionRefreshesAnInitiallyUnavailableRoute)
{
	auto Map = MakeRouteMap();
	QmMapProgress::CPlayer Player;
	CQmRouteRenderCache Cache;
	int Builds = 0;
	Player.Update(Map, 0, 0, 1);
	EXPECT_FALSE(PublishRoute(Cache, Player, Builds));
	EXPECT_FALSE(PublishRoute(Cache, Player, Builds));
	EXPECT_EQ(Builds, 1);
	Player.Update(Map, 0, 0, 128);
	ASSERT_TRUE(PublishRoute(Cache, Player, Builds));
	EXPECT_EQ(Builds, 2);
	EXPECT_EQ(Cache.Point(0), vec2(0.0f, 0.0f));
}

TEST(QmMapRouteCache, TeleCheckpointChangeRefreshesRouteWithoutMovingThePlayer)
{
	QmMapProgress::CMap Map(5, 2);
	for(int Index = 0; Index < 10; ++Index)
		Map.SetTile(Index, QmMapProgress::MakeTile(TILE_SOLID));
	Map.SetTile(0, QmMapProgress::MakeTile(TILE_START));
	Map.SetTile(1, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELECHECKIN));
	Map.SetTile(3, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELECHECKOUT, 1));
	Map.SetTile(4, QmMapProgress::MakeTile(TILE_FINISH));
	Map.SetTile(8, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELECHECKOUT, 2));
	Map.SetTile(9, QmMapProgress::MakeTile(TILE_AIR));
	Map.Finalize();
	QmMapProgress::CPlayer Player;
	CQmRouteRenderCache Cache;
	int Builds = 0;
	Player.Update(Map, 1, 1, 128);
	ASSERT_TRUE(PublishRoute(Cache, Player, Builds));
	EXPECT_EQ(Cache.Point(1), vec2(3.0f, 0.0f));
	Player.Update(Map, 1, 2, 128);
	ASSERT_TRUE(PublishRoute(Cache, Player, Builds));
	EXPECT_EQ(Builds, 2);
	EXPECT_EQ(Cache.Point(1), vec2(8.0f, 0.0f));
}

TEST(QmMapRouteCache, EnteringATimeCheckpointRefreshesTheSegmentAtTheSameTile)
{
	QmMapProgress::CMap Map(7, 1);
	for(int Index = 0; Index < 7; ++Index)
		Map.SetTile(Index, QmMapProgress::MakeTile(Index == 0 ? TILE_START : Index == 6 ? TILE_FINISH :
												  TILE_AIR));
	Map.SetTile(3, QmMapProgress::MakeTile(TILE_TIME_CHECKPOINT_FIRST));
	Map.Finalize();
	QmMapProgress::CPlayer Player;
	CQmRouteRenderCache Cache;
	int Builds = 0;
	Player.Observe(Map.Tile(0), 0);
	Player.Update(Map, 3, 0, 128);
	ASSERT_TRUE(Player.UsesSegmentRoute());
	ASSERT_TRUE(PublishRoute(Cache, Player, Builds));
	ASSERT_EQ(Cache.QueryVisible({-1.0f, -1.0f, 7.0f, 1.0f}).size(), 1u);
	Player.Observe(Map.Tile(3), 3);
	Player.Update(Map, 3, 0, 128);
	ASSERT_TRUE(Player.UsesSegmentRoute());
	ASSERT_TRUE(PublishRoute(Cache, Player, Builds));
	EXPECT_EQ(Builds, 2);
	EXPECT_EQ(Cache.QueryVisible({-1.0f, -1.0f, 7.0f, 1.0f}).size(), 4u);
	EXPECT_EQ(Cache.Point(1), vec2(4.0f, 0.0f));
}
