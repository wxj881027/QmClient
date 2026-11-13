#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/laser_data.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/dragger.h>
#include <game/collision.h>

#include <gtest/gtest.h>
#include <test/support/prediction_world.h>

#include <cmath>
#include <limits>

namespace
{
	struct CRangeScope
	{
		int m_Previous = g_Config.m_SvDraggerRange;
		CRangeScope() { g_Config.m_SvDraggerRange = 512; }
		~CRangeScope() { g_Config.m_SvDraggerRange = m_Previous; }
	};

	struct CScenario
	{
		CRangeScope m_Range;
		CCollision m_Collision;
		CPredictionTestWorld m_World;
		CCharacter *m_pCharacter;

		CScenario()
		{
			m_World.Init(&m_Collision, m_World.TuningList(), nullptr);
			CNetObj_Character Input{};
			Input.m_X = std::numeric_limits<int>::max();
			Input.m_HookedPlayer = -1;
			Input.m_Weapon = WEAPON_HAMMER;
			Input.m_Emote = EMOTE_NORMAL;
			m_pCharacter = new CCharacter(&m_World, 0, &Input);
			m_World.InsertEntity(m_pCharacter);
		}

		CLaserData Data(int Subtype = 0)
		{
			CLaserData Data{};
			Data.m_From = vec2(std::nextafter(2147483648.0f, 0.0f), 0);
			Data.m_To = Data.m_From;
			Data.m_Owner = 0;
			Data.m_Type = LASERTYPE_DRAGGER;
			Data.m_ExtraInfo = true;
			Data.m_Predict = true;
			Data.m_Subtype = Subtype;
			return Data;
		}
	};
}

TEST(PredictionDraggerGeometry, ExistingTargetWithUnrepresentablePositionIsNotAccelerated)
{
	for(const int Subtype : {0, 1})
	{
		SCOPED_TRACE(Subtype);
		CScenario S;
		auto Data = S.Data(Subtype);
		CDragger Dragger(&S.m_World, 7, &Data);
		Dragger.Tick();
		EXPECT_EQ(S.m_pCharacter->Core()->m_Vel, vec2(0, 0));
		EXPECT_EQ(Dragger.GetPos(), Data.m_From);
	}
}

TEST(PredictionDraggerGeometry, PeriodicCandidateScanRejectsUnrepresentableTarget)
{
	CScenario S;
	S.m_World.m_GameTick = 98;
	auto Data = S.Data();
	Data.m_Owner = -1;
	CDragger Dragger(&S.m_World, 7, &Data);
	Dragger.Tick();
	EXPECT_EQ(S.m_pCharacter->Core()->m_Vel, vec2(0, 0));
}

TEST(PredictionDraggerGeometry, NonfiniteOriginDoesNotPropagateNonfiniteForce)
{
	CScenario S;
	auto Data = S.Data(1);
	Data.m_From.x = std::numeric_limits<float>::quiet_NaN();
	CDragger Dragger(&S.m_World, 7, &Data);
	Dragger.Tick();
	EXPECT_EQ(S.m_pCharacter->Core()->m_Vel, vec2(0, 0));
}

TEST(PredictionDraggerGeometry, ValidIgnoreWallsBeamKeepsNormalAttraction)
{
	CScenario S;
	S.m_pCharacter->m_Pos = vec2(100, 0);
	S.m_pCharacter->Core()->m_Pos = S.m_pCharacter->m_Pos;
	auto Data = S.Data(1);
	Data.m_From = vec2(0, 0);
	CDragger Dragger(&S.m_World, 7, &Data);
	Dragger.Tick();
	EXPECT_EQ(S.m_pCharacter->Core()->m_Vel, vec2(-1, 0));
}

TEST(PredictionDraggerGeometry, ResetBeamWaitsForNextPeriodicScanBeforeRecovery)
{
	CScenario S;
	auto Data = S.Data(1);
	CDragger Dragger(&S.m_World, 7, &Data);
	Dragger.Tick();
	S.m_pCharacter->m_Pos = Data.m_From - vec2(128, 0);
	S.m_pCharacter->Core()->m_Pos = S.m_pCharacter->m_Pos;
	Dragger.Tick();
	EXPECT_EQ(S.m_pCharacter->Core()->m_Vel, vec2(0, 0));
	S.m_World.m_GameTick = 105;
	Dragger.Tick();
	EXPECT_EQ(S.m_pCharacter->Core()->m_Vel, vec2(1, 0));
}
