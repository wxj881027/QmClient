#include <generated/protocol.h>

#include <game/client/laser_data.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/door.h>
#include <game/collision.h>
#include <game/layers.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/support/prediction_collision_map.h>
#include <test/support/prediction_world.h>

#include <initializer_list>

namespace
{
	class PredictionDoorRebuild : public ::testing::Test
	{
	protected:
		CPredictionCollisionMap m_Map;
		CLayers m_Layers;
		CCollision m_Collision;
		CPredictionTestWorld m_World;

		void SetUp() override
		{
			m_Layers.Init(&m_Map, false);
			m_Collision.Init(&m_Layers);
			ASSERT_EQ(m_Collision.GetWidth(), 16);
			ASSERT_EQ(m_Collision.GetHeight(), 16);
			m_World.Init(&m_Collision, m_World.TuningList(), nullptr);
			m_World.Switchers().resize(4);
		}

		CDoor *AddDoor(int Id, vec2 Pos, vec2 To, int Number)
		{
			CLaserData Data{};
			Data.m_From = To;
			Data.m_To = Pos;
			Data.m_Type = LASERTYPE_DOOR;
			Data.m_ExtraInfo = true;
			Data.m_Predict = true;
			Data.m_SwitchNumber = Number;
			auto *pDoor = new CDoor(&m_World, Id, &Data);
			m_World.InsertEntity(pDoor);
			return pDoor;
		}

		CDoorTile Tile(int X, int Y = 0)
		{
			CDoorTile Tile{};
			m_Collision.GetDoorTile(Y * 16 + X, &Tile);
			return Tile;
		}

		void ExpectMainList(std::initializer_list<CDoor *> Expected)
		{
			CEntity *pActual = m_World.FindFirst(CGameWorld::ENTTYPE_DOOR);
			CEntity *pPrevious = nullptr;
			for(CDoor *pExpected : Expected)
			{
				ASSERT_EQ(pActual, pExpected);
				EXPECT_EQ(pActual->TypePrev(), pPrevious);
				pPrevious = pActual;
				pActual = pActual->TypeNext();
			}
			EXPECT_EQ(pActual, nullptr);
		}
	};
}

TEST_F(PredictionDoorRebuild, MapIndexOrderControlsOverlapWithoutChangingEntityList)
{
	auto *pEarlier = AddDoor(1, vec2(16, 16), vec2(112, 16), 1);
	auto *pLater = AddDoor(2, vec2(48, 16), vec2(144, 16), 2);
	m_World.ResetDoorCollision();
	EXPECT_EQ(Tile(0).m_Number, 1);
	EXPECT_EQ(Tile(2).m_Number, 2);
	EXPECT_EQ(Tile(4).m_Number, 2);
	EXPECT_EQ(Tile(2).m_Index, TILE_STOPA);
	ExpectMainList({pLater, pEarlier});
}

TEST_F(PredictionDoorRebuild, MapIndexOrderIsIndependentOfInsertionOrder)
{
	auto *pLater = AddDoor(2, vec2(48, 16), vec2(144, 16), 2);
	auto *pEarlier = AddDoor(1, vec2(16, 16), vec2(112, 16), 1);
	m_World.ResetDoorCollision();
	EXPECT_EQ(Tile(2).m_Number, 2);
	ExpectMainList({pEarlier, pLater});
}

TEST_F(PredictionDoorRebuild, EqualSortKeysRetainOriginalListOrder)
{
	auto *pFirst = AddDoor(1, vec2(16, 16), vec2(112, 16), 1);
	auto *pSecond = AddDoor(2, vec2(16, 16), vec2(112, 16), 2);
	m_World.ResetDoorCollision();
	EXPECT_EQ(Tile(1).m_Number, 1);
	ExpectMainList({pSecond, pFirst});
}

TEST_F(PredictionDoorRebuild, ReversedEqualKeysRetainReversedListOrder)
{
	auto *pSecond = AddDoor(2, vec2(16, 16), vec2(112, 16), 2);
	auto *pFirst = AddDoor(1, vec2(16, 16), vec2(112, 16), 1);
	m_World.ResetDoorCollision();
	EXPECT_EQ(Tile(1).m_Number, 2);
	ExpectMainList({pFirst, pSecond});
}

