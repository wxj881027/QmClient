#include <generated/protocol.h>

#include <game/client/components/qmclient/practice_tick_shift.h>
#include <game/client/laser_data.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/laser.h>
#include <game/client/prediction/entities/plasma.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/client/projectile_data.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/support/prediction_world.h>

#include <array>
#include <limits>

namespace
{
	CLaserData LaserData(int Tick, int Type = LASERTYPE_RIFLE)
	{
		CLaserData Data{};
		Data.m_From = vec2(0, 0);
		Data.m_To = vec2(1, 0);
		Data.m_StartTick = Tick;
		Data.m_Type = Type;
		Data.m_Owner = 0;
		return Data;
	}

	struct CScenario
	{
		CPredictionTestWorld m_World;
		std::array<int, 3> m_Attacks{-1, 0, 80};
		CCharacter *m_pCharacter;
		CProjectile *m_pProjectile;
		CLaser *m_pLaser;
		CPlasma *m_pPlasma;

		CScenario()
		{
			m_World.Switchers().resize(1);
			m_World.Switchers()[0].m_aEndTick[0] = 120;
			m_World.Switchers()[0].m_aLastUpdateTick[0] = 100;
			CNetObj_Character Input{};
			Input.m_HookedPlayer = -1;
			Input.m_Weapon = WEAPON_HAMMER;
			Input.m_Emote = EMOTE_NORMAL;
			Input.m_AttackTick = 20;
			m_pCharacter = new CCharacter(&m_World, 0, &Input);
			m_World.InsertEntity(m_pCharacter);
			m_pCharacter->Core()->m_FreezeStart = 10;
			m_pCharacter->Core()->m_FreezeEnd = 30;
			m_pCharacter->SetWeaponGot(WEAPON_NINJA, true);
			m_pCharacter->SetNinjaActivationTick(15);
			CProjectileData Projectile{};
			Projectile.m_StartPos = vec2(10, 20);
			Projectile.m_StartVel = vec2(1, 0);
			Projectile.m_Type = WEAPON_GUN;
			Projectile.m_StartTick = 30;
			m_pProjectile = new CProjectile(&m_World, 7, &Projectile);
			m_World.InsertEntity(m_pProjectile);
			auto Laser = LaserData(40);
			m_pLaser = new CLaser(&m_World, 8, &Laser);
			m_World.InsertEntity(m_pLaser);
			auto Plasma = LaserData(50, LASERTYPE_PLASMA);
			m_pPlasma = new CPlasma(&m_World, 9, &Plasma);
			m_World.InsertEntity(m_pPlasma);
		}

		void ExpectOriginalOtherEntities()
		{
			EXPECT_EQ(m_World.GameTick(), 100);
			EXPECT_EQ(m_pCharacter->GetAttackTick(), 20);
			EXPECT_EQ(m_pCharacter->Core()->m_Ninja.m_ActivationTick, 15);
			EXPECT_EQ(m_pProjectile->GetStartTick(), 30);
			EXPECT_EQ(m_pLaser->GetEvalTick(), 40);
		}
	};
}

TEST(PracticeTickShift, ShiftsWorldAndAllExistingTimestampKindsTogether)
{
	CScenario S;
	const int64_t LifeSpan = S.m_pProjectile->GetLifeSpan();
	ASSERT_TRUE(QmPractice::TryShiftTicks(S.m_World, 5, S.m_Attacks));
	EXPECT_EQ(S.m_World.GameTick(), 105);
	EXPECT_EQ(S.m_World.Switchers()[0].m_aEndTick[0], 125);
	EXPECT_EQ(S.m_World.Switchers()[0].m_aLastUpdateTick[0], 105);
	EXPECT_EQ(S.m_Attacks, (std::array<int, 3>{-1, 5, 85}));
	EXPECT_EQ(S.m_pCharacter->GetAttackTick(), 25);
	EXPECT_EQ(S.m_pCharacter->GetLastDamageTick(), -1);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeStart, 15);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeEnd, 35);
	EXPECT_EQ(S.m_pCharacter->Core()->m_Ninja.m_ActivationTick, 20);
	EXPECT_EQ(S.m_pProjectile->GetStartTick(), 35);
	EXPECT_EQ(S.m_pProjectile->GetLifeSpan(), LifeSpan);
	EXPECT_EQ(S.m_pProjectile->GetPos(0), vec2(10, 20));
	EXPECT_EQ(S.m_pLaser->GetEvalTick(), 45);
	auto Data = LaserData(55, LASERTYPE_PLASMA);
	CPlasma Expected(&S.m_World, 10, &Data);
	EXPECT_TRUE(S.m_pPlasma->Match(&Expected));
	EXPECT_EQ(S.m_World.GetCharacterById(0), S.m_pCharacter);
	EXPECT_EQ(S.m_World.m_Core.m_apCharacters[0], S.m_pCharacter->Core());
}

