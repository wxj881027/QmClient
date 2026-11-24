#include <generated/protocol.h>

#include <game/client/components/qmclient/predicted_event_queue.h>
#include <game/client/pickup_data.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/pickup.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/support/prediction_world.h>

#include <memory>
#include <vector>

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

TEST(PredictionWorldCopy, FastInputRepredictionPlaysEachHammerSwingAndHitOnce)
{
	CPredictionTestWorld Predicted, Regular;
	Predicted.m_WorldConfig.m_PredictEvents = true;
	const int SwingTick = Predicted.GameTick();
	const vec2 AttackerPos(64, 32);
	const vec2 HitPos(88, 32);
	const std::vector<int> vSwingEvents{NETEVENTTYPE_SOUNDWORLD, NETEVENTTYPE_HAMMERHIT};
	auto Swing = [&] {
		Predicted.CreatePredictedEvent({NETEVENTTYPE_SOUNDWORLD, AttackerPos, 2, Predicted.GameTick(), SOUND_HAMMER_FIRE});
		Predicted.CreatePredictedHammerHitEvent(HitPos, 2, 3);
	};
	std::vector<int> vPlayed;
	auto PlayEvents = [&](int Tick) {
		QmProcessPredictedEvents(Predicted.m_PredictedEvents, Tick, SERVER_TICK_SPEED, [&](const CGameWorld::CPredictedEvent &Event) {
			vPlayed.push_back(Event.m_EventId);
			return true;
		});
	};

	Swing();
	ASSERT_TRUE(QmFinishRegularPredictionTick(Predicted, Regular, SwingTick, PlayEvents));
	ASSERT_EQ(vPlayed, vSwingEvents);
	vPlayed.clear();
	Predicted.m_GameTick++;
	ASSERT_TRUE(QmFinishRegularPredictionTick(Predicted, Regular, SwingTick, PlayEvents));
	ASSERT_TRUE(Predicted.CopyWorld(&Regular));
	ASSERT_EQ(Predicted.GameTick(), SwingTick);

	Swing();
	ASSERT_TRUE(QmFinishRegularPredictionTick(Predicted, Regular, SwingTick, PlayEvents));
	EXPECT_TRUE(vPlayed.empty());

	// 同一位置的下一次挥锤仍要触发挥锤声和命中特效。
	Predicted.m_GameTick += SERVER_TICK_SPEED;
	Swing();
	ASSERT_TRUE(QmFinishRegularPredictionTick(Predicted, Regular, Predicted.GameTick(), PlayEvents));
	EXPECT_EQ(vPlayed, vSwingEvents);
}

