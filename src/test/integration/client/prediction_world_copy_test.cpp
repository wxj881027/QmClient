#include <game/client/pickup_data.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/pickup.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>
#include <test/support/prediction_world.h>

#include <memory>

namespace
{
	CPickup *AddPickup(CGameWorld &World, int Id, vec2 Pos = vec2(10, 20), int Flags = 0)
	{
		CPickupData Data{};
		Data.m_Pos = Pos;
		Data.m_Type = POWERUP_HEALTH;
		Data.m_Flags = Flags;
		auto *pPickup = new CPickup(&World, Id, &Data);
		World.InsertEntity(pPickup, true);
		return pPickup;
	}
}

TEST(PredictionWorldCopy, NativeCopyKeepsEntityOrderDataAndReciprocalLinks)
{
	CPredictionTestWorld Source, Copy;
	const auto *pFirst = AddPickup(Source, 1, vec2(100, 200), PICKUPFLAG_NO_PREDICT);
	AddPickup(Source, 2, vec2(-30, 50));
	Source.m_GameTick = 123;
	ASSERT_TRUE(Copy.CopyWorld(&Source));
	auto *pCopy = static_cast<CPickup *>(Copy.FindFirst(CGameWorld::ENTTYPE_PICKUP));
	ASSERT_NE(pCopy, nullptr);
	EXPECT_NE(pCopy, pFirst);
	EXPECT_EQ(pCopy->GetId(), 1);
	EXPECT_EQ(pCopy->m_Pos, vec2(100, 200));
	EXPECT_EQ(pCopy->Flags(), PICKUPFLAG_NO_PREDICT);
	ASSERT_NE(pCopy->TypeNext(), nullptr);
	EXPECT_EQ(pCopy->TypeNext()->GetId(), 2);
	EXPECT_EQ(pCopy->m_pParent, pFirst);
	EXPECT_EQ(pFirst->m_pChild, pCopy);
	EXPECT_EQ(pCopy->m_pChild, nullptr);
	EXPECT_EQ(Copy.m_pParent, &Source);
	EXPECT_EQ(Source.m_pChild, &Copy);
	EXPECT_TRUE(Copy.m_IsValidCopy);
	EXPECT_EQ(Copy.m_GameTick, 123);
}

TEST(PredictionWorldCopy, ReplacingNativeChildThenDestroyingOldCopyKeepsCurrentLinks)
{
	CPredictionTestWorld Source, Current;
	auto Old = std::make_unique<CPredictionTestWorld>();
	auto *pSource = AddPickup(Source, 1);
	ASSERT_TRUE(Old->CopyWorld(&Source));
	auto *pOld = Old->FindFirst(CGameWorld::ENTTYPE_PICKUP);
	ASSERT_NE(pOld, nullptr);
	ASSERT_TRUE(Current.CopyWorld(&Source));
	auto *pCurrent = Current.FindFirst(CGameWorld::ENTTYPE_PICKUP);
	ASSERT_NE(pCurrent, nullptr);
	EXPECT_EQ(Old->m_pParent, nullptr);
	EXPECT_FALSE(Old->m_IsValidCopy);
	EXPECT_EQ(pOld->m_pParent, nullptr);
	Old.reset();
	EXPECT_EQ(Source.m_pChild, &Current);
	EXPECT_EQ(Current.m_pParent, &Source);
	EXPECT_EQ(pSource->m_pChild, pCurrent);
	EXPECT_EQ(pCurrent->m_pParent, pSource);
}

TEST(PredictionWorldCopy, ReparentedNativeWorldSurvivesOldParentDestruction)
{
	CPredictionTestWorld NewParent, Copy;
	auto OldParent = std::make_unique<CPredictionTestWorld>();
	AddPickup(*OldParent, 1);
	auto *pNew = AddPickup(NewParent, 2);
	ASSERT_TRUE(Copy.CopyWorld(OldParent.get()));
	ASSERT_TRUE(Copy.CopyWorld(&NewParent));
	EXPECT_EQ(OldParent->m_pChild, nullptr);
	OldParent.reset();
	auto *pCopy = Copy.FindFirst(CGameWorld::ENTTYPE_PICKUP);
	ASSERT_NE(pCopy, nullptr);
	EXPECT_EQ(Copy.m_pParent, &NewParent);
	EXPECT_EQ(NewParent.m_pChild, &Copy);
	EXPECT_EQ(pCopy->GetId(), 2);
	EXPECT_EQ(pCopy->m_pParent, pNew);
	EXPECT_EQ(pNew->m_pChild, pCopy);
}