TEST(PracticeTickShift, NonPositiveDeltaKeepsPauseNoOpSemantics)
{
	CScenario S;
	for(const int64_t Delta : {int64_t{0}, int64_t{-1}, std::numeric_limits<int64_t>::min()})
	{
		ASSERT_TRUE(QmPractice::TryShiftTicks(S.m_World, Delta, S.m_Attacks));
		S.ExpectOriginalOtherEntities();
		EXPECT_EQ(S.m_Attacks, (std::array<int, 3>{-1, 0, 80}));
	}
}

TEST(PracticeTickShift, WorldTickOverflowKeepsEntitiesAndHistory)
{
	CScenario S;
	S.m_World.m_GameTick = std::numeric_limits<int>::max();
	EXPECT_FALSE(QmPractice::TryShiftTicks(S.m_World, 1, S.m_Attacks));
	EXPECT_EQ(S.m_World.GameTick(), std::numeric_limits<int>::max());
	EXPECT_EQ(S.m_pCharacter->GetAttackTick(), 20);
	EXPECT_EQ(S.m_pProjectile->GetStartTick(), 30);
	EXPECT_EQ(S.m_Attacks, (std::array<int, 3>{-1, 0, 80}));
}

TEST(PracticeTickShift, LastTeamSwitcherOverflowKeepsEarlierFields)
{
	CScenario S;
	S.m_World.Switchers()[0].m_aLastUpdateTick[NUM_DDRACE_TEAMS - 1] = std::numeric_limits<int>::max();
	EXPECT_FALSE(QmPractice::TryShiftTicks(S.m_World, 1, S.m_Attacks));
	S.ExpectOriginalOtherEntities();
	EXPECT_EQ(S.m_World.Switchers()[0].m_aEndTick[0], 120);
	EXPECT_EQ(S.m_World.Switchers()[0].m_aLastUpdateTick[0], 100);
	EXPECT_EQ(S.m_World.Switchers()[0].m_aLastUpdateTick[NUM_DDRACE_TEAMS - 1], std::numeric_limits<int>::max());
}

TEST(PracticeTickShift, AttackHistoryOverflowKeepsWorldAndEarlierHistory)
{
	CScenario S;
	S.m_Attacks[2] = std::numeric_limits<int>::max();
	EXPECT_FALSE(QmPractice::TryShiftTicks(S.m_World, 1, S.m_Attacks));
	S.ExpectOriginalOtherEntities();
	EXPECT_EQ(S.m_Attacks[1], 0);
	EXPECT_EQ(S.m_World.Switchers()[0].m_aEndTick[0], 120);
}

TEST(PracticeTickShift, CharacterFreezeEndOverflowKeepsOtherTimestampKinds)
{
	CScenario S;
	S.m_pCharacter->Core()->m_FreezeEnd = std::numeric_limits<int>::max();
	EXPECT_FALSE(QmPractice::TryShiftTicks(S.m_World, 1, S.m_Attacks));
	S.ExpectOriginalOtherEntities();
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeStart, 10);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeEnd, std::numeric_limits<int>::max());
	EXPECT_EQ(S.m_Attacks[1], 0);
}

TEST(PracticeTickShift, ProjectileOverflowKeepsPreviouslyCheckedCharacter)
{
	CScenario S;
	ASSERT_TRUE(S.m_pProjectile->ShiftStartTick(static_cast<int64_t>(std::numeric_limits<int>::max()) - 30));
	EXPECT_FALSE(QmPractice::TryShiftTicks(S.m_World, 1, S.m_Attacks));
	EXPECT_EQ(S.m_World.GameTick(), 100);
	EXPECT_EQ(S.m_pCharacter->GetAttackTick(), 20);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeStart, 10);
	EXPECT_EQ(S.m_pProjectile->GetStartTick(), std::numeric_limits<int>::max());
	EXPECT_EQ(S.m_pLaser->GetEvalTick(), 40);
}

