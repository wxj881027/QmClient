#include <game/client/components/qmclient/route_visited.h>

#include <gtest/gtest.h>

TEST(QmRouteVisited, LoopsRemainDetectedAndSeparateFramesDoNotShareMarks)
{
	CQmRouteVisited Visited;
	for(int Frame = 0; Frame < 128; ++Frame)
	{
		Visited.Begin(1024 * 1024);
		for(size_t Tile : {size_t(0), size_t(63), size_t(64), size_t(511), size_t(1024 * 1024 - 1)})
		{
			EXPECT_TRUE(Visited.Visit(Tile));
			EXPECT_FALSE(Visited.Visit(Tile));
		}
	}
	// 相同字数但尺寸变化、缩小地图、清空地图都不能保留上一条路径的标记。
	Visited.Begin(65);
	EXPECT_TRUE(Visited.Visit(64));
	Visited.Begin(66);
	EXPECT_TRUE(Visited.Visit(64));
	Visited.Begin(1);
	EXPECT_TRUE(Visited.Visit(0));
	Visited.Reset();
	Visited.Begin(1);
	EXPECT_TRUE(Visited.Visit(0));
	Visited.Begin(0);
	Visited.Begin(1024 * 1024);
	EXPECT_TRUE(Visited.Visit(1024 * 1024 - 1));
}