TEST(PredictionWorldCopy, CleanCopyDetachesFormerParentsAndChildren)
{
	CPredictionTestWorld Source, Destination, Child, Independent;
	auto *pSource = AddPickup(Source, 1);
	AddPickup(Independent, 2);
	ASSERT_TRUE(Destination.CopyWorld(&Source));
	ASSERT_TRUE(Child.CopyWorld(&Destination));
	auto *pChild = Child.FindFirst(CGameWorld::ENTTYPE_PICKUP);
	ASSERT_NE(pChild, nullptr);
	ASSERT_TRUE(Destination.CopyWorldClean(&Independent));
	auto *pDestination = Destination.FindFirst(CGameWorld::ENTTYPE_PICKUP);
	ASSERT_NE(pDestination, nullptr);
	EXPECT_EQ(Destination.m_pParent, nullptr);
	EXPECT_EQ(Destination.m_pChild, nullptr);
	EXPECT_FALSE(Destination.m_IsValidCopy);
	EXPECT_EQ(Source.m_pChild, nullptr);
	EXPECT_EQ(pSource->m_pChild, nullptr);
	EXPECT_EQ(Child.m_pParent, nullptr);
	EXPECT_FALSE(Child.m_IsValidCopy);
	EXPECT_EQ(pChild->m_pParent, nullptr);
	EXPECT_EQ(pDestination->GetId(), 2);
	EXPECT_EQ(pDestination->m_pParent, nullptr);
	EXPECT_EQ(pDestination->m_pChild, nullptr);
	EXPECT_EQ(Independent.m_pChild, nullptr);
}

TEST(PredictionWorldCopy, ReplacingSourceCopyDoesNotInheritOrBreakOldCopiesChild)
{
	CPredictionTestWorld Source, OldCopy, GrandChild, Replacement;
	AddPickup(Source, 1);
	ASSERT_TRUE(OldCopy.CopyWorld(&Source));
	ASSERT_TRUE(GrandChild.CopyWorld(&OldCopy));
	auto *pOld = OldCopy.FindFirst(CGameWorld::ENTTYPE_PICKUP);
	auto *pGrandChild = GrandChild.FindFirst(CGameWorld::ENTTYPE_PICKUP);
	ASSERT_NE(pOld, nullptr);
	ASSERT_NE(pGrandChild, nullptr);
	ASSERT_TRUE(Replacement.CopyWorld(&Source));
	auto *pReplacement = Replacement.FindFirst(CGameWorld::ENTTYPE_PICKUP);
	ASSERT_NE(pReplacement, nullptr);
	EXPECT_EQ(pReplacement->m_pChild, nullptr);
	EXPECT_EQ(pOld->m_pParent, nullptr);
	EXPECT_EQ(pOld->m_pChild, pGrandChild);
	EXPECT_EQ(pGrandChild->m_pParent, pOld);
	EXPECT_EQ(GrandChild.m_pParent, &OldCopy);
}

TEST(PredictionWorldCopy, DestroyingNativeParentClearsEntityAndWorldBorrowedReferences)
{
	CPredictionTestWorld Copy;
	auto Source = std::make_unique<CPredictionTestWorld>();
	AddPickup(*Source, 1);
	ASSERT_TRUE(Copy.CopyWorld(Source.get()));
	auto *pCopy = Copy.FindFirst(CGameWorld::ENTTYPE_PICKUP);
	ASSERT_NE(pCopy, nullptr);
	Source.reset();
	EXPECT_EQ(Copy.m_pParent, nullptr);
	EXPECT_FALSE(Copy.m_IsValidCopy);
	EXPECT_EQ(pCopy->m_pParent, nullptr);
	Copy.Clear();
	EXPECT_EQ(Copy.FindFirst(CGameWorld::ENTTYPE_PICKUP), nullptr);
}

TEST(PredictionWorldCopy, RemovingCurrentCopyEntityKeepsOriginalDestroyTickPropagation)
{
	CPredictionTestWorld Source, Copy;
	auto *pSource = AddPickup(Source, 1);
	ASSERT_TRUE(Copy.CopyWorld(&Source));
	auto *pCopy = Copy.FindFirst(CGameWorld::ENTTYPE_PICKUP);
	ASSERT_NE(pCopy, nullptr);
	Copy.m_GameTick = 150;
	delete pCopy;
	EXPECT_EQ(pSource->m_DestroyTick, 150);
	EXPECT_EQ(pSource->m_pChild, nullptr);
	EXPECT_EQ(Copy.FindFirst(CGameWorld::ENTTYPE_PICKUP), nullptr);
}

