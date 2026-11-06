#include <generated/protocol.h>

#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/projectile.h>

#include <gtest/gtest.h>
#include <test/support/prediction_world.h>

#include <limits>
#include <utility>

namespace
{
	struct CScenario
	{
		CPredictionTestWorld m_World;
		CNetObj_Character m_Input{};
		CCharacter *m_pCharacter;

		explicit CScenario(int Tick = 100, int Weapon = WEAPON_HAMMER)
		{
			m_World.m_GameTick = Tick;
			m_World.m_WorldConfig.m_PredictWeapons = true;
			m_World.m_WorldConfig.m_InfiniteAmmo = true;
			m_World.TuningList()->m_JetpackStrength = 0.0f;
			m_Input.m_HookedPlayer = -1;
			m_Input.m_Weapon = Weapon;
			m_Input.m_Emote = EMOTE_NORMAL;
			m_pCharacter = new CCharacter(&m_World, 0, &m_Input);
			m_World.InsertEntity(m_pCharacter);
		}

		void ArmGun()
		{
			CNetObj_PlayerInput Input{};
			Input.m_TargetX = 1;
			m_pCharacter->OnDirectInput(&Input);
			m_pCharacter->OnDirectInput(&Input);
			Input.m_Fire = 1;
			m_pCharacter->SetInput(&Input);
		}

		void ReadFreezeEnd(int Tick, int EndTick)
		{
			m_World.m_GameTick = Tick;
			m_World.m_WorldConfig.m_PredictFreeze = true;
			// 进入已有冻结状态，避免依赖其它测试留下的 sv_freeze_delay 全局配置。
			m_pCharacter->m_FreezeTime = 1;
			CNetObj_DDNetCharacter Extended{};
			Extended.m_Flags = CHARACTERFLAG_WEAPON_HAMMER;
			Extended.m_FreezeEnd = EndTick;
			m_pCharacter->Read(&m_Input, &Extended, false);
		}
	};
}

TEST(PredictionCharacterTiming, OldNinjaActivationAtOppositeTickExtremeExpires)
{
	CScenario S;
	S.m_pCharacter->GiveNinja();
	S.m_pCharacter->SetNinjaActivationTick(std::numeric_limits<int>::min());
	S.m_pCharacter->SetNinjaCurrentMoveTime(0);
	S.m_World.m_GameTick = std::numeric_limits<int>::max();
	S.m_pCharacter->HandleNinja();
	EXPECT_FALSE(S.m_pCharacter->GetWeaponGot(WEAPON_NINJA));
	EXPECT_EQ(S.m_pCharacter->GetActiveWeapon(), WEAPON_HAMMER);
}

TEST(PredictionCharacterTiming, NinjaDurationKeepsStrictFifteenSecondBoundary)
{
	// 生产武器数据为15秒，固定50tick/s；零移动阶段不访问地图。
	for(const auto &[Activation, Expired] : {std::pair{250, false}, {249, true}})
	{
		SCOPED_TRACE(Activation);
		CScenario S(1000);
		S.m_pCharacter->GiveNinja();
		S.m_pCharacter->SetNinjaActivationTick(Activation);
		S.m_pCharacter->SetNinjaCurrentMoveTime(0);
		S.m_pCharacter->HandleNinja();
		EXPECT_EQ(S.m_pCharacter->GetWeaponGot(WEAPON_NINJA), !Expired);
	}
}

TEST(PredictionCharacterTiming, FutureNinjaActivationDoesNotExpireEarly)
{
	CScenario S;
	S.m_pCharacter->GiveNinja();
	S.m_pCharacter->SetNinjaActivationTick(std::numeric_limits<int>::max());
	S.m_pCharacter->SetNinjaCurrentMoveTime(0);
	S.m_pCharacter->HandleNinja();
	EXPECT_TRUE(S.m_pCharacter->GetWeaponGot(WEAPON_NINJA));
}

TEST(PredictionCharacterTiming, WideFutureReloadKeepsGunBlocked)
{
	CScenario S(0, WEAPON_GUN);
	S.m_World.TuningList()->m_GunFireDelay = 2000.0f;
	S.m_World.m_GameTick = 60;
	S.m_Input.m_AttackTick = std::numeric_limits<int>::max();
	S.m_pCharacter->Read(&S.m_Input, nullptr, true);
	S.ArmGun();
	S.m_pCharacter->FireWeapon();
	S.m_pCharacter->HandleWeapons();
	S.m_pCharacter->FireWeapon();
	EXPECT_EQ(S.m_World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE), nullptr);
	EXPECT_EQ(S.m_pCharacter->GetAttackTick(), std::numeric_limits<int>::max());
}

