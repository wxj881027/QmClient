#include <game/client/components/qmclient/ghost_start_detection.h>
#include <game/client/components/qmclient/race_start_detection.h>
#include <game/layers.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/support/prediction_collision_map.h>

#include <array>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace
{
	// 只替换已解码地图的第二层存储，分层和碰撞读取仍使用真实实现。
	class CStartScanMap : public CPredictionCollisionMap
	{
		CMapItemLayerTilemap m_Front{};
		std::array<CTile, 16 * 16> m_aFrontTiles{};

	public:
		CStartScanMap()
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

	class QmRaceStartMapIndexScan : public ::testing::Test
	{
	protected:
		CStartScanMap m_Map;
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

		uint64_t MaximumGroups() const
		{
			return static_cast<uint64_t>(m_Collision.GetWidth()) + m_Collision.GetHeight() - 1;
		}

		void ExpectWorkBound(const qm_race_start::CTileStartMapIndexScan &Scan)
		{
			EXPECT_LE(Scan.CellGroups(), MaximumGroups());
			EXPECT_LE(Scan.SampleEvaluations(), 2u + 32u * Scan.CellGroups());
		}

		std::vector<int> Consume(qm_race_start::CTileStartMapIndexScan &Scan)
		{
			std::vector<int> vIndices;
			int Index;
			while(Scan.NextAcceptedIndex(Index))
			{
				if(vIndices.size() >= MaximumGroups())
				{
					ADD_FAILURE() << "scan revisited too many map cells";
					break;
				}
				vIndices.push_back(Index);
			}
			const uint64_t Samples = Scan.SampleEvaluations(), Groups = Scan.CellGroups();
			Index = -99;
			EXPECT_FALSE(Scan.NextAcceptedIndex(Index));
			EXPECT_EQ(Index, -99);
			EXPECT_EQ(Scan.SampleEvaluations(), Samples);
			EXPECT_EQ(Scan.CellGroups(), Groups);
			ExpectWorkBound(Scan);
			return vIndices;
		}
	};
}

TEST_F(QmRaceStartMapIndexScan, GroupedSequenceMatchesRealCollisionForBoundedFloatSegments)
{
	for(int Y = 0; Y < 16; ++Y)
	{
		for(int X = 0; X < 16; ++X)
		{
			if((X + 2 * Y) % 3 == 0)
				m_Map.SetGameTile(X, Y, TILE_FREEZE);
			if((2 * X + Y) % 5 == 0)
				m_Map.SetFrontTile(X, Y, TILE_STOPA);
		}
	}
	m_Map.SetGameTile(2, 1, TILE_START);
	m_Map.SetFrontTile(7, 4, TILE_START);
	Initialize();
	const std::array Lines{
		std::pair{vec2(0, 0), vec2(0, 0)},
		std::pair{vec2(0, 0), vec2(std::numeric_limits<float>::denorm_min(), 0)},
		std::pair{vec2(16, 48), vec2(496, 48)},
		std::pair{vec2(48, 16), vec2(48, 496)},
		std::pair{vec2(31.75f, 31.5f), vec2(32.5f, 32.25f)},
		std::pair{vec2(31.75f, 31.25f), vec2(384.25f, 225.75f)},
		std::pair{vec2(-160, 63.75f), vec2(700, 64.25f)},
		std::pair{vec2(490, 16), vec2(16, 490)},
		std::pair{vec2(-1000, -1000), vec2(600, 600)},
		std::pair{vec2(-5000, 144), vec2(595, 144)}};
	for(const auto &[From, To] : Lines)
	{
		for(const bool Reverse : {false, true})
		{
			const vec2 Prev = Reverse ? To : From, Pos = Reverse ? From : To;
			SCOPED_TRACE(::testing::Message() << Prev.x << "," << Prev.y << " -> " << Pos.x << "," << Pos.y);
			qm_race_start::CTileStartMapIndexScan Scan(m_Collision, Prev, Pos);
			ASSERT_TRUE(Scan.IsValid());
			EXPECT_EQ(Consume(Scan), m_Collision.GetMapIndices(Prev, Pos));
		}
	}
}

TEST_F(QmRaceStartMapIndexScan, RoundedTailSampleCanEnterTheCellBeyondTheEndpoint)
{
	m_Map.SetGameTile(1, 0, TILE_START);
	Initialize();
	const vec2 Prev(-16777188.0f, 0), Pos(30, 0);
	qm_race_start::CTileStartMapIndexScan Scan(m_Collision, Prev, Pos);
	ASSERT_TRUE(Scan.IsValid());
	EXPECT_EQ(Scan.EndSampleCount(), 16777220);
	EXPECT_EQ(Consume(Scan), (std::vector<int>{1}));
	EXPECT_EQ(m_Collision.GetTileIndex(m_Collision.GetPureMapIndex(Pos)), TILE_AIR);
	EXPECT_TRUE(qm_race_start::IsTileStart(m_Collision, Prev, Pos));
	EXPECT_FALSE(qm_race_start::IsTileStart(m_Collision, Pos, Prev));
}

TEST_F(QmRaceStartMapIndexScan, AcceptedNonStartTailSuppressesTheEndpointFallback)
{
	m_Map.SetGameTile(0, 0, TILE_START);
	m_Map.SetGameTile(1, 0, TILE_FREEZE);
	Initialize();
	const vec2 Prev(-16777188.0f, 0), Pos(30, 0);
	qm_race_start::CTileStartMapIndexScan Forward(m_Collision, Prev, Pos);
	qm_race_start::CTileStartMapIndexScan Reverse(m_Collision, Pos, Prev);
	ASSERT_TRUE(Forward.IsValid());
	ASSERT_TRUE(Reverse.IsValid());
	EXPECT_EQ(Consume(Forward), (std::vector<int>{1}));
	EXPECT_TRUE(Consume(Reverse).empty());
	EXPECT_FALSE(qm_race_start::IsTileStart(m_Collision, Prev, Pos));
	EXPECT_TRUE(qm_race_start::IsTileStart(m_Collision, Pos, Prev));
}

TEST_F(QmRaceStartMapIndexScan, BillionUnitDirectionsKeepTheOriginalFloatQuantization)
{
	m_Map.SetGameTile(1, 1, TILE_START);
	Initialize();
	const vec2 Near(0, 48), Far(1000000000.0f, 48);
	qm_race_start::CTileStartMapIndexScan Forward(m_Collision, Near, Far);
	qm_race_start::CTileStartMapIndexScan Reverse(m_Collision, Far, Near);
	ASSERT_TRUE(Forward.IsValid());
	ASSERT_TRUE(Reverse.IsValid());
	EXPECT_EQ(Consume(Forward), (std::vector<int>{17}));
	EXPECT_TRUE(Consume(Reverse).empty());
	EXPECT_TRUE(qm_race_start::IsTileStart(m_Collision, Near, Far));
	EXPECT_FALSE(qm_race_start::IsTileStart(m_Collision, Far, Near));
}

TEST_F(QmRaceStartMapIndexScan, BillionUnitSpanUsesMapBoundedWorkWithoutAllocation)
{
	for(int X = 0; X < 16; ++X)
		m_Map.SetGameTile(X, 1, TILE_FREEZE);
	m_Map.SetFrontTile(15, 1, TILE_START);
	Initialize();
	const vec2 Prev(-1000000000.0f, 48), Pos(1000000000.0f, 48);
	qm_race_start::CTileStartMapIndexScan Scan(m_Collision, Prev, Pos);
	ASSERT_TRUE(Scan.IsValid());
	uint64_t Accepted = 0;
	const uint64_t GroupLimit = MaximumGroups();
	bool Started, Triggered;
	std::size_t Attempts;
	{
		CPredictionAllocationFailure Failure;
		int Index;
		while(Accepted <= GroupLimit && Scan.NextAcceptedIndex(Index))
			++Accepted;
		Started = qm_race_start::IsTileStart(m_Collision, Prev, Pos);
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_GT(Accepted, 1u);
	EXPECT_LE(Accepted, MaximumGroups());
	EXPECT_TRUE(Started);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	ExpectWorkBound(Scan);
}

TEST_F(QmRaceStartMapIndexScan, ShortThresholdKeepsResourceFailureAndTheNextLongLineAllocatesNothing)
{
	for(int X = 0; X < 16; ++X)
		m_Map.SetGameTile(X, 1, TILE_FREEZE);
	m_Map.SetGameTile(8, 1, TILE_START);
	Initialize();
	const vec2 Pos(595, 48);
	bool ShortStarted, ShortTriggered, LongStarted, LongTriggered;
	std::size_t LongAttempts;
	{
		CPredictionAllocationFailure Failure;
		ShortStarted = qm_race_start::IsTileStart(m_Collision, vec2(-3500, 48), Pos);
		ShortTriggered = Failure.Triggered();
	}
	EXPECT_TRUE(ShortTriggered);
	EXPECT_FALSE(ShortStarted);
	EXPECT_TRUE(qm_race_start::IsTileStart(m_Collision, vec2(-3500, 48), Pos));
	{
		CPredictionAllocationFailure Failure;
		LongStarted = qm_race_start::IsTileStart(m_Collision, vec2(-3501, 48), Pos);
		LongTriggered = Failure.Triggered();
		LongAttempts = Failure.Attempts();
	}
	EXPECT_TRUE(LongStarted);
	EXPECT_FALSE(LongTriggered);
	EXPECT_EQ(LongAttempts, 0u);
}

TEST_F(QmRaceStartMapIndexScan, LongGhostSubsegmentsKeepTheirFirstAndLastMatchingTick)
{
	m_Map.SetGameTile(0, 1, TILE_START);
	Initialize();
	const qm_ghost_start::SSample Prev{0, 100, vec2(-10000, 48)};
	const qm_ghost_start::SSample Cur{0, 104, vec2(10000, 48)};
	const auto IsStart = [&](vec2 From, vec2 To) { return qm_race_start::IsTileStart(m_Collision, From, To); };
	EXPECT_EQ(qm_ghost_start::FindStartTick(Prev, Cur, -1, false, IsStart), 101);
	EXPECT_EQ(qm_ghost_start::FindStartTick(Prev, Cur, -1, true, IsStart), 103);
}

TEST_F(QmRaceStartMapIndexScan, InvalidOrUnloadedScanDoesNotProduceAnIndex)
{
	Initialize();
	const float Upper = static_cast<float>(std::numeric_limits<int>::max());
	for(const vec2 Pos : {vec2(Upper, 0), vec2(0, std::numeric_limits<float>::quiet_NaN())})
	{
		qm_race_start::CTileStartMapIndexScan Scan(m_Collision, Pos, Pos);
		EXPECT_FALSE(Scan.IsValid());
		int Index = 77;
		EXPECT_FALSE(Scan.NextAcceptedIndex(Index));
		EXPECT_EQ(Index, 77);
		EXPECT_EQ(Scan.CellGroups(), 0u);
		EXPECT_EQ(Scan.SampleEvaluations(), 0u);
	}
	CCollision Unloaded;
	qm_race_start::CTileStartMapIndexScan Scan(Unloaded, vec2(0, 0), vec2(1, 1));
	EXPECT_FALSE(Scan.IsValid());
	int Index = 77;
	EXPECT_FALSE(Scan.NextAcceptedIndex(Index));
	EXPECT_EQ(Index, 77);
}
