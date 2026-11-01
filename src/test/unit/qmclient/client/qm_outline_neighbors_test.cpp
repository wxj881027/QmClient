#include <game/client/components/tclient/qm_outline_neighbors.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

namespace
{
	struct SGrid
	{
		std::array<int, 9> m_aTiles{1, 2, 7, 3, 4, 5, 7, 6, 0};
		int m_Reads = 0;
		int Get(int X, int Y)
		{
			++m_Reads;
			return m_aTiles[std::clamp(Y, 0, 2) * 3 + std::clamp(X, 0, 2)] & 7;
		}
	};
}

TEST(QmOutlineNeighbors, CachePreservesPriorityAndVirtualMapEdges)
{
	SGrid Grid;
	const auto Get = [&](int X, int Y) { return Grid.Get(X, Y); };
	EXPECT_EQ(QmOutlineCachedNeighbors(Grid.m_aTiles[4], 1, 1, 3, 3, Get), 116);
	EXPECT_EQ(Grid.m_Reads, 8);
	for(int Frame = 0; Frame < 240; ++Frame)
		EXPECT_EQ(QmOutlineCachedNeighbors(Grid.m_aTiles[4], 1, 1, 3, 3, Get), 116);
	EXPECT_EQ(Grid.m_Reads, 8);
	EXPECT_EQ(QmOutlineCachedNeighbors(Grid.m_aTiles[5], 2, 1, 3, 3, Get), 54);
	const int CachedEdge = Grid.m_aTiles[5];
	EXPECT_EQ(QmOutlineCachedNeighbors(Grid.m_aTiles[5], 3, 1, 3, 3, Get), 31);
	EXPECT_EQ(Grid.m_aTiles[5], CachedEdge);
	EXPECT_EQ(QmOutlineCachedNeighbors(Grid.m_aTiles[5], 2, 1, 3, 3, Get), 54);
	EXPECT_EQ(Grid.m_Reads, 24);
}

TEST(QmOutlineNeighbors, VirtualCoordinatesNeverPublishAClampedCellCache)
{
	SGrid Grid;
	const auto Get = [&](int X, int Y) { return Grid.Get(X, Y); };
	EXPECT_EQ(QmOutlineCachedNeighbors(Grid.m_aTiles[5], 3, 1, 3, 3, Get), 31);
	EXPECT_EQ(Grid.m_aTiles[5], 5);
	EXPECT_EQ(QmOutlineCachedNeighbors(Grid.m_aTiles[5], 4, 1, 3, 3, Get), 31);
	EXPECT_EQ(Grid.m_aTiles[5], 5);
	EXPECT_EQ(Grid.m_Reads, 16);
}

TEST(QmOutlineNeighbors, NewMapTypesInvalidateThePreviousMapCache)
{
	SGrid Grid;
	const auto Get = [&](int X, int Y) { return Grid.Get(X, Y); };
	ASSERT_EQ(QmOutlineCachedNeighbors(Grid.m_aTiles[4], 1, 1, 3, 3, Get), 116);
	Grid.m_aTiles.fill(4);
	Grid.m_Reads = 0;
	EXPECT_EQ(QmOutlineCachedNeighbors(Grid.m_aTiles[4], 1, 1, 3, 3, Get), 255);
	EXPECT_EQ(Grid.m_Reads, 8);
}

TEST(QmOutlineNeighbors, CallbackFailureDoesNotPublishAPartialCacheAndCanRetry)
{
	SGrid Grid;
	bool Fail = true;
	int Calls = 0;
	const auto Get = [&](int X, int Y) {
		++Calls;
		if(Fail && Calls == 4)
			throw 17;
		return Grid.Get(X, Y);
	};
	EXPECT_THROW(QmOutlineCachedNeighbors(Grid.m_aTiles[4], 1, 1, 3, 3, Get), int);
	EXPECT_EQ(Grid.m_aTiles[4], 4);
	EXPECT_EQ(Calls, 4);
	Fail = false;
	Calls = 0;
	EXPECT_EQ(QmOutlineCachedNeighbors(Grid.m_aTiles[4], 1, 1, 3, 3, Get), 116);
	EXPECT_EQ(Calls, 8);
}

TEST(QmOutlineNeighbors, NeighborBitsMatchTheConsumersCornerAndEdgeOrder)
{
	const std::array<std::array<int, 2>, 8> aCases{{{0, 1}, {1, 2}, {2, 4}, {3, 8}, {5, 16}, {6, 32}, {7, 64}, {8, 128}}};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case[0]);
		SGrid Grid;
		Grid.m_aTiles.fill(3);
		Grid.m_aTiles[4] = 4;
		Grid.m_aTiles[Case[0]] = 5;
		EXPECT_EQ(QmOutlineCachedNeighbors(Grid.m_aTiles[4], 1, 1, 3, 3, [&](int X, int Y) { return Grid.Get(X, Y); }), Case[1]);
	}
}
