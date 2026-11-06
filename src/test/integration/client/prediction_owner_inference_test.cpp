#include <generated/protocol.h>

#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/projectile.h>

#include <gtest/gtest.h>
#include <test/support/prediction_world.h>

#include <limits>

namespace
{
	CCharacter *AddCandidate(CGameWorld &World, int Id, vec2 Previous, vec2 Older)
	{
		CNetObj_Character Input{};
		Input.m_HookedPlayer = -1;
		Input.m_Weapon = WEAPON_HAMMER;
		Input.m_Emote = EMOTE_NORMAL;
		auto *pCharacter = new CCharacter(&World, Id, &Input);
		World.InsertEntity(pCharacter);
		pCharacter->m_PrevPos = Previous;
		pCharacter->m_PrevPrevPos = Older;
		return pCharacter;
	}

	CProjectile *ReceiveProjectile(CGameWorld &World, int StartTick)
	{
		CNetObj_Projectile Input{};
		Input.m_Type = WEAPON_GUN;
		Input.m_VelX = 100;
		Input.m_StartTick = StartTick;
		World.NetObjAdd(7, NETOBJTYPE_PROJECTILE, &Input, nullptr);
		return static_cast<CProjectile *>(World.GetEntity(7, CGameWorld::ENTTYPE_PROJECTILE));
	}
}

TEST(PredictionOwnerInference, MinimumTickStillRecognizesNewProjectileOwner)
{
	CPredictionTestWorld World;
	World.m_GameTick = std::numeric_limits<int>::min();
	World.m_WorldConfig.m_PredictWeapons = true;
	AddCandidate(World, 0, vec2(-21, 0), vec2(1000, 0));
	auto *pProjectile = ReceiveProjectile(World, std::numeric_limits<int>::min());
	ASSERT_NE(pProjectile, nullptr);
	EXPECT_EQ(pProjectile->GetOwner(), 0);
}

TEST(PredictionOwnerInference, AgeZeroAndOneUsePreviousPosition)
{
	for(const int StartTick : {100, 99})
	{
		SCOPED_TRACE(StartTick);
		CPredictionTestWorld World;
		World.m_WorldConfig.m_PredictWeapons = true;
		AddCandidate(World, 0, vec2(-21, 0), vec2(1000, 0));
		auto *pProjectile = ReceiveProjectile(World, StartTick);
		ASSERT_NE(pProjectile, nullptr);
		EXPECT_EQ(pProjectile->GetOwner(), 0);
	}
}

TEST(PredictionOwnerInference, AgeTwoThroughFourUseOlderPosition)
{
	for(const int StartTick : {98, 97, 96})
	{
		SCOPED_TRACE(StartTick);
		CPredictionTestWorld World;
		World.m_WorldConfig.m_PredictWeapons = true;
		AddCandidate(World, 0, vec2(1000, 0), vec2(-21, 0));
		auto *pProjectile = ReceiveProjectile(World, StartTick);
		ASSERT_NE(pProjectile, nullptr);
		EXPECT_EQ(pProjectile->GetOwner(), 0);
	}
}

TEST(PredictionOwnerInference, AgeFiveDoesNotGuessOwner)
{
	CPredictionTestWorld World;
	World.m_WorldConfig.m_PredictWeapons = true;
	AddCandidate(World, 0, vec2(-21, 0), vec2(-21, 0));
	auto *pProjectile = ReceiveProjectile(World, 95);
	ASSERT_NE(pProjectile, nullptr);
	EXPECT_EQ(pProjectile->GetOwner(), -1);
}

TEST(PredictionOwnerInference, OppositeFutureTickDoesNotWrapIntoOlderPositionSelection)
{
	CPredictionTestWorld World;
	World.m_GameTick = std::numeric_limits<int>::min() + 1;
	World.m_WorldConfig.m_PredictWeapons = true;
	AddCandidate(World, 0, vec2(-21, 0), vec2(1000, 0));
	auto *pProjectile = ReceiveProjectile(World, std::numeric_limits<int>::max());
	ASSERT_NE(pProjectile, nullptr);
	EXPECT_EQ(pProjectile->GetOwner(), 0);
}

TEST(PredictionOwnerInference, AmbiguousNearbyCandidatesKeepUnknownOwner)
{
	for(const bool ClosestAddedLast : {false, true})
	{
		SCOPED_TRACE(ClosestAddedLast);
		CPredictionTestWorld World;
		World.m_WorldConfig.m_PredictWeapons = true;
		if(ClosestAddedLast)
			AddCandidate(World, 1, vec2(-20, 0), vec2(-20, 0));
		AddCandidate(World, 0, vec2(-21, 0), vec2(-21, 0));
		if(!ClosestAddedLast)
			AddCandidate(World, 1, vec2(-20, 0), vec2(-20, 0));
		auto *pProjectile = ReceiveProjectile(World, 100);
		ASSERT_NE(pProjectile, nullptr);
		EXPECT_EQ(pProjectile->GetOwner(), -1);
	}
}

TEST(PredictionOwnerInference, RepeatedlyCloserCandidatesStillRetainSecondClosest)
{
	CPredictionTestWorld World;
	World.m_WorldConfig.m_PredictWeapons = true;
	AddCandidate(World, 0, vec2(-21, 0), vec2(-21, 0));
	AddCandidate(World, 1, vec2(-20, 0), vec2(-20, 0));
	AddCandidate(World, 2, vec2(79, 0), vec2(79, 0));
	auto *pProjectile = ReceiveProjectile(World, 100);
	ASSERT_NE(pProjectile, nullptr);
	EXPECT_EQ(pProjectile->GetOwner(), -1);
}

TEST(PredictionOwnerInference, ClearlySeparatedClosestCandidateIsSelectedInEitherOrder)
{
	for(const bool ClosestAddedLast : {false, true})
	{
		SCOPED_TRACE(ClosestAddedLast);
		CPredictionTestWorld World;
		World.m_WorldConfig.m_PredictWeapons = true;
		if(ClosestAddedLast)
			AddCandidate(World, 1, vec2(79, 0), vec2(79, 0));
		AddCandidate(World, 0, vec2(-21, 0), vec2(-21, 0));
		if(!ClosestAddedLast)
			AddCandidate(World, 1, vec2(79, 0), vec2(79, 0));
		auto *pProjectile = ReceiveProjectile(World, 100);
		ASSERT_NE(pProjectile, nullptr);
		EXPECT_EQ(pProjectile->GetOwner(), 0);
	}
}
