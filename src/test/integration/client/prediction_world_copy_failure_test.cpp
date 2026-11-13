#include <game/client/pickup_data.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/pickup.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/support/prediction_world.h>

namespace
{
	CPickup *AddPickup(CGameWorld &World, int Id)
	{
		CPickupData Data{};
		Data.m_Pos = vec2(10, 20);
		Data.m_Type = POWERUP_HEALTH;
		auto *pPickup = new CPickup(&World, Id, &Data);
		World.InsertEntity(pPickup, true);
		return pPickup;
	}

	CCharacter *AddCharacter(CGameWorld &World, int Id)
	{
		CNetObj_Character Input{};
		Input.m_HookedPlayer = -1;
		Input.m_Weapon = WEAPON_HAMMER;
		Input.m_Emote = EMOTE_NORMAL;
		auto *pCharacter = new CCharacter(&World, Id, &Input);
		World.InsertEntity(pCharacter);
		return pCharacter;
	}

	struct CScenario
	{
		CPredictionTestWorld Source, SourceOldCopy, OldParent, Destination, DestinationChild;
		CPickup *m_pSource = nullptr;
		CPickup *m_pSecondSource = nullptr;
		CEntity *m_pSourceOldCopy = nullptr;
		CEntity *m_pDestination = nullptr;
		CEntity *m_pDestinationChild = nullptr;
		CCharacter *m_apCharacters[2]{};

		bool Prepare()
		{
			m_pSource = AddPickup(Source, 1);
			m_pSecondSource = AddPickup(Source, 2);
			m_apCharacters[0] = AddCharacter(Source, 0);
			m_apCharacters[1] = AddCharacter(Source, 1);
			Source.m_GameTick = 200;
			Source.m_Core.m_vSwitchers.resize(2);
			Source.m_Core.m_vSwitchers[1].m_aStatus[0] = true;
			Source.m_PredictedEvents.emplace_back(NETEVENTTYPE_SOUNDWORLD, vec2(10, 20), 11, 200, 3);
			AddPickup(OldParent, 91);
			OldParent.m_PredictedEvents.emplace_back(NETEVENTTYPE_EXPLOSION, vec2(30, 40), 91, 100);
			OldParent.m_PredictedEvents[0].m_Handled = true;
			if(!SourceOldCopy.CopyWorld(&Source) || !Destination.CopyWorld(&OldParent) || !DestinationChild.CopyWorld(&Destination))
				return false;
			m_pSourceOldCopy = SourceOldCopy.FindFirst(CGameWorld::ENTTYPE_PICKUP);
			m_pDestination = Destination.FindFirst(CGameWorld::ENTTYPE_PICKUP);
			m_pDestinationChild = DestinationChild.FindFirst(CGameWorld::ENTTYPE_PICKUP);
			return m_pSourceOldCopy && m_pDestination && m_pDestinationChild;
		}

		void ExpectUnchanged()
		{
			EXPECT_EQ(Source.m_pChild, &SourceOldCopy);
			EXPECT_EQ(SourceOldCopy.m_pParent, &Source);
			EXPECT_TRUE(SourceOldCopy.m_IsValidCopy);
			EXPECT_EQ(m_pSource->m_pChild, m_pSourceOldCopy);
			EXPECT_EQ(m_pSourceOldCopy->m_pParent, m_pSource);
			ASSERT_NE(m_pSecondSource->m_pChild, nullptr);
			EXPECT_EQ(m_pSecondSource->m_pChild->m_pParent, m_pSecondSource);
			EXPECT_EQ(OldParent.m_pChild, &Destination);
			EXPECT_EQ(Destination.m_pParent, &OldParent);
			EXPECT_EQ(Destination.m_pChild, &DestinationChild);
			EXPECT_EQ(DestinationChild.m_pParent, &Destination);
			EXPECT_TRUE(Destination.m_IsValidCopy);
			EXPECT_TRUE(DestinationChild.m_IsValidCopy);
			EXPECT_EQ(Destination.FindFirst(CGameWorld::ENTTYPE_PICKUP), m_pDestination);
			EXPECT_EQ(m_pDestination->m_pChild, m_pDestinationChild);
			EXPECT_EQ(m_pDestinationChild->m_pParent, m_pDestination);
			EXPECT_EQ(Destination.m_GameTick, 100);
			ASSERT_EQ(Destination.m_PredictedEvents.size(), 1u);
			EXPECT_EQ(Destination.m_PredictedEvents[0].m_Id, 91);
			EXPECT_TRUE(Destination.m_PredictedEvents[0].m_Handled);
			for(int Id = 0; Id < 2; Id++)
			{
				EXPECT_EQ(Source.GetCharacterById(Id), m_apCharacters[Id]);
				EXPECT_EQ(Source.m_Core.m_apCharacters[Id], m_apCharacters[Id]->Core());
				EXPECT_EQ(m_apCharacters[Id]->GameWorld(), &Source);
				EXPECT_EQ(Destination.GetCharacterById(Id), nullptr);
			}
		}
	};