TEST(PredictionCharacterTiming, OrdinaryReloadWaitsUntilTheLastRemainingStep)
{
	CScenario S(0, WEAPON_GUN);
	S.m_World.TuningList()->m_GunFireDelay = 200.0f;
	S.m_World.m_GameTick = 60;
	S.m_Input.m_AttackTick = 55;
	S.m_pCharacter->Read(&S.m_Input, nullptr, true);
	S.ArmGun();
	for(int Step = 0; Step < 4; ++Step)
		S.m_pCharacter->HandleWeapons();
	S.m_pCharacter->FireWeapon();
	ASSERT_EQ(S.m_World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE), nullptr);
	S.m_pCharacter->HandleWeapons();
	S.m_pCharacter->FireWeapon();
	EXPECT_NE(S.m_World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE), nullptr);
	EXPECT_EQ(S.m_pCharacter->GetAttackTick(), 60);
}

TEST(PredictionCharacterTiming, FreshWeaponHistoryAtUpperTickDoesNotReconstructReload)
{
	CScenario S(std::numeric_limits<int>::max() - 10, WEAPON_GUN);
	S.m_World.TuningList()->m_GunFireDelay = 200.0f;
	S.m_World.m_GameTick = std::numeric_limits<int>::max();
	S.m_Input.m_AttackTick = std::numeric_limits<int>::max();
	S.m_pCharacter->Read(&S.m_Input, nullptr, true);
	S.ArmGun();
	S.m_pCharacter->FireWeapon();
	EXPECT_NE(S.m_World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE), nullptr);
}

TEST(PredictionCharacterTiming, ReloadReconstructionKeepsStrictHistoryThreshold)
{
	for(const auto &[Tick, CanFire] : {std::pair{50, true}, {51, false}})
	{
		SCOPED_TRACE(Tick);
		CScenario S(0, WEAPON_GUN);
		S.m_World.TuningList()->m_GunFireDelay = 200.0f;
		S.m_World.m_GameTick = Tick;
		S.m_Input.m_AttackTick = 100;
		S.m_pCharacter->Read(&S.m_Input, nullptr, true);
		S.ArmGun();
		S.m_pCharacter->FireWeapon();
		EXPECT_EQ(S.m_World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE) != nullptr, CanFire);
	}
}

TEST(PredictionCharacterTiming, WorldCopiesKeepWideReloadBlocking)
{
	CScenario S(0, WEAPON_GUN);
	S.m_World.TuningList()->m_GunFireDelay = 2000.0f;
	S.m_World.m_GameTick = 60;
	S.m_Input.m_AttackTick = std::numeric_limits<int>::max();
	S.m_pCharacter->Read(&S.m_Input, nullptr, true);
	S.ArmGun();
	CPredictionTestWorld Linked, Clean;
	ASSERT_TRUE(Linked.CopyWorld(&S.m_World));
	ASSERT_TRUE(Clean.CopyWorldClean(&S.m_World));
	for(CGameWorld *pWorld : {&Linked, &Clean})
	{
		auto *pCharacter = pWorld->GetCharacterById(0);
		ASSERT_NE(pCharacter, nullptr);
		pCharacter->FireWeapon();
		EXPECT_EQ(pWorld->FindFirst(CGameWorld::ENTTYPE_PROJECTILE), nullptr);
		EXPECT_EQ(pCharacter->GetAttackTick(), std::numeric_limits<int>::max());
	}
}

TEST(PredictionCharacterTiming, FreezePublishesNormalDurationAndDeadlineTogether)
{
	CScenario S;
	S.m_World.m_WorldConfig.m_PredictFreeze = true;
	ASSERT_TRUE(S.m_pCharacter->Freeze(2));
	EXPECT_EQ(S.m_pCharacter->m_FreezeTime, 100);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeStart, 100);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeEnd, 200);
}

TEST(PredictionCharacterTiming, FreezeKeepsStrictCooldownThreshold)
{
	for(const auto &[Tick, Accepted] : {std::pair{50, false}, {51, true}})
	{
		CScenario S(Tick);
		S.m_World.m_WorldConfig.m_PredictFreeze = true;
		EXPECT_EQ(S.m_pCharacter->Freeze(1), Accepted);
		EXPECT_EQ(S.m_pCharacter->m_FreezeTime, Accepted ? 50 : 0);
	}
}

