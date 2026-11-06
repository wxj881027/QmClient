#include <game/client/components/qmclient/weapon_trajectory_geometry.h>
#include <game/layers.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/support/prediction_collision_map.h>

#include <array>
#include <limits>

namespace
{
	// 仅提供解码后地图存储；传送索引、移动和碰撞全部使用生产实现。
	class CTrajectoryMap : public CPredictionCollisionMap
	{
		CMapItemGroup m_Group;
		CMapItemLayerTilemap m_Tele;
		std::array<CTeleTile, 16 * 16> m_aTeleTiles{};

	public:
		CTrajectoryMap()
		{
			m_Group = *static_cast<CMapItemGroup *>(CPredictionCollisionMap::GetItem(0));
			m_Group.m_NumLayers = 3;
			m_Tele = *static_cast<CMapItemLayerTilemap *>(CPredictionCollisionMap::GetItem(1));
			m_Tele.m_Flags = TILESLAYERFLAG_TELE;
			m_Tele.m_Data = -1;
			m_Tele.m_Tele = 2;
		}
		void SetTeleTile(int X, int Y, int Type)
		{
			m_aTeleTiles[Y * 16 + X].m_Type = static_cast<unsigned char>(Type);
			m_aTeleTiles[Y * 16 + X].m_Number = 1;
		}
		int GetDataSize(int Index) const override { return Index == 2 ? static_cast<int>(sizeof(m_aTeleTiles)) : CPredictionCollisionMap::GetDataSize(Index); }
		void *GetData(int Index) override { return Index == 2 ? m_aTeleTiles.data() : CPredictionCollisionMap::GetData(Index); }
		int NumData() const override { return 3; }
		int GetItemSize(int Index) override { return Index == 3 ? static_cast<int>(sizeof(m_Tele)) : CPredictionCollisionMap::GetItemSize(Index); }
		void *GetItem(int Index, int *pType = nullptr, int *pId = nullptr) override
		{
			if(Index == 0 || Index == 3)
			{
				if(pType)
					*pType = Index == 0 ? MAPITEMTYPE_GROUP : MAPITEMTYPE_LAYER;
				if(pId)
					*pId = Index == 0 ? 0 : 2;
				if(Index == 0)
					return &m_Group;
				return &m_Tele;
			}
			return CPredictionCollisionMap::GetItem(Index, pType, pId);
		}
		void GetType(int Type, int *pStart, int *pNum) override
		{
			CPredictionCollisionMap::GetType(Type, pStart, pNum);
			if(Type == MAPITEMTYPE_LAYER)
				*pNum = 3;
		}
		int FindItemIndex(int Type, int Id) override { return Type == MAPITEMTYPE_LAYER && Id == 2 ? 3 : CPredictionCollisionMap::FindItemIndex(Type, Id); }
		int NumItems() const override { return 4; }
	};

	class WeaponTrajectoryPreparation : public ::testing::Test
	{
	protected:
		CTrajectoryMap m_Map;
		CLayers m_Layers;
		CCollision m_Collision;

		void Initialize()
		{
			m_Layers.Init(&m_Map, false);
			m_Collision.Init(&m_Layers);
			ASSERT_EQ(m_Collision.GetWidth(), 16);
			ASSERT_NE(m_Layers.TeleLayer(), nullptr);
		}
	};
}

TEST_F(WeaponTrajectoryPreparation, NinjaUsesRealBoxMovementAndNormalizedDirection)
{
	Initialize();
	vec2 Out(9, 9);
	ASSERT_TRUE(QmTryPredictNinjaEndPosition(m_Collision, vec2(200, 200), vec2(2, 0), 2, 50, vec2(0, 0), Out));
	EXPECT_NEAR(Out.x, 300, 0.01f);
	EXPECT_NEAR(Out.y, 200, 0.01f);
}

TEST_F(WeaponTrajectoryPreparation, NinjaStopsAtTheRealWallInsteadOfCrossingIt)
{
	for(int Y = 0; Y < 16; ++Y)
		m_Map.SetGameTile(10, Y, TILE_SOLID);
	Initialize();
	vec2 Out(9, 9);
	ASSERT_TRUE(QmTryPredictNinjaEndPosition(m_Collision, vec2(280, 200), vec2(1, 0), 2, 50, vec2(0, 0), Out));
	EXPECT_GT(Out.x, 280);
	EXPECT_LT(Out.x, 306.1f);
	EXPECT_FALSE(m_Collision.TestBox(Out, CCharacterCore::PhysicalSizeVec2()));
}