TEST(PracticeTickShift, LaserOverflowKeepsPreviouslyCheckedProjectile)
{
	CScenario S;
	ASSERT_TRUE(S.m_pLaser->ShiftEvalTick(static_cast<int64_t>(std::numeric_limits<int>::max()) - 40));
	EXPECT_FALSE(QmPractice::TryShiftTicks(S.m_World, 1, S.m_Attacks));
	EXPECT_EQ(S.m_World.GameTick(), 100);
	EXPECT_EQ(S.m_pCharacter->GetAttackTick(), 20);
	EXPECT_EQ(S.m_pProjectile->GetStartTick(), 30);
	EXPECT_EQ(S.m_pLaser->GetEvalTick(), std::numeric_limits<int>::max());
}

TEST(PracticeTickShift, PlasmaOverflowKeepsAllEarlierEntitiesAndAttackHistory)
{
	CScenario S;
	ASSERT_TRUE(S.m_pPlasma->ShiftEvalTick(static_cast<int64_t>(std::numeric_limits<int>::max()) - 50));
	EXPECT_FALSE(QmPractice::TryShiftTicks(S.m_World, 1, S.m_Attacks));
	S.ExpectOriginalOtherEntities();
	EXPECT_EQ(S.m_Attacks, (std::array<int, 3>{-1, 0, 80}));
	EXPECT_EQ(S.m_World.Switchers()[0].m_aEndTick[0], 120);
	auto Data = LaserData(std::numeric_limits<int>::max(), LASERTYPE_PLASMA);
	CPlasma Expected(&S.m_World, 10, &Data);
	EXPECT_TRUE(S.m_pPlasma->Match(&Expected));
}

TEST(PracticeTickShift, SentinelFieldsAreNotShifted)
{
	CScenario S;
	S.m_World.Switchers()[0].m_aEndTick[0] = 0;
	S.m_World.Switchers()[0].m_aLastUpdateTick[0] = -1;
	S.m_pCharacter->Core()->m_FreezeStart = 0;
	S.m_pCharacter->Core()->m_FreezeEnd = -1;
	S.m_pCharacter->SetWeaponGot(WEAPON_NINJA, false);
	S.m_pCharacter->SetNinjaActivationTick(std::numeric_limits<int>::max());
	ASSERT_TRUE(QmPractice::TryShiftTicks(S.m_World, 5, S.m_Attacks));
	EXPECT_EQ(S.m_World.Switchers()[0].m_aEndTick[0], 0);
	EXPECT_EQ(S.m_World.Switchers()[0].m_aLastUpdateTick[0], -1);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeStart, 0);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeEnd, -1);
	EXPECT_EQ(S.m_pCharacter->Core()->m_Ninja.m_ActivationTick, std::numeric_limits<int>::max());
	EXPECT_EQ(S.m_pCharacter->GetLastDamageTick(), -1);
}

TEST(PracticeTickShift, WidePositiveDeltaReachesExactTargetWithoutIntSubtraction)
{
	CPredictionTestWorld World;
	World.m_GameTick = std::numeric_limits<int>::min();
	ASSERT_TRUE(QmPractice::TryShiftTicks(World, 4294967295LL, {}));
	EXPECT_EQ(World.GameTick(), std::numeric_limits<int>::max());
}

TEST(PracticeTickShift, FailedWorldShiftCanBeRetriedAfterRepairingTheOffendingClock)
{
	CScenario S;
	ASSERT_TRUE(S.m_pLaser->ShiftEvalTick(static_cast<int64_t>(std::numeric_limits<int>::max()) - 40));
	EXPECT_FALSE(QmPractice::TryShiftTicks(S.m_World, 5, S.m_Attacks));
	ASSERT_TRUE(S.m_pLaser->ShiftEvalTick(40LL - std::numeric_limits<int>::max()));
	ASSERT_TRUE(QmPractice::TryShiftTicks(S.m_World, 5, S.m_Attacks));
	EXPECT_EQ(S.m_World.GameTick(), 105);
	EXPECT_EQ(S.m_pCharacter->GetAttackTick(), 25);
	EXPECT_EQ(S.m_pProjectile->GetStartTick(), 35);
	EXPECT_EQ(S.m_pLaser->GetEvalTick(), 45);
}

TEST(PracticeTickShift, ShiftDoesNotAllocateAfterScenarioPreparation)
{
	CScenario S;
	bool Shifted, Allocated;
	{
		CPredictionAllocationFailure Failure;
		Shifted = QmPractice::TryShiftTicks(S.m_World, 5, S.m_Attacks);
		Allocated = Failure.Triggered();
	}
	EXPECT_TRUE(Shifted);
	EXPECT_FALSE(Allocated);
	EXPECT_EQ(S.m_World.GameTick(), 105);
}