TEST(PredictionCharacterTiming, MinimumGameTickDoesNotWrapFreezeCooldownIntoTheFuture)
{
	CScenario S(std::numeric_limits<int>::min());
	S.m_World.m_WorldConfig.m_PredictFreeze = true;
	EXPECT_FALSE(S.m_pCharacter->Freeze(1));
	EXPECT_EQ(S.m_pCharacter->m_FreezeTime, 0);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeStart, 0);
}

TEST(PredictionCharacterTiming, UnrepresentableFreezeDeadlineKeepsExistingState)
{
	CScenario S(std::numeric_limits<int>::max() - 49);
	S.m_World.m_WorldConfig.m_PredictFreeze = true;
	S.m_pCharacter->m_FreezeTime = 10;
	S.m_pCharacter->Core()->m_FreezeEnd = 123;
	EXPECT_FALSE(S.m_pCharacter->Freeze(1));
	EXPECT_EQ(S.m_pCharacter->m_FreezeTime, 10);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeStart, 0);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeEnd, 123);
}

TEST(PredictionCharacterTiming, ExactUpperFreezeDeadlineIsStillAccepted)
{
	CScenario S(std::numeric_limits<int>::max() - 50);
	S.m_World.m_WorldConfig.m_PredictFreeze = true;
	ASSERT_TRUE(S.m_pCharacter->Freeze(1));
	EXPECT_EQ(S.m_pCharacter->m_FreezeTime, 50);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeEnd, std::numeric_limits<int>::max());
}

TEST(PredictionCharacterTiming, DeepFreezeKeepsInfiniteDeadlineAndWideLocalDuration)
{
	CScenario S;
	S.m_World.m_WorldConfig.m_PredictFreeze = true;
	S.m_pCharacter->Core()->m_DeepFrozen = true;
	ASSERT_TRUE(S.m_pCharacter->Freeze(std::numeric_limits<int>::max()));
	EXPECT_EQ(S.m_pCharacter->m_FreezeTime, 107374182350LL);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeEnd, -1);
}

TEST(PredictionCharacterTiming, StrongerExistingFreezeIsNotShortened)
{
	CScenario S;
	S.m_World.m_WorldConfig.m_PredictFreeze = true;
	S.m_pCharacter->m_FreezeTime = 150;
	EXPECT_FALSE(S.m_pCharacter->Freeze(2));
	EXPECT_EQ(S.m_pCharacter->m_FreezeTime, 150);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeStart, 0);
}

TEST(PredictionCharacterTiming, SnapshotFreezeRemainingTimeKeepsFullTickDifference)
{
	CScenario S;
	S.ReadFreezeEnd(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
	EXPECT_EQ(S.m_pCharacter->m_FreezeTime, 4294967295LL);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeEnd, std::numeric_limits<int>::max());
}

TEST(PredictionCharacterTiming, AlreadyExpiredSnapshotKeepsOneRemainingStep)
{
	CScenario S;
	S.ReadFreezeEnd(200, 100);
	EXPECT_EQ(S.m_pCharacter->m_FreezeTime, 1);
}

TEST(PredictionCharacterTiming, UnfreezeClearsWideCounterAndFiniteCoreDeadline)
{
	CScenario S;
	S.ReadFreezeEnd(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
	ASSERT_TRUE(S.m_pCharacter->Unfreeze());
	EXPECT_EQ(S.m_pCharacter->m_FreezeTime, 0);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeStart, 0);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeEnd, 0);
}

TEST(PredictionCharacterTiming, WorldCopiesKeepWideFreezeCounterAndIntCoreDeadline)
{
	CScenario S;
	S.ReadFreezeEnd(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
	CPredictionTestWorld Linked, Clean;
	ASSERT_TRUE(Linked.CopyWorld(&S.m_World));
	ASSERT_TRUE(Clean.CopyWorldClean(&S.m_World));
	for(CGameWorld *pWorld : {&Linked, &Clean})
	{
		auto *pCharacter = pWorld->GetCharacterById(0);
		ASSERT_NE(pCharacter, nullptr);
		EXPECT_EQ(pCharacter->m_FreezeTime, 4294967295LL);
		EXPECT_EQ(pCharacter->Core()->m_FreezeEnd, std::numeric_limits<int>::max());
	}
}