TEST_F(PredictionDoorRebuild, SwitchDoorOverlaysPermanentDoorWithSameMapIndex)
{
	AddDoor(2, vec2(16, 16), vec2(112, 16), 0);
	AddDoor(1, vec2(16, 16), vec2(112, 16), 1);
	m_World.ResetDoorCollision();
	EXPECT_EQ(Tile(1).m_Number, 1);
}

TEST_F(PredictionDoorRebuild, RemovingOldDoorRebuildsRemainingOverlappingCollision)
{
	AddDoor(1, vec2(16, 16), vec2(112, 16), 1);
	auto *pRemaining = AddDoor(2, vec2(48, 16), vec2(144, 16), 2);
	m_World.ResetDoorCollision();
	m_World.NetObjBegin(CTeamsCore{}, 0);
	pRemaining->Keep();
	EXPECT_TRUE(m_World.NetObjEnd());
	EXPECT_EQ(Tile(0).m_Index, 0);
	EXPECT_EQ(Tile(2).m_Number, 2);
	ExpectMainList({pRemaining});
}

TEST_F(PredictionDoorRebuild, RepeatedRebuildsKeepStableOrderAndCoverage)
{
	auto *pFirst = AddDoor(1, vec2(16, 16), vec2(112, 16), 1);
	auto *pSecond = AddDoor(2, vec2(16, 16), vec2(112, 16), 2);
	auto *pThird = AddDoor(3, vec2(48, 16), vec2(144, 16), 3);
	for(int Repeat = 0; Repeat < 3; ++Repeat)
	{
		m_World.ResetDoorCollision();
		EXPECT_EQ(Tile(0).m_Number, 1);
		EXPECT_EQ(Tile(2).m_Number, 3);
		ExpectMainList({pThird, pSecond, pFirst});
	}
}

TEST_F(PredictionDoorRebuild, CopyKeepsGeometryDataWithoutCopyingListIdentity)
{
	auto *pSource = AddDoor(1, vec2(16, 16), vec2(112, 16), 1);
	m_World.ResetDoorCollision();
	{
		CDoor Copy(*pSource);
		EXPECT_TRUE(Copy.Match(pSource));
		EXPECT_EQ(Copy.GetPos(), pSource->GetPos());
		EXPECT_EQ(Copy.TypeNext(), nullptr);
		EXPECT_EQ(Copy.TypePrev(), nullptr);
		EXPECT_EQ(Copy.m_pParent, nullptr);
		EXPECT_EQ(Copy.m_pChild, nullptr);
	}
	EXPECT_EQ(Tile(1).m_Number, 1);
	m_World.ResetDoorCollision();
	ExpectMainList({pSource});
}

TEST_F(PredictionDoorRebuild, PreparedRebuildDoesNotAllocateOrThrow)
{
	AddDoor(1, vec2(16, 16), vec2(112, 16), 1);
	AddDoor(2, vec2(48, 16), vec2(144, 16), 2);
	bool Threw = false, Allocated;
	{
		CPredictionAllocationFailure Failure;
		try
		{
			m_World.ResetDoorCollision();
		}
		catch(...)
		{
			Threw = true;
		}
		Allocated = Failure.Triggered();
	}
	EXPECT_FALSE(Threw);
	EXPECT_FALSE(Allocated);
	EXPECT_EQ(Tile(2).m_Number, 2);
}

TEST_F(PredictionDoorRebuild, SnapshotFinalizationRebuildsWithoutAllocating)
{
	auto *pDoor = AddDoor(1, vec2(16, 16), vec2(112, 16), 1);
	m_World.NetObjBegin(CTeamsCore{}, 0);
	pDoor->Keep();
	bool Threw = false, Allocated, Rebuilt = false;
	{
		CPredictionAllocationFailure Failure;
		try
		{
			Rebuilt = m_World.NetObjEnd();
		}
		catch(...)
		{
			Threw = true;
		}
		Allocated = Failure.Triggered();
	}
	EXPECT_FALSE(Threw);
	EXPECT_FALSE(Allocated);
	EXPECT_TRUE(Rebuilt);
	EXPECT_EQ(Tile(1).m_Number, 1);
}