TEST(PredictionWorldCopy, SnapshotAfterFastInputRestoreRecognizesPlayedGunfire)
{
	CPredictionTestWorld Predicted, Regular;
	Predicted.m_WorldConfig.m_PredictEvents = true;
	const int FireTick = Predicted.GameTick();
	const vec2 Pos(64, 32);
	Predicted.CreatePredictedEvent({NETEVENTTYPE_SOUNDWORLD, Pos, 2, FireTick, SOUND_GUN_FIRE});
	int Played = 0;
	ASSERT_TRUE(QmFinishRegularPredictionTick(Predicted, Regular, FireTick, [&](int Tick) {
		QmProcessPredictedEvents(Predicted.m_PredictedEvents, Tick, SERVER_TICK_SPEED, [&](const CGameWorld::CPredictedEvent &) {
			++Played;
			return true;
		});
	}));
	ASSERT_EQ(Played, 1);
	ASSERT_TRUE(Predicted.CopyWorld(&Regular));

	bool NeedsSnapshotPlayback = true;
	ASSERT_TRUE(Predicted.CheckPredictedEventHandled({NETEVENTTYPE_SOUNDWORLD, Pos, -1, FireTick + 1, SOUND_GUN_FIRE}, &NeedsSnapshotPlayback));
	EXPECT_FALSE(NeedsSnapshotPlayback);
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

TEST(PredictionWorldCharacters, HookBacklinkAllocationFailureRetractsAndAllowsAnotherLaunch)
{
	CCollision Collision;
	CPredictionTestWorld World;
	World.Init(&Collision, World.TuningList(), nullptr);
	World.Teams()->Team(0, TEAM_FLOCK);
	World.Teams()->Team(1, TEAM_FLOCK);
	World.Teams()->Team(2, TEAM_FLOCK);

	auto Idle = CharacterInput();
	auto *pHooker = new CCharacter(&World, 0, &Idle);
	World.InsertEntity(pHooker);
	auto Target = CharacterInput();
	Target.m_X = 112;
	auto *pFarTarget = new CCharacter(&World, 1, &Target);
	World.InsertEntity(pFarTarget);
	Target.m_X = 80;
	auto *pNearTarget = new CCharacter(&World, 2, &Target);
	World.InsertEntity(pNearTarget);

	auto &Hooker = *World.m_Core.m_apCharacters[0];
	Hooker.m_Input = {};
	Hooker.m_Input.m_TargetX = 1;
	Hooker.m_Input.m_Hook = 1;
	bool Triggered;
	{
		CPredictionAllocationFailure Failure(1);
		Hooker.Tick(true);
		Triggered = Failure.Triggered();
	}

	EXPECT_TRUE(Triggered);
	EXPECT_EQ(Hooker.m_HookState, HOOK_RETRACTED);
	EXPECT_EQ(Hooker.HookedPlayer(), -1);
	EXPECT_EQ(Hooker.m_HookPos, Hooker.m_Pos);
	EXPECT_EQ(Hooker.m_TriggeredEvents & COREEVENT_HOOK_ATTACH_PLAYER, 0);
	EXPECT_TRUE(pFarTarget->Core()->m_AttachedPlayers.empty());
	EXPECT_TRUE(pNearTarget->Core()->m_AttachedPlayers.empty());

	Hooker.m_Input.m_Hook = 0;
	Hooker.Tick(true);
	Hooker.m_Input.m_Hook = 1;
	Hooker.Tick(true);

	EXPECT_EQ(Hooker.m_HookState, HOOK_GRABBED);
	EXPECT_EQ(Hooker.HookedPlayer(), 2);
	EXPECT_TRUE(pFarTarget->Core()->m_AttachedPlayers.empty());
	EXPECT_TRUE(pNearTarget->Core()->m_AttachedPlayers.contains(0));
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

TEST(PredictionWorldCharacters, CoreSnapshotAppliesHookChangeOnlyOnWriteBack)
{
	CCollision Collision;
	CPredictionTestWorld World;
	World.Init(&Collision, World.TuningList(), nullptr);
	auto Idle = CharacterInput();
	auto Hooked = Idle;
	Hooked.m_HookState = HOOK_GRABBED;
	Hooked.m_HookedPlayer = 1;
	auto *pHooked = new CCharacter(&World, 1, &Idle);
	World.InsertEntity(pHooked);
	auto *pHooker = new CCharacter(&World, 0, &Hooked);
	World.InsertEntity(pHooker);
	ASSERT_TRUE(pHooked->Core()->m_AttachedPlayers.contains(0));

	CCharacterCore Core = pHooker->GetCore();
	Core.Read(&Idle);
	EXPECT_EQ(Core.HookedPlayer(), -1);
	EXPECT_TRUE(pHooked->Core()->m_AttachedPlayers.contains(0));

	pHooker->SetCore(Core);
	EXPECT_EQ(pHooker->Core()->HookedPlayer(), -1);
	EXPECT_FALSE(pHooked->Core()->m_AttachedPlayers.contains(0));

	pHooker->Read(&Hooked, nullptr, true);
	EXPECT_TRUE(pHooked->Core()->m_AttachedPlayers.contains(0));
}
