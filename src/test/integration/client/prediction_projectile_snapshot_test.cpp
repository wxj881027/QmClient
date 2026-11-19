#include <generated/protocol.h>

#include <game/client/prediction/entities/projectile.h>
#include <game/client/projectile_data.h>

#include <gtest/gtest.h>
#include <test/support/prediction_world.h>

TEST(PredictionProjectileSnapshot, WorldCopyKeepsPublishedProjectileAfterSourceIsCleared)
{
	CPredictionTestWorld Source, Predicted;
	Source.m_WorldConfig.m_IsDDRace = true;
	Source.m_WorldConfig.m_PredictWeapons = true;

	const int Id = 7;
	CNetObj_DDNetProjectile Snapshot{};
	Snapshot.m_X = 9600;
	Snapshot.m_Y = 6400;
	Snapshot.m_VelX = 1000000;
	Snapshot.m_Type = WEAPON_SHOTGUN;
	Snapshot.m_StartTick = Source.GameTick() - 2;
	Snapshot.m_Owner = -1;
	Snapshot.m_SwitchNumber = 2;
	Snapshot.m_Flags = PROJECTILEFLAG_BOUNCE_HORIZONTAL | PROJECTILEFLAG_FREEZE;
	Source.NetObjBegin(CTeamsCore{}, Source.m_LocalClientId);
	Source.NetObjAdd(Id, NETOBJTYPE_DDNETPROJECTILE, &Snapshot, nullptr);
	Source.NetObjEnd();
	ASSERT_TRUE(Predicted.CopyWorld(&Source));
	Source.Clear();

	const auto *pProjectile = static_cast<CProjectile *>(Predicted.GetEntity(Id, CGameWorld::ENTTYPE_PROJECTILE));
	ASSERT_NE(pProjectile, nullptr);
	const CProjectileData Data = pProjectile->GetData();
	EXPECT_EQ(Data.m_StartPos, vec2(96, 64));
	EXPECT_EQ(Data.m_StartVel, vec2(1, 0));
	EXPECT_EQ(Data.m_Type, WEAPON_SHOTGUN);
	EXPECT_EQ(Data.m_StartTick, Snapshot.m_StartTick);
	EXPECT_EQ(Data.m_Owner, -1);
	EXPECT_EQ(Data.m_SwitchNumber, 2);
	EXPECT_TRUE(Data.m_Freeze);
	EXPECT_EQ(Data.m_Bouncing, 1);