TEST_F(PredictionDoorRebuild, EmptyAndLargePreparedListsDoNotIntroduceCapacityOrAllocationLimits)
{
	for(const int Count : {0, 1, 1024, 1025})
	{
		SCOPED_TRACE(Count);
		for(int Id = 0; Id < Count; ++Id)
			AddDoor(Id, vec2(16, 16), vec2(17, 16), 1);
		bool Threw = false, Allocated;
		{
			CPredictionAllocationFailure Failure;
			try
			{
				m_World.ResetDoorCollision();
			}
			catch(...)
			{
				Threw = true;
			}
			Allocated = Failure.Triggered();
		}
		EXPECT_FALSE(Threw);
		EXPECT_FALSE(Allocated);
		m_World.Clear();
	}
}

TEST_F(PredictionDoorRebuild, InvalidOriginDoesNotBlockRebuildingValidDoor)
{
	auto *pValid = AddDoor(1, vec2(16, 16), vec2(112, 16), 1);
	auto *pInvalid = AddDoor(2, vec2(2147483648.0f, 0), vec2(0, 0), 2);
	m_World.ResetDoorCollision();
	EXPECT_EQ(Tile(1).m_Number, 1);
	ExpectMainList({pInvalid, pValid});
}

TEST_F(PredictionDoorRebuild, ConfiguredMapLengthRestoresShortenedDoorFootprint)
{
	m_Map.SetGameTile(0, 0, ENTITY_OFFSET + ENTITY_DOOR);
	m_Map.SetGameTile(1, 0, ENTITY_OFFSET + ENTITY_LASER_LONG);
	AddDoor(1, vec2(16, 16), vec2(48, 16), 1);
	m_World.ResetDoorCollision();
	EXPECT_EQ(Tile(7).m_Number, 1);
	EXPECT_EQ(Tile(7).m_Index, TILE_STOPA);
}

TEST_F(PredictionDoorRebuild, SolidTileStopsWritingAndRemovalClearsOnlyDoorFootprint)
{
	m_Map.SetGameTile(2, 0, TILE_SOLID);
	AddDoor(1, vec2(16, 16), vec2(144, 16), 1);
	m_World.ResetDoorCollision();
	ASSERT_EQ(Tile(1).m_Index, TILE_STOPA);
	EXPECT_EQ(Tile(2).m_Index, 0);
	EXPECT_EQ(Tile(3).m_Index, 0);
	m_World.NetObjBegin(CTeamsCore{}, 0);
	EXPECT_TRUE(m_World.NetObjEnd());
	EXPECT_EQ(Tile(1).m_Index, 0);
	EXPECT_EQ(m_Collision.GetTile(64, 16), TILE_SOLID);
}

