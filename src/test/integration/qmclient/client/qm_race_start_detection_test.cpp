#include <game/client/components/qmclient/ghost_start_detection.h>
#include <game/client/components/qmclient/race_start_detection.h>
#include <game/layers.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/support/prediction_collision_map.h>

#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
	// 仅替换已解码地图的第二层存储；分层、扫描和起跑判断均使用生产实现。
	class CRaceStartMap : public CPredictionCollisionMap
	{
		CMapItemLayerTilemap m_Front{};
		std::array<CTile, 16 * 16> m_aFrontTiles{};

	public:
		CRaceStartMap()
		{
			m_Front = *static_cast<CMapItemLayerTilemap *>(CPredictionCollisionMap::GetItem(1));
			m_Front.m_Flags = TILESLAYERFLAG_FRONT;
			m_Front.m_Data = -1;
			m_Front.m_Front = 1;
		}
		void SetFrontTile(int X, int Y, int Tile)
		{
			m_aFrontTiles[Y * 16 + X].m_Index = static_cast<unsigned char>(Tile);
		}
		int GetDataSize(int Index) const override
		{
			return Index == 1 ? static_cast<int>(sizeof(m_aFrontTiles)) : CPredictionCollisionMap::GetDataSize(Index);
		}
		void *GetData(int Index) override
		{
			return Index == 1 ? m_aFrontTiles.data() : CPredictionCollisionMap::GetData(Index);
		}
		void *GetItem(int Index, int *pType = nullptr, int *pId = nullptr) override
		{
			void *pItem = CPredictionCollisionMap::GetItem(Index, pType, pId);
			return Index == 2 ? &m_Front : pItem;
		}
	};

	class QmRaceStartDetection : public ::testing::Test
	{
	protected:
		CRaceStartMap m_Map;
		CLayers m_Layers;
		CCollision m_Collision;

		void Initialize()
		{
			m_Layers.Init(&m_Map, false);
			m_Collision.Init(&m_Layers);
			ASSERT_EQ(m_Collision.GetWidth(), 16);
			ASSERT_EQ(m_Collision.GetHeight(), 16);
			ASSERT_NE(m_Collision.FrontLayer(), nullptr);
		}
	};
}

TEST_F(QmRaceStartDetection, CrossingAGameStartUsesTheOriginalVisitedTiles)
{
	m_Map.SetGameTile(2, 1, TILE_START);
	Initialize();
	const vec2 Prev(48, 48), Pos(112, 48);
	ASSERT_EQ(m_Collision.GetMapIndices(Prev, Pos), (std::vector<int>{18}));
	EXPECT_TRUE(qm_race_start::IsTileStart(m_Collision, Prev, Pos));
	EXPECT_TRUE(qm_race_start::IsTileStart(m_Collision, Pos, Prev));
	EXPECT_FALSE(qm_race_start::IsTileStart(m_Collision, vec2(48, 80), vec2(112, 80)));
}

TEST_F(QmRaceStartDetection, FrontStartsAreReadDuringScanningAndTheEmptyListFallback)
{
	m_Map.SetFrontTile(2, 1, TILE_START);
	m_Map.SetFrontTile(0, 0, TILE_START);
	Initialize();
	EXPECT_TRUE(qm_race_start::IsTileStart(m_Collision, vec2(48, 48), vec2(112, 48)));
	ASSERT_TRUE(m_Collision.GetMapIndices(vec2(5, 5), vec2(10, 10)).empty());
	EXPECT_TRUE(qm_race_start::IsTileStart(m_Collision, vec2(5, 5), vec2(10, 10)));
}

TEST_F(QmRaceStartDetection, EmptyVisitedListKeepsTheDestinationFallbackAtIndexZero)
{
	m_Map.SetGameTile(0, 0, TILE_START);
	Initialize();
	const vec2 Prev(5, 5), Pos(10, 10);
	ASSERT_TRUE(m_Collision.GetMapIndices(Prev, Pos).empty());
	EXPECT_TRUE(qm_race_start::IsTileStart(m_Collision, Prev, Pos));
	EXPECT_TRUE(qm_race_start::IsTileStart(m_Collision, Pos, Pos));
}

TEST_F(QmRaceStartDetection, NonemptyVisitedListDoesNotAddAnEndpointResample)
{
	m_Map.SetGameTile(1, 0, TILE_FREEZE);
	m_Map.SetGameTile(2, 0, TILE_START);
	Initialize();
	const vec2 Prev(63.75f, 16), Pos(64.25f, 16);
	ASSERT_EQ(m_Collision.GetMapIndices(Prev, Pos), (std::vector<int>{1}));
	ASSERT_EQ(m_Collision.GetTileIndex(m_Collision.GetPureMapIndex(Pos)), TILE_START);
	EXPECT_FALSE(qm_race_start::IsTileStart(m_Collision, Prev, Pos));
}