TEST_F(WeaponTrajectoryPreparation, ZeroMovementTicksPublishTheOriginalPosition)
{
	Initialize();
	vec2 Out(9, 9);
	ASSERT_TRUE(QmTryPredictNinjaEndPosition(m_Collision, vec2(200, 200), vec2(1, 0), 0, 50, vec2(0, 0), Out));
	EXPECT_EQ(Out, vec2(200, 200));
}

TEST_F(WeaponTrajectoryPreparation, InvalidNumericInputsLeaveOutputUnchangedAndAllowRetry)
{
	Initialize();
	vec2 Out(9, 9);
	const float Inf = std::numeric_limits<float>::infinity();
	EXPECT_FALSE(QmTryPredictNinjaEndPosition(m_Collision, vec2(static_cast<float>(std::numeric_limits<int>::max()), 0), vec2(1, 0), 1, 50, vec2(0, 0), Out));
	EXPECT_EQ(Out, vec2(9, 9));
	EXPECT_FALSE(QmTryPredictNinjaEndPosition(m_Collision, vec2(200, 200), vec2(Inf, 0), 1, 50, vec2(0, 0), Out));
	EXPECT_EQ(Out, vec2(9, 9));
	EXPECT_FALSE(QmTryPredictNinjaEndPosition(m_Collision, vec2(200, 200), vec2(1, 0), 1, Inf, vec2(0, 0), Out));
	EXPECT_EQ(Out, vec2(9, 9));
	ASSERT_TRUE(QmTryPredictNinjaEndPosition(m_Collision, vec2(200, 200), vec2(1, 0), 1, 50, vec2(0, 0), Out));
	EXPECT_NEAR(Out.x, 250, 0.01f);
}

TEST_F(WeaponTrajectoryPreparation, NinjaKeepsTheFirstTeleporterExitPolicy)
{
	m_Map.SetTeleTile(4, 4, TILE_TELEIN);
	m_Map.SetTeleTile(6, 6, TILE_TELEOUT);
	m_Map.SetTeleTile(8, 8, TILE_TELEOUT);
	Initialize();
	vec2 Out(9, 9);
	ASSERT_TRUE(QmTryPredictNinjaEndPosition(m_Collision, vec2(94, 144), vec2(1, 0), 1, 50, vec2(0, 0), Out));
	EXPECT_EQ(Out, vec2(208, 208));
}

TEST_F(WeaponTrajectoryPreparation, MissingExitAllocationFailureDoesNotPublishTheMovedPosition)
{
	m_Map.SetTeleTile(4, 4, TILE_TELEIN);
	Initialize();
	vec2 Out(9, 9);
	bool Prepared, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Prepared = QmTryPredictNinjaEndPosition(m_Collision, vec2(94, 144), vec2(1, 0), 1, 50, vec2(0, 0), Out);
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Prepared);
	EXPECT_EQ(Out, vec2(9, 9));
	ASSERT_TRUE(QmTryPredictNinjaEndPosition(m_Collision, vec2(94, 144), vec2(1, 0), 1, 50, vec2(0, 0), Out));
	EXPECT_NEAR(Out.x, 144, 0.01f);
	EXPECT_NEAR(Out.y, 144, 0.01f);
}

TEST_F(WeaponTrajectoryPreparation, GeometryAllocationFailuresClearPrefixesAndAllowRealMovementRetry)
{
	Initialize();
	int Failures = 0;
	bool ReachedSuccess = false;
	for(int Allocation = 0; Allocation < 8; ++Allocation)
	{
		SQmWeaponTrajectoryScratch Scratch;
		auto Prepare = [&] {
			vec2 End;
			if(!QmTryPredictNinjaEndPosition(m_Collision, vec2(200, 200), vec2(1, 0), 1, 50, vec2(0, 0), End))
				return false;
			Scratch.m_vPoints.push_back(End);
			Scratch.m_vLineSegments.emplace_back(vec2(200, 200), End);
			return Scratch.PrepareQuads(2);
		};
		bool Prepared, Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			Prepared = Scratch.TryPrepare(Prepare);
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_TRUE(Prepared);
			ReachedSuccess = Prepared;
			break;
		}
		++Failures;
		EXPECT_FALSE(Prepared);
		EXPECT_TRUE(Scratch.m_vPoints.empty());
		EXPECT_TRUE(Scratch.m_vLineSegments.empty());
		EXPECT_TRUE(Scratch.m_vLineQuadSegments.empty());
		ASSERT_TRUE(Scratch.TryPrepare(Prepare));
		ASSERT_EQ(Scratch.m_vPoints.size(), 1u);
		EXPECT_NEAR(Scratch.m_vPoints.front().x, 250, 0.01f);
		EXPECT_EQ(Scratch.m_vLineQuadSegments.size(), 1u);
	}
	EXPECT_GE(Failures, 3);
	EXPECT_TRUE(ReachedSuccess);
}