	bool FailCopy(CGameWorld &Destination, CGameWorld &Source, std::size_t BeforeFailure, bool Clean = false)
	{
		bool Result;
		bool Triggered;
		{
			CPredictionAllocationFailure Failure(BeforeFailure);
			Result = Clean ? Destination.CopyWorldClean(&Source) : Destination.CopyWorld(&Source);
			Triggered = Failure.Triggered();
		}
		EXPECT_TRUE(Triggered);
		return Result;
	}
}

TEST(PredictionWorldCopyFailure, SwitcherAllocationFailurePreservesBothWorldsAndExistingCopies)
{
	CScenario State;
	ASSERT_TRUE(State.Prepare());
	EXPECT_FALSE(FailCopy(State.Destination, State.Source, 0));
	State.ExpectUnchanged();
	ASSERT_EQ(State.Source.m_Core.m_vSwitchers.size(), 2u);
	EXPECT_TRUE(State.Source.m_Core.m_vSwitchers[1].m_aStatus[0]);
}

TEST(PredictionWorldCopyFailure, EventAllocationFailurePreservesOldHistoryAndLinks)
{
	CScenario State;
	ASSERT_TRUE(State.Prepare());
	State.Source.m_Core.m_vSwitchers.clear();
	EXPECT_FALSE(FailCopy(State.Destination, State.Source, 0));
	State.ExpectUnchanged();
	ASSERT_EQ(State.Source.m_PredictedEvents.size(), 1u);
	EXPECT_EQ(State.Source.m_PredictedEvents[0].m_Id, 11);
}

TEST(PredictionWorldCopyFailure, FirstEntityAllocationFailureIsReportedWithoutNullWrite)
{
	CScenario State;
	ASSERT_TRUE(State.Prepare());
	State.Source.m_Core.m_vSwitchers.clear();
	State.Source.m_PredictedEvents.clear();
	EXPECT_FALSE(FailCopy(State.Destination, State.Source, 0));
	State.ExpectUnchanged();
}

TEST(PredictionWorldCopyFailure, LaterEntityFailureReclaimsCandidateWithoutDetachingSourceCopy)
{
	CScenario State;
	ASSERT_TRUE(State.Prepare());
	State.Source.m_Core.m_vSwitchers.clear();
	State.Source.m_PredictedEvents.clear();
	EXPECT_FALSE(FailCopy(State.Destination, State.Source, 1));
	State.ExpectUnchanged();
}

TEST(PredictionWorldCopyFailure, CleanCopyFailurePreservesDestinationParentAndChild)
{
	CScenario State;
	ASSERT_TRUE(State.Prepare());
	State.Source.m_Core.m_vSwitchers.clear();
	State.Source.m_PredictedEvents.clear();
	EXPECT_FALSE(FailCopy(State.Destination, State.Source, 1, true));
	State.ExpectUnchanged();
}

