#include <game/client/components/qmclient/collision_hitbox_logic.h>
#include <game/client/components/tclient/qm_outline_neighbors.h>
#include <game/client/components/tclient/qm_outline_viewport.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

TEST(QmOutlineViewportIntegration, SharedViewportKeepsOutlineExtensionAndHitboxMapClipping)
{
	const auto Outline = QmCalcOutlineTileRange({64, 32}, {96, 64}, {120, 120});
	const auto Hitbox = QmCalcHitboxTileRange({64, 32}, {96, 64}, {120, 120}, {3, 3});
	ASSERT_TRUE(Outline);
	ASSERT_TRUE(Hitbox);
	EXPECT_EQ(Outline->m_Begin, ivec2(1, 0));
	EXPECT_EQ(Outline->m_End, ivec2(4, 3));
	EXPECT_EQ(Hitbox->m_Begin, ivec2(1, 0));
	EXPECT_EQ(Hitbox->m_End, ivec2(3, 3));
}

TEST(QmOutlineViewportIntegration, VisibleVirtualEdgeDoesNotReuseTheRealEdgeNeighborMask)
{
	const auto Range = QmCalcOutlineTileRange({64, 32}, {96, 64}, {120, 120});
	ASSERT_TRUE(Range);
	std::array<int, 9> aTiles{1, 2, 7, 3, 4, 5, 7, 6, 0};
	const auto Get = [&](int X, int Y) { return aTiles[std::clamp(Y, 0, 2) * 3 + std::clamp(X, 0, 2)] & 7; };
	int RealEdge = -1, VirtualEdge = -1;
	for(int Y = Range->m_Begin.y; Y < Range->m_End.y; ++Y)
	{
		for(int X = Range->m_Begin.x; X < Range->m_End.x; ++X)
		{
			int &Tile = aTiles[std::clamp(Y, 0, 2) * 3 + std::clamp(X, 0, 2)];
			const int Mask = QmOutlineCachedNeighbors(Tile, X, Y, 3, 3, Get);
			if(Y == 1 && X == 2)
				RealEdge = Mask;
			if(Y == 1 && X == 3)
				VirtualEdge = Mask;
		}
	}
	EXPECT_EQ(RealEdge, 54);
	EXPECT_EQ(VirtualEdge, 31);
}
