#include <game/client/components/qmclient/water_hammer_indicator_logic.h>
#include <game/layers.h>

#include <gtest/gtest.h>
#include <test/support/prediction_collision_map.h>

#include <initializer_list>
#include <limits>

namespace
{
	class WaterHammerCollisionSampling : public ::testing::Test
	{
	protected:
		CPredictionCollisionMap m_Map;
		CLayers m_Layers;
		CCollision m_Collision;

		void Initialize()
		{
			m_Layers.Init(&m_Map, false);
			m_Collision.Init(&m_Layers);
			ASSERT_EQ(m_Collision.GetWidth(), 16);
			ASSERT_EQ(m_Collision.GetHeight(), 16);
		}
	};
}

TEST_F(WaterHammerCollisionSampling, EmptyGameLayerDoesNotReportAPenalty)
{
	Initialize();
	EXPECT_FALSE(QmIsWaterHammerPenaltyArea(m_Collision, vec2(100, 200)));
}

TEST_F(WaterHammerCollisionSampling, AllFourPenaltyTypesAreReadThroughRealCollision)
{
	for(int Tile : {TILE_DEATH, TILE_FREEZE, TILE_DFREEZE, TILE_LFREEZE})
	{
		m_Map.SetGameTile(4, 4, Tile);
		Initialize();
		EXPECT_TRUE(QmIsWaterHammerPenaltyArea(m_Collision, vec2(144, 144))) << Tile;
	}
}

TEST_F(WaterHammerCollisionSampling, ACornerSampleCanReachTheNeighboringPenaltyTile)
{
	m_Map.SetGameTile(1, 1, TILE_FREEZE);
	Initialize();
	EXPECT_EQ(m_Collision.GetTileIndex(m_Collision.GetPureMapIndex(vec2(31, 31))), TILE_AIR);
	EXPECT_TRUE(QmIsWaterHammerPenaltyArea(m_Collision, vec2(31, 31)));
}

TEST_F(WaterHammerCollisionSampling, ValidNegativePositionsKeepTheExistingMapEdgeClamp)
{
	m_Map.SetGameTile(0, 0, TILE_DEATH);
	Initialize();
	EXPECT_TRUE(QmIsWaterHammerPenaltyArea(m_Collision, vec2(-1000, -1000)));
}

TEST_F(WaterHammerCollisionSampling, InvalidPositionIsNotClampedIntoAPenaltyAndTheNextQueryStillWorks)
{
	m_Map.SetGameTile(0, 0, TILE_FREEZE);
	m_Map.SetGameTile(15, 0, TILE_DEATH);
	Initialize();
	const float Max = static_cast<float>(std::numeric_limits<int>::max());
	EXPECT_FALSE(QmIsWaterHammerPenaltyArea(m_Collision, vec2(Max, 0)));
	EXPECT_FALSE(QmIsWaterHammerPenaltyArea(m_Collision, vec2(0, std::numeric_limits<float>::quiet_NaN())));
	EXPECT_TRUE(QmIsWaterHammerPenaltyArea(m_Collision, vec2(16, 16)));
}