TEST(PredictionWorldCopyFailure, ASubsequentCopyCanCommitAfterFailedPreparation)
{
	CScenario State;
	ASSERT_TRUE(State.Prepare());
	EXPECT_FALSE(FailCopy(State.Destination, State.Source, 0));
	State.ExpectUnchanged();
	ASSERT_TRUE(State.Destination.CopyWorld(&State.Source));
	auto *pNew = State.Destination.FindFirst(CGameWorld::ENTTYPE_PICKUP);
	ASSERT_NE(pNew, nullptr);
	EXPECT_EQ(pNew->GetId(), 1);
	EXPECT_EQ(pNew->m_pParent, State.m_pSource);
	EXPECT_EQ(State.m_pSource->m_pChild, pNew);
	EXPECT_EQ(State.SourceOldCopy.m_pParent, nullptr);
	EXPECT_FALSE(State.SourceOldCopy.m_IsValidCopy);
	EXPECT_EQ(State.OldParent.m_pChild, nullptr);
	EXPECT_EQ(State.DestinationChild.m_pParent, nullptr);
	EXPECT_FALSE(State.DestinationChild.m_IsValidCopy);
	EXPECT_EQ(State.Destination.m_GameTick, 200);
}

TEST(PredictionWorldCopyFailure, EveryPreparationAllocationCanFailWithoutPublishingPartialState)
{
	int Failures = 0;
	bool Completed = false;
	for(std::size_t BeforeFailure = 0; BeforeFailure < 64; BeforeFailure++)
	{
		SCOPED_TRACE(BeforeFailure);
		CScenario State;
		ASSERT_TRUE(State.Prepare());
		bool Result;
		bool Triggered;
		{
			CPredictionAllocationFailure Failure(BeforeFailure);
			Result = State.Destination.CopyWorld(&State.Source);
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_TRUE(Result);
			Completed = true;
			break;
		}
		++Failures;
		EXPECT_FALSE(Result);
		State.ExpectUnchanged();
	}
	EXPECT_TRUE(Completed);
	EXPECT_GE(Failures, 6);
}

TEST(PredictionWorldCopyFailure, CleanCopyKeepsEventHistoryAndSwitchersWithoutParentLinks)
{
	CScenario State;
	ASSERT_TRUE(State.Prepare());
	ASSERT_TRUE(State.Destination.CopyWorldClean(&State.Source));
	ASSERT_EQ(State.Destination.m_PredictedEvents.size(), 1u);
	EXPECT_EQ(State.Destination.m_PredictedEvents[0].m_Id, 11);
	EXPECT_EQ(State.Destination.m_PredictedEvents[0].m_ExtraInfo, 3);
	EXPECT_FALSE(State.Destination.m_PredictedEvents[0].m_Handled);
	ASSERT_EQ(State.Destination.m_Core.m_vSwitchers.size(), 2u);
	EXPECT_TRUE(State.Destination.m_Core.m_vSwitchers[1].m_aStatus[0]);
	EXPECT_EQ(State.Destination.m_pParent, nullptr);
	EXPECT_EQ(State.Source.m_pChild, &State.SourceOldCopy);
	EXPECT_EQ(State.Source.GetCharacterById(0), State.m_apCharacters[0]);
	ASSERT_NE(State.Destination.GetCharacterById(0), nullptr);
	EXPECT_EQ(State.Destination.GetCharacterById(0)->GameWorld(), &State.Destination);
	EXPECT_EQ(State.Destination.m_Core.m_apCharacters[0], State.Destination.GetCharacterById(0)->Core());
}

TEST(PredictionWorldCopyFailure, SelfAndNullCopyNeedNoAllocation)
{
	CPredictionTestWorld World;
	auto *pOriginal = AddPickup(World, 1);
	bool Self, CleanSelf, Null, CleanNull, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Self = World.CopyWorld(&World);
		CleanSelf = World.CopyWorldClean(&World);
		Null = World.CopyWorld(nullptr);
		CleanNull = World.CopyWorldClean(nullptr);
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Self && CleanSelf && Null && CleanNull);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(World.FindFirst(CGameWorld::ENTTYPE_PICKUP), pOriginal);
}