TEST_F(QmRaceStartDetection, InvalidQueriesDoNotScanOrClampIntoAnEdgeStart)
{
	for(const int X : {0, 15})
		for(const int Y : {0, 15})
			m_Map.SetGameTile(X, Y, TILE_START);
	Initialize();
	const float Upper = static_cast<float>(std::numeric_limits<int>::max());
	const float Inf = std::numeric_limits<float>::infinity();
	const float Lower = static_cast<float>(std::numeric_limits<int>::min());
	const std::array Queries{
		std::pair{vec2(Upper, 0), vec2(Upper, 0)},
		std::pair{vec2(0, Upper), vec2(0, Upper)},
		std::pair{vec2(std::nextafter(Lower, -Inf), 0), vec2(16, 16)},
		std::pair{vec2(16, 16), vec2(Inf, 0)},
		std::pair{vec2(0, std::numeric_limits<float>::quiet_NaN()), vec2(16, 16)},
		std::pair{vec2(-1000000000.0f, -1000000000.0f), vec2(1000000000.0f, 1000000000.0f)},
		std::pair{vec2(-1073741824.0f, 0), vec2(1073741760.0f, 0)}};
	for(const auto &[Prev, Pos] : Queries)
	{
		SCOPED_TRACE(::testing::Message() << Prev.x << "," << Prev.y << " -> " << Pos.x << "," << Pos.y);
		bool Started, Triggered;
		std::size_t Attempts;
		{
			CPredictionAllocationFailure Failure;
			Started = qm_race_start::IsTileStart(m_Collision, Prev, Pos);
			Triggered = Failure.Triggered();
			Attempts = Failure.Attempts();
		}
		EXPECT_FALSE(Started);
		EXPECT_FALSE(Triggered);
		EXPECT_EQ(Attempts, 0u);
	}
	EXPECT_TRUE(qm_race_start::IsTileStart(m_Collision, vec2(16, 16), vec2(16, 16)));
}

TEST_F(QmRaceStartDetection, RepresentableIntegerEdgePositionsKeepTheExistingMapClamp)
{
	m_Map.SetGameTile(0, 0, TILE_START);
	m_Map.SetGameTile(15, 15, TILE_START);
	Initialize();
	const float Lower = static_cast<float>(std::numeric_limits<int>::min());
	const float LastPositive = std::nextafter(static_cast<float>(std::numeric_limits<int>::max()), 0.0f);
	EXPECT_TRUE(qm_race_start::IsTileStart(m_Collision, vec2(Lower, Lower), vec2(Lower, Lower)));
	EXPECT_TRUE(qm_race_start::IsTileStart(m_Collision, vec2(LastPositive, LastPositive), vec2(LastPositive, LastPositive)));
}

TEST_F(QmRaceStartDetection, GhostSubsegmentsKeepTheFirstAndLastMatchingSnapshotTick)
{
	m_Map.SetGameTile(2, 1, TILE_START);
	Initialize();
	const qm_ghost_start::SSample Prev{0, 100, vec2(16, 48)};
	const qm_ghost_start::SSample Cur{0, 104, vec2(144, 48)};
	const auto IsStart = [&](vec2 From, vec2 To) { return qm_race_start::IsTileStart(m_Collision, From, To); };
	EXPECT_EQ(qm_ghost_start::FindStartTick(Prev, Cur, -1, false, IsStart), 102);
	EXPECT_EQ(qm_ghost_start::FindStartTick(Prev, Cur, -1, true, IsStart), 103);
}

TEST_F(QmRaceStartDetection, EveryNativeScanAllocationFailureReturnsFalseAndTheSameLineRetries)
{
	for(int X = 0; X < 16; ++X)
		m_Map.SetGameTile(X, 1, TILE_FREEZE);
	m_Map.SetGameTile(8, 1, TILE_START);
	Initialize();
	const vec2 Prev(16, 48), Pos(496, 48);
	ASSERT_TRUE(qm_race_start::IsTileStart(m_Collision, Prev, Pos));
	std::size_t Allocations;
	bool Started;
	{
		CPredictionAllocationFailure Trace(std::numeric_limits<std::size_t>::max());
		Started = qm_race_start::IsTileStart(m_Collision, Prev, Pos);
		Allocations = Trace.Attempts();
	}
	ASSERT_TRUE(Started);
	ASSERT_GT(Allocations, 0u);
	for(std::size_t Allocation = 0; Allocation < Allocations; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		bool Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			Started = qm_race_start::IsTileStart(m_Collision, Prev, Pos);
			Triggered = Failure.Triggered();
		}
		EXPECT_TRUE(Triggered);
		EXPECT_FALSE(Started);
		EXPECT_EQ(m_Collision.GetTileIndex(24), TILE_START);
		EXPECT_TRUE(qm_race_start::IsTileStart(m_Collision, Prev, Pos));
	}
}