TEST(PredictionWorldCopy, CleanCopyRemovalDoesNotPropagateDestroyTick)
{
	CPredictionTestWorld Source, Copy;
	auto *pSource = AddPickup(Source, 1);
	ASSERT_TRUE(Copy.CopyWorldClean(&Source));
	auto *pCopy = Copy.FindFirst(CGameWorld::ENTTYPE_PICKUP);
	ASSERT_NE(pCopy, nullptr);
	Copy.m_GameTick = 150;
	delete pCopy;
	EXPECT_EQ(pSource->m_DestroyTick, -1);
	EXPECT_EQ(pSource->m_pChild, nullptr);
}

TEST(PredictionWorldCopy, NullAndSelfCopyRemainNoOps)
{
	CPredictionTestWorld World;
	auto *pEntity = AddPickup(World, 1);
	ASSERT_TRUE(World.CopyWorld(&World));
	ASSERT_TRUE(World.CopyWorldClean(&World));
	ASSERT_TRUE(World.CopyWorld(nullptr));
	ASSERT_TRUE(World.CopyWorldClean(nullptr));
	EXPECT_EQ(World.FindFirst(CGameWorld::ENTTYPE_PICKUP), pEntity);
	EXPECT_EQ(pEntity->GetId(), 1);
	EXPECT_EQ(World.m_GameTick, 100);
	EXPECT_EQ(World.m_pParent, nullptr);
	EXPECT_EQ(World.m_pChild, nullptr);
}

namespace
{
	CNetObj_Character CharacterInput()
	{
		CNetObj_Character Input{};
		Input.m_HookedPlayer = -1;
		Input.m_Weapon = WEAPON_HAMMER;
		Input.m_Emote = EMOTE_NORMAL;
		Input.m_Health = 10;
		return Input;
	}
}

TEST(PredictionWorldCharacters, UnregisteredCharacterCopyDestructionKeepsCanonicalCharacter)
{
	CPredictionTestWorld World;
	auto Input = CharacterInput();
	auto *pSource = new CCharacter(&World, 0, &Input);
	World.InsertEntity(pSource);
	{
		CCharacter Copy(*pSource);
		EXPECT_EQ(Copy.TypeNext(), nullptr);
		EXPECT_EQ(Copy.TypePrev(), nullptr);
	}
	EXPECT_EQ(World.GetCharacterById(0), pSource);
	EXPECT_EQ(World.m_Core.m_apCharacters[0], pSource->Core());
	EXPECT_EQ(World.FindFirst(CGameWorld::ENTTYPE_CHARACTER), pSource);
}

TEST(PredictionWorldCharacters, MatchingTemporaryDoesNotUnregisterExistingCharacter)
{
	CPredictionTestWorld World;
	auto Input = CharacterInput();
	auto *pSource = new CCharacter(&World, 0, &Input);
	World.InsertEntity(pSource);
	EXPECT_EQ(World.FindMatch(0, NETOBJTYPE_CHARACTER, &Input), pSource);
	EXPECT_EQ(World.GetCharacterById(0), pSource);
	EXPECT_EQ(World.m_Core.m_apCharacters[0], pSource->Core());
}

TEST(PredictionWorldCharacters, RemovingOldSameIdCharacterKeepsReplacementRegistered)
{
	CPredictionTestWorld World;
	auto Input = CharacterInput();
	auto *pOld = new CCharacter(&World, 0, &Input);
	World.InsertEntity(pOld);
	auto *pCurrent = new CCharacter(&World, 0, &Input);
	World.InsertEntity(pCurrent);
	delete pOld;
	EXPECT_EQ(World.GetCharacterById(0), pCurrent);
	EXPECT_EQ(World.m_Core.m_apCharacters[0], pCurrent->Core());
	EXPECT_EQ(World.FindFirst(CGameWorld::ENTTYPE_CHARACTER), pCurrent);
}

TEST(PredictionWorldCharacters, RemovingCurrentCharacterClearsBothIndexes)
{
	CPredictionTestWorld World;
	auto Input = CharacterInput();
	auto *pCurrent = new CCharacter(&World, 0, &Input);
	World.InsertEntity(pCurrent);
	delete pCurrent;
	EXPECT_EQ(World.GetCharacterById(0), nullptr);
	EXPECT_EQ(World.m_Core.m_apCharacters[0], nullptr);
	EXPECT_EQ(World.FindFirst(CGameWorld::ENTTYPE_CHARACTER), nullptr);
}