TEST(PracticeTickShift, CharacterDirectCheckAndFailedCommitLeaveEarlierFieldsUntouched)
{
	CScenario S;
	ASSERT_TRUE(S.m_pCharacter->ShiftTickBase(5, QmTickShift::EMode::CHECK));
	EXPECT_EQ(S.m_pCharacter->GetAttackTick(), 20);
	S.m_pCharacter->Core()->m_FreezeEnd = std::numeric_limits<int>::max();
	EXPECT_FALSE(S.m_pCharacter->ShiftTickBase(5));
	EXPECT_EQ(S.m_pCharacter->GetAttackTick(), 20);
	EXPECT_EQ(S.m_pCharacter->Core()->m_FreezeStart, 10);
	EXPECT_EQ(S.m_pCharacter->Core()->m_Ninja.m_ActivationTick, 15);
}

TEST(PracticeTickShift, RecordedDamageAtTickZeroMovesWithPause)
{
	CScenario S;
	S.m_World.m_GameTick = 0;
	ASSERT_TRUE(S.m_pCharacter->TakeDamage(vec2(0, 0), 0, 14, WEAPON_GRENADE));
	S.m_World.m_GameTick = 100;
	ASSERT_TRUE(QmPractice::TryShiftTicks(S.m_World, 5, S.m_Attacks));
	EXPECT_EQ(S.m_pCharacter->GetLastDamageTick(), 5);
	EXPECT_EQ(S.m_pCharacter->GetLastDamageFrom(), 14);
	EXPECT_EQ(S.m_pCharacter->GetLastDamageWeapon(), WEAPON_GRENADE);
}

TEST(PracticeTickShift, TuneZoneTimestampOverflowRejectsWholeShift)
{
	CScenario S;
	S.m_World.m_GameTick = std::numeric_limits<int>::max();
	S.m_pCharacter->SetTuneZone(1);
	S.m_World.m_GameTick = 100;
	EXPECT_FALSE(QmPractice::TryShiftTicks(S.m_World, 1, S.m_Attacks));
	S.ExpectOriginalOtherEntities();
	EXPECT_EQ(S.m_pCharacter->GetPureTuneZone(), 1);
	EXPECT_EQ(S.m_Attacks[1], 0);
}

TEST(PracticeTickShift, WeaponSwitchTimestampOverflowRejectsWholeShift)
{
	CScenario S;
	S.m_World.m_GameTick = std::numeric_limits<int>::max();
	S.m_pCharacter->SetActiveWeapon(WEAPON_GUN);
	S.m_World.m_GameTick = 100;
	EXPECT_FALSE(QmPractice::TryShiftTicks(S.m_World, 1, S.m_Attacks));
	S.ExpectOriginalOtherEntities();
	EXPECT_EQ(S.m_pCharacter->Core()->m_ActiveWeapon, WEAPON_GUN);
}

TEST(PracticeTickShift, NinjaActivationOverflowRejectsWholeShift)
{
	CScenario S;
	S.m_pCharacter->SetNinjaActivationTick(std::numeric_limits<int>::max());
	EXPECT_FALSE(QmPractice::TryShiftTicks(S.m_World, 1, S.m_Attacks));
	EXPECT_EQ(S.m_World.GameTick(), 100);
	EXPECT_EQ(S.m_pCharacter->GetAttackTick(), 20);
	EXPECT_EQ(S.m_pCharacter->Core()->m_Ninja.m_ActivationTick, std::numeric_limits<int>::max());
	EXPECT_EQ(S.m_pProjectile->GetStartTick(), 30);
}

TEST(PracticeTickShift, LastDamageTimestampOverflowRejectsWholeShift)
{
	CScenario S;
	S.m_World.m_GameTick = std::numeric_limits<int>::max();
	ASSERT_TRUE(S.m_pCharacter->TakeDamage(vec2(0, 0), 0, 14, WEAPON_GRENADE));
	S.m_World.m_GameTick = 100;
	EXPECT_FALSE(QmPractice::TryShiftTicks(S.m_World, 1, S.m_Attacks));
	S.ExpectOriginalOtherEntities();
	EXPECT_EQ(S.m_pCharacter->GetLastDamageTick(), std::numeric_limits<int>::max());
	EXPECT_EQ(S.m_pCharacter->GetLastDamageFrom(), 14);
}
