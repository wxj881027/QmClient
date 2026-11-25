#include <game/client/components/qmclient/dummy_distance_link_hook.h>
#include <game/layers.h>

#include <gtest/gtest.h>
#include <test/support/prediction_collision_map.h>

#include <array>

namespace
{
	using namespace qm_dummy_link;

	class CHookLinkMap : public CPredictionCollisionMap
	{
		std::array<CTeleTile, 16 * 16> m_aTeleTiles{};

	public:
		CHookLinkMap()
		{
			// 测试地图复用碰撞 fixture 的第二层，换成钩索传送层。
			auto *pLayer = static_cast<CMapItemLayerTilemap *>(GetItem(2));
			pLayer->m_Flags = TILESLAYERFLAG_TELE;
			pLayer->m_Tele = 1;
		}

		int GetDataSize(int Index) const override { return Index == 1 ? static_cast<int>(sizeof(m_aTeleTiles)) : CPredictionCollisionMap::GetDataSize(Index); }
		void *GetData(int Index) override { return Index == 1 ? m_aTeleTiles.data() : CPredictionCollisionMap::GetData(Index); }

		void SetTele(int X, int Y, int Type)
		{
			m_aTeleTiles[Y * 16 + X].m_Number = 1;
			m_aTeleTiles[Y * 16 + X].m_Type = static_cast<unsigned char>(Type);
		}
	};

	class QmDummyDistanceLinkHook : public ::testing::Test
	{
	protected:
		CHookLinkMap m_Map;
		CLayers m_Layers;
		CCollision m_Collision;
		const SEndpoint m_Main{vec2(48, 144), false};
		const SEndpoint m_Dummy{vec2(240, 144), false};
		CTuningParams m_Tuning;

		void VerticalWall(int Tile, int Flags = 0)
		{
			auto *pTiles = static_cast<CTile *>(m_Map.GetData(0));
			for(int Y = 0; Y < 16; ++Y)
			{
				m_Map.SetGameTile(5, Y, Tile);
				pTiles[Y * 16 + 5].m_Flags = static_cast<unsigned char>(Flags);
			}
		}

		void Initialize()
		{
			m_Layers.Init(&m_Map, false);
			m_Collision.Init(&m_Layers);
			ASSERT_NE(m_Collision.GameLayer(), nullptr);
		}

		EHookReach Reach(const SEndpoint &Source, const SEndpoint &Target, bool SameTeam = true)
		{
			return HookReach(m_Collision, Source, Target, m_Tuning, SameTeam);
		}
	};
}

TEST_F(QmDummyDistanceLinkHook, WallBlockingBothDirectionsHidesLink)
{
	for(const int Tile : {TILE_SOLID, TILE_NOHOOK})
	{
		SCOPED_TRACE(Tile);
		VerticalWall(Tile);
		Initialize();
		auto Link = MakeLink(m_Main, m_Dummy);
		ASSERT_TRUE(Link);
		EXPECT_FALSE(ApplyHookReach(*Link, Reach(m_Main, m_Dummy), Reach(m_Dummy, m_Main)));
	}
}

TEST_F(QmDummyDistanceLinkHook, OneWayHookTileOnlyHighlightsFromTheAllowedSide)
{
	VerticalWall(TILE_THROUGH_DIR, ROTATION_90);
	Initialize();

	auto Link = MakeLink(m_Main, m_Dummy);
	ASSERT_TRUE(Link);
	const auto MainReach = Reach(m_Main, m_Dummy);
	const auto DummyReach = Reach(m_Dummy, m_Main);
	ASSERT_EQ(MainReach, EHookReach::IN_RANGE);
	ASSERT_EQ(DummyReach, EHookReach::BLOCKED);
	EXPECT_TRUE(ApplyHookReach(*Link, MainReach, DummyReach));
	EXPECT_TRUE(Link->m_InHookRange);
	EXPECT_TRUE(ApplyHookReach(*Link, DummyReach, MainReach));
	EXPECT_FALSE(Link->m_InHookRange);
}

TEST_F(QmDummyDistanceLinkHook, OpenAimAtTargetEdgeCanStillHook)
{
	m_Map.SetGameTile(5, 4, TILE_NOHOOK);
	Initialize();

	const SEndpoint Main{vec2(48, 131), false};
	const SEndpoint Dummy{vec2(240, 131), false};
	EXPECT_EQ(Reach(Main, Dummy), EHookReach::IN_RANGE);
}

TEST_F(QmDummyDistanceLinkHook, CanReachTargetThroughOneOfSeveralHookTeleportExits)
{
	VerticalWall(TILE_NOHOOK);
	m_Map.SetTele(3, 4, g_Config.m_SvOldTeleportHook ? TILE_TELEIN : TILE_TELEINHOOK);
	m_Map.SetTele(7, 7, TILE_TELEOUT);
	m_Map.SetTele(8, 4, TILE_TELEOUT);
	Initialize();
	ASSERT_TRUE(m_Collision.HasHookTeleIns());

	const SEndpoint Target{vec2(368, 144), false};
	EXPECT_EQ(Reach(m_Main, Target), EHookReach::IN_RANGE);
}

TEST_F(QmDummyDistanceLinkHook, BeyondHookRangeKeepsVisibleLinkNormal)
{
	Initialize();

	const SEndpoint Far{vec2(500, 144), false};
	auto Link = MakeLink(m_Main, Far);
	ASSERT_TRUE(Link);
	EXPECT_TRUE(ApplyHookReach(*Link, Reach(m_Main, Far), Reach(Far, m_Main)));
	EXPECT_FALSE(Link->m_InHookRange);
}

TEST_F(QmDummyDistanceLinkHook, UsesRenderedSourceAndSnapshotTargetForHookDistance)
{
	Initialize();

	const SEndpoint Dummy{vec2(463, 144), false, vec2(455, 144)};
	EXPECT_EQ(Reach(m_Main, Dummy), EHookReach::IN_RANGE);
	EXPECT_EQ(Reach(Dummy, m_Main), EHookReach::POSSIBLE);
}

TEST_F(QmDummyDistanceLinkHook, DisabledPlayerHookOnlyBlocksTheTeeFiringIt)
{
	Initialize();

	auto Main = m_Main;
	Main.m_HookHitDisabled = true;
	EXPECT_EQ(Reach(Main, m_Dummy), EHookReach::BLOCKED);
	EXPECT_EQ(Reach(m_Dummy, Main), EHookReach::IN_RANGE);
}

TEST_F(QmDummyDistanceLinkHook, SoloOrDifferentTeamsBlockHookingBothWays)
{
	Initialize();

	EXPECT_EQ(Reach(m_Main, m_Dummy, false), EHookReach::BLOCKED);
	EXPECT_EQ(Reach(m_Dummy, m_Main, false), EHookReach::BLOCKED);
	auto Solo = m_Dummy;
	Solo.m_Solo = true;
	EXPECT_EQ(Reach(m_Main, Solo), EHookReach::BLOCKED);
	EXPECT_EQ(Reach(Solo, m_Main), EHookReach::BLOCKED);
}