TEST_F(PredictionDoorRebuild, SnapshotEntityAllocationFailureClearsPublishedCollisionAndAllowsRetry)
{
	AddDoor(1, vec2(16, 16), vec2(112, 16), 1);
	m_World.ResetDoorCollision();
	ASSERT_EQ(Tile(1).m_Index, TILE_STOPA);

	m_World.m_WorldConfig.m_PredictTiles = true;
	CNetObj_DDNetLaser Door{};
	Door.m_FromX = 112;
	Door.m_FromY = Door.m_ToX = Door.m_ToY = 16;
	Door.m_Type = LASERTYPE_DOOR;
	Door.m_Owner = -1;
	Door.m_SwitchNumber = 1;
	m_World.NetObjBegin(CTeamsCore{}, 0);
	m_World.NetObjAdd(1, NETOBJTYPE_DDNETLASER, &Door, nullptr);

	bool Triggered;
	std::size_t Attempts;
	bool Rebuilt;
	{
		CPredictionAllocationFailure Failure;
		m_World.NetObjAdd(2, NETOBJTYPE_DDNETLASER, &Door, nullptr);
		m_World.NetObjAdd(3, NETOBJTYPE_DDNETLASER, &Door, nullptr);
		Rebuilt = m_World.NetObjEnd();
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_EQ(Attempts, 1u);
	EXPECT_FALSE(Rebuilt);
	EXPECT_EQ(m_World.FindFirst(CGameWorld::ENTTYPE_DOOR), nullptr);
	EXPECT_EQ(Tile(1).m_Index, 0);

	m_World.NetObjBegin(CTeamsCore{}, 0);
	m_World.NetObjAdd(2, NETOBJTYPE_DDNETLASER, &Door, nullptr);
	ASSERT_TRUE(m_World.NetObjEnd());
	EXPECT_NE(m_World.GetEntity(2, CGameWorld::ENTTYPE_DOOR), nullptr);
	EXPECT_EQ(Tile(1).m_Index, TILE_STOPA);
}

TEST_F(PredictionDoorRebuild, CharacterTilePredictionIgnoresDoorMissingFromWorldSwitchTable)
{
	m_World.m_WorldConfig.m_PredictDDRace = true;
	CNetObj_Character Character{};
	Character.m_X = Character.m_Y = 16;
	Character.m_HookedPlayer = -1;
	Character.m_Weapon = WEAPON_HAMMER;
	m_World.NetObjBegin(CTeamsCore{}, 0);
	m_World.NetCharAdd(0, &Character, nullptr, 0, true);
	ASSERT_TRUE(m_World.NetObjEnd());
	auto *pChar = m_World.GetCharacterById(0);
	ASSERT_NE(pChar, nullptr);
	AddDoor(1, vec2(48, 16), vec2(112, 16), 3);
	m_World.ResetDoorCollision();
	ASSERT_EQ(Tile(1).m_Index, TILE_STOPA);
	m_World.Switchers()[3].m_aStatus[TEAM_FLOCK] = true;

	// 核心远离门，测的是角色通过 m_Pos 查询图块移动限制的路径。
	CCharacterCore Core = pChar->GetCore();
	Core.m_Pos = vec2(384, 384);
	pChar->SetCore(Core);
	pChar->Tick();
	pChar->SetVelocity(vec2(2, 0));
	EXPECT_EQ(pChar->Core()->m_Vel.x, 0.0f);

	m_World.Switchers().resize(2);
	pChar->m_PrevPos = pChar->m_Pos;
	pChar->Tick();
	pChar->SetVelocity(vec2(2, 0));
	EXPECT_EQ(pChar->Core()->m_Vel.x, 2.0f);
}

TEST_F(PredictionDoorRebuild, SnapshotCharacterAllocationFailureClearsBothCharacterIndexes)
{
	CNetObj_Character Character{};
	Character.m_HookedPlayer = -1;
	Character.m_Weapon = WEAPON_HAMMER;
	CTeamsCore Teams;
	Teams.Team(0, TEAM_FLOCK);
	Teams.Team(1, TEAM_FLOCK);
	m_World.NetObjBegin(Teams, 0);
	m_World.NetCharAdd(0, &Character, nullptr, 0, true);
	ASSERT_TRUE(m_World.NetObjEnd());
	ASSERT_NE(m_World.GetCharacterById(0), nullptr);
	ASSERT_NE(m_World.m_Core.m_apCharacters[0], nullptr);

	m_World.NetObjBegin(Teams, 0);
	m_World.NetCharAdd(0, &Character, nullptr, 0, true);
	bool Triggered;
	{
		CPredictionAllocationFailure Failure;
		m_World.NetCharAdd(1, &Character, nullptr, 0, false);
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(m_World.NetObjEnd());
	EXPECT_EQ(m_World.GetCharacterById(0), nullptr);
	EXPECT_EQ(m_World.m_Core.m_apCharacters[0], nullptr);
	EXPECT_EQ(m_World.GetCharacterById(1), nullptr);
	EXPECT_EQ(m_World.m_Core.m_apCharacters[1], nullptr);
}
