// QmNewUi 菜单源码合同：gameplay 预测与事件。运行时行为保留在 qm_new_ui_menu_branch_test.cpp。
#include <engine/client/backend/vulkan/backend_vulkan.h>
#include <engine/client/backend_sdl.h>
#include <engine/client/plausible_sizes.h>
#include <engine/client/rounded_rect_geometry.h>
#include <engine/storage.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/camera.h>
#include <game/client/components/controls.h>
#include <game/client/components/menus.h>
#include <game/client/components/nameplate_text_effects.h>
#include <game/client/components/nameplates.h>
#include <game/client/components/qmclient/axiom_auto_login.h>
#include <game/client/components/tclient/statusbar.h>
#include <game/client/components/tooltips.h>
#include <game/client/prediction/gameworld.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>
#include <test/test.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>

TEST(QmNewUiMenuGameplayPredictionContract, WeaponImpactEventsUseInferredOwnerAlpha)
{
	const std::string Source = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string ProcessEvents = FunctionBody(Source, "void CGameClient::ProcessEvents()");
	const std::string FinalizeHammerHitEvents = FunctionBody(Source, "void CGameClient::FinalizeHammerHitEvents()");
	const std::string HandlePredictedEvents = FunctionBody(Source, "void CGameClient::HandlePredictedEvents(const int Tick)");

	EXPECT_NE(Source.find("float QmKnownOwnerEventAlpha(CGameClient *pGameClient, int Owner)"), std::string::npos);
	EXPECT_NE(Source.find("int QmInferExplosionOwner(CGameClient *pGameClient, vec2 Pos)"), std::string::npos);
	EXPECT_NE(Source.find("SQmHammerHitMatch QmInferHammerHit(CGameClient *pGameClient, vec2 Pos, int EventTick)"), std::string::npos);
	EXPECT_NE(ProcessEvents.find("const float ExplosionAlpha = QmKnownOwnerEventAlpha(this, QmInferExplosionOwner(this, ExplosionPos));"), std::string::npos);
	EXPECT_NE(ProcessEvents.find("m_Effects.Explosion(ExplosionPos, ExplosionAlpha);"), std::string::npos);
	EXPECT_NE(ProcessEvents.find("m_vPendingHammerHitEvents.push_back({"), std::string::npos);
	EXPECT_EQ(ProcessEvents.find("CheckPredictedHammerHitHandled("), std::string::npos);
	EXPECT_EQ(ProcessEvents.find("QmInferHammerHit(this"), std::string::npos);
	EXPECT_EQ(ProcessEvents.find("m_HammerHitTracker.Record(Hit)"), std::string::npos);
	EXPECT_EQ(ProcessEvents.find("m_Effects.HammerHit("), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("const SQmHammerHitMatch Match = QmInferHammerHit(this, Event.m_Pos, Event.m_SnapshotTick);"), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("m_PredictedWorld.CheckPredictedHammerHitHandled("), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("Match.m_AttackerId, Event.m_SnapshotTick, Match.m_TargetId"), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("m_HammerHitTracker.Record(Hit)"), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("const float HammerHitAlpha = QmKnownOwnerEventAlpha(this, Match.m_AttackerId);"), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("m_Effects.HammerHit(Event.m_Pos, HammerHitAlpha, 1.0f);"), std::string::npos);
	EXPECT_NE(HandlePredictedEvents.find("m_Effects.HammerHit(EventsIterator->m_Pos, Alpha, 1.0f);"), std::string::npos);
	EXPECT_EQ(ProcessEvents.find("m_Effects.Explosion(vec2(pEvent->m_X, pEvent->m_Y), Alpha);"), std::string::npos);
}

TEST(QmNewUiMenuGameplayPredictionContract, GhostPlayersDoNotEmitDuplicateWeaponEffects)
{
	const std::string Source = ReadTextFile("src/game/client/components/players.cpp");
	const std::string RenderPlayerGhost = FunctionBody(Source, "void CPlayers::RenderPlayerGhost(");

	EXPECT_NE(RenderPlayerGhost.find("const bool AllowEffects = false;"), std::string::npos);
	EXPECT_NE(RenderPlayerGhost.find("if(AllowEffects)\n\t\tGameClient()->m_Flow.Add("), std::string::npos);
	EXPECT_NE(RenderPlayerGhost.find("if(AllowEffects && !InAir && WantOtherDir"), std::string::npos);
	EXPECT_NE(RenderPlayerGhost.find("if(AllowEffects)\n\t\t\t\t\t\tGameClient()->m_Effects.PowerupShine("), std::string::npos);
	EXPECT_NE(RenderPlayerGhost.find("if(AllowEffects && !Focus.m_HideMuzzleEffects &&"), std::string::npos);
}

TEST(QmNewUiMenuGameplayPredictionContract, HammerPredictionDeduplicatesSameTargetAndTick)
{
	const std::string Source = ReadTextFile("src/game/client/prediction/gameworld.cpp");
	const std::string CreateHammerEvent = FunctionBody(Source, "void CGameWorld::CreatePredictedHammerHitEvent(");

	EXPECT_NE(CreateHammerEvent.find("Existing.m_EventId == Event.m_EventId"), std::string::npos);
	EXPECT_NE(CreateHammerEvent.find("Existing.m_Id == Event.m_Id"), std::string::npos);
	EXPECT_NE(CreateHammerEvent.find("Existing.m_Tick == Event.m_Tick"), std::string::npos);
	EXPECT_NE(CreateHammerEvent.find("Existing.m_ExtraInfo == Event.m_ExtraInfo"), std::string::npos);
}

TEST(QmNewUiMenuGameplayPredictionContract, ExtraPredictionWorldDoesNotReplayVisibleEffects)
{
	const std::string Source = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string OnPredictBody = FunctionBody(Source, "void CGameClient::OnPredict()");

	EXPECT_NE(OnPredictBody.find("m_ExtraPredictedWorld.m_PredictedEvents.clear();"), std::string::npos);
	EXPECT_EQ(OnPredictBody.find("HandlePredictedEvents(m_ExtraPredictedWorld.m_GameTick)"), std::string::npos);
}

TEST(QmNewUiMenuGameplayPredictionContract, FastInputKeepsPredictedEventStateOnRegularWorldRestore)
{
	const std::string GameWorldSource = ReadTextFile("src/game/client/prediction/gameworld.cpp");
	const std::string GameClientSource = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string CopyWorldClean = FunctionBody(GameWorldSource, "void CGameWorld::CopyWorldClean(");
	const std::string OnPredict = FunctionBody(GameClientSource, "void CGameClient::OnPredict()");

	EXPECT_NE(CopyWorldClean.find("m_PredictedEvents = pFrom->m_PredictedEvents;"), std::string::npos);
	EXPECT_NE(OnPredict.find("if(Tick <= FinalTickRegular)\n\t\t\tHandlePredictedEvents(Tick);"), std::string::npos);
}

TEST(QmNewUiMenuGameplayPredictionContract, HammerSkinSwapUsesTheActiveProtocolSkinFormat)
{
	const std::string Source = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string HandleHammerSkinSwap = FunctionBody(Source, "void CGameClient::HandleHammerSkinSwap(");

	EXPECT_NE(HandleHammerSkinSwap.find("if(Client()->IsSixup())"), std::string::npos);
	EXPECT_NE(HandleHammerSkinSwap.find("TargetClient.m_aSixup[SourceConnection]"), std::string::npos);
	EXPECT_NE(HandleHammerSkinSwap.find("CSkins7::ms_apSkinVariables[TeeIndex][Part]"), std::string::npos);
	EXPECT_NE(HandleHammerSkinSwap.find("const char *pTargetSkinName = pTargetSkin->Name();"), std::string::npos);
}

TEST(QmNewUiMenuGameplayPredictionContract, HammerHitPredictionMatchingUsesOwnerDistanceAndOneToOneConsumption)
{
	std::vector<CGameWorld::CPredictedEvent> vPredictedEvents;
	CGameWorld::CPredictedEvent Near(NETEVENTTYPE_HAMMERHIT, vec2(100.0f, 100.0f), 3, 100, 4);
	Near.m_Handled = true;
	vPredictedEvents.push_back(Near);
	EXPECT_FALSE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(124.0f, 100.0f), 4, 102, 4)));
	EXPECT_EQ(vPredictedEvents.size(), 1u);
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(132.0f, 100.0f), 3, 102, 4)));
	ASSERT_EQ(vPredictedEvents.size(), 1u);
	EXPECT_TRUE(vPredictedEvents.front().m_ServerConfirmed);
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(132.0f, 100.0f), 3, 102, 4)));

	CGameWorld::CPredictedEvent First(NETEVENTTYPE_HAMMERHIT, vec2(100.0f, 100.0f), 3, 100, 4);
	CGameWorld::CPredictedEvent Second(NETEVENTTYPE_HAMMERHIT, vec2(130.0f, 100.0f), 3, 100, 5);
	First.m_Handled = true;
	Second.m_Handled = true;
	vPredictedEvents = {First, Second};
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(101.0f, 100.0f), 3, 102, 4)));
	EXPECT_EQ(vPredictedEvents.size(), 2u);
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(131.0f, 100.0f), 3, 102, 5)));
	EXPECT_EQ(vPredictedEvents.size(), 2u);

	CGameWorld::CPredictedEvent TargetA(NETEVENTTYPE_HAMMERHIT, vec2(200.0f, 100.0f), 3, 200, 4);
	CGameWorld::CPredictedEvent TargetB(NETEVENTTYPE_HAMMERHIT, vec2(202.0f, 100.0f), 3, 200, 5);
	TargetA.m_Handled = true;
	TargetB.m_Handled = true;
	vPredictedEvents = {TargetA, TargetB};
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(201.0f, 100.0f), 3, 202, 5)));
	EXPECT_EQ(vPredictedEvents.size(), 2u);
	EXPECT_EQ(vPredictedEvents.front().m_ExtraInfo, 4);
	vPredictedEvents.clear();

	CGameWorld::CPredictedEvent Far(NETEVENTTYPE_HAMMERHIT, vec2(100.0f, 100.0f), 3, 100, 4);
	Far.m_Handled = true;
	vPredictedEvents.push_back(Far);
	EXPECT_FALSE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(133.0f, 100.0f), 3, 102, 4)));
	EXPECT_EQ(vPredictedEvents.size(), 1u);
	EXPECT_FALSE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(100.0f, 100.0f), 3, 100 + SERVER_TICK_SPEED + 1, 4)));
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(132.0f, 100.0f), 3, 102, 4)));
	EXPECT_EQ(vPredictedEvents.size(), 1u);

	CGameWorld::CPredictedEvent Future(NETEVENTTYPE_HAMMERHIT, vec2(600.0f, 100.0f), 3, 110, 4);
	Future.m_Handled = true;
	vPredictedEvents.push_back(Future);
	EXPECT_FALSE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(600.0f, 100.0f), 3, 102, 4)));
	EXPECT_EQ(vPredictedEvents.size(), 2u);
	vPredictedEvents.clear();

	vPredictedEvents.clear();
	CGameWorld::CPredictedEvent UnknownOwner(NETEVENTTYPE_HAMMERHIT, vec2(200.0f, 100.0f), 7, 200, 8);
	UnknownOwner.m_Handled = true;
	vPredictedEvents.push_back(UnknownOwner);
	EXPECT_FALSE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(201.0f, 100.0f), -1, 202, 8)));

	CGameWorld::CPredictedEvent AmbiguousA(NETEVENTTYPE_HAMMERHIT, vec2(300.0f, 100.0f), 7, 300, 9);
	CGameWorld::CPredictedEvent AmbiguousB(NETEVENTTYPE_HAMMERHIT, vec2(302.0f, 100.0f), 8, 300, 9);
	AmbiguousA.m_Handled = true;
	AmbiguousB.m_Handled = true;
	vPredictedEvents = {AmbiguousA, AmbiguousB};
	EXPECT_FALSE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(301.0f, 100.0f), -1, 302, 9)));
	EXPECT_EQ(vPredictedEvents.size(), 2u);

	vPredictedEvents.clear();
	CGameWorld::CPredictedEvent Boundary(NETEVENTTYPE_HAMMERHIT, vec2(400.0f, 100.0f), 3, 400, 4);
	Boundary.m_Handled = true;
	vPredictedEvents.push_back(Boundary);
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(432.0f, 100.0f), 3, 402, 4)));
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(432.0f, 100.0f), 3, 402, 4)));

	CGameWorld::CPredictedEvent Earlier(NETEVENTTYPE_HAMMERHIT, vec2(500.0f, 100.0f), 3, 500, 4);
	CGameWorld::CPredictedEvent Later(NETEVENTTYPE_HAMMERHIT, vec2(520.0f, 100.0f), 3, 516, 4);
	Earlier.m_Handled = true;
	Later.m_Handled = true;
	vPredictedEvents = {Earlier, Later};
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(501.0f, 100.0f), 3, 517, 4)));
	EXPECT_EQ(vPredictedEvents.size(), 2u);
	EXPECT_EQ(vPredictedEvents.back().m_Tick, 517);
	vPredictedEvents.clear();
	CGameWorld::CPredictedEvent Single(NETEVENTTYPE_HAMMERHIT, vec2(550.0f, 100.0f), 3, 600, 4);
	Single.m_Handled = true;
	vPredictedEvents.push_back(Single);
	EXPECT_TRUE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(550.0f, 100.0f), 3, 601, 4)));
	EXPECT_FALSE(QmCheckPredictedHammerHitHandled(vPredictedEvents, CGameWorld::CPredictedEvent(NETEVENTTYPE_HAMMERHIT, vec2(550.0f, 100.0f), 3, 603, 4)));
}

TEST(QmNewUiMenuGameplayPredictionContract, HammerHitConsumersUseDeferredServerEvidenceOnly)
{
	const std::string CharacterSource = ReadTextFile("src/game/client/prediction/entities/character.cpp");
	const std::string GameClientSource = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string GameWorldSource = ReadTextFile("src/game/client/prediction/gameworld.cpp");
	const std::string FastPracticeSource = ReadTextFile("src/game/client/components/tclient/fast_practice.cpp");
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/tclient.cpp");
	const std::string InferHammerHit = FunctionBody(GameClientSource, "SQmHammerHitMatch QmInferHammerHit(");
	const std::string FinalizeHammerHitEvents = FunctionBody(GameClientSource, "void CGameClient::FinalizeHammerHitEvents()");
	const std::string OnNewSnapshot = FunctionBody(GameClientSource, "void CGameClient::OnNewSnapshot(bool DummySwapped)");
	const std::string WakeupActions = FunctionBody(TClientSource, "void CTClient::CheckHammerWakeupActions()");

	EXPECT_EQ(CharacterSource.find("CreateHammerHitEvent"), std::string::npos);
	EXPECT_EQ(GameWorldSource.find("HammerHitEvents"), std::string::npos);
	EXPECT_EQ(GameWorldSource.find("BeginHammerHitEventBatch"), std::string::npos);
	EXPECT_EQ(GameClientSource.find("RecordPredictedHammerHits"), std::string::npos);
	EXPECT_EQ(GameClientSource.find("ConfirmPredictedEvent"), std::string::npos);
	EXPECT_EQ(GameClientSource.find("MatchPredictedEvent"), std::string::npos);
	EXPECT_EQ(InferHammerHit.find("m_PredictedWorld"), std::string::npos);
	EXPECT_NE(InferHammerHit.find("QmIsHammerSuperTeam(DDTeam, pGameClient->m_Teams.m_NumDDRaceTeams)"), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("m_HammerHitTracker.Record(Hit)"), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("QmIsHammerWakeupTransition("), std::string::npos);
	EXPECT_NE(FinalizeHammerHitEvents.find("HandleConfirmedHammerHit(Hit);"), std::string::npos);
	EXPECT_NE(GameClientSource.find("const bool Online = Client()->State() == IClient::STATE_ONLINE;"), std::string::npos);
	EXPECT_NE(GameClientSource.find("if(Online)\n\t\tHandleHammerSkinSwap(Hit);"), std::string::npos);
	EXPECT_EQ(FastPracticeSource.find("void CFastPractice::MaybePlayHammerHitEffect(CCharacter *pChar)"), std::string::npos);
	EXPECT_NE(FastPracticeSource.find("GameClient()->HandlePredictedEvents(Tick);"), std::string::npos);
	EXPECT_EQ(FastPracticeSource.find("m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SoundId"), std::string::npos);
	EXPECT_NE(FastPracticeSource.find("if(!GameClient()->m_PredictedWorld.m_WorldConfig.m_IsDDRace)"), std::string::npos);
	EXPECT_EQ(FastPracticeSource.find("HammerHitTracker"), std::string::npos);
	EXPECT_NE(TClientSource.find("FindTargetHitsAtTick("), std::string::npos);
	EXPECT_NE(TClientSource.find("if(!Hit.m_TargetWoke)"), std::string::npos);
	EXPECT_NE(TClientSource.find("CheckHammerWakeupActions();"), std::string::npos);
	EXPECT_NE(TClientSource.find("m_aaComboLastHammerHitSnapshotTick[Dummy][TargetId]"), std::string::npos);
	EXPECT_EQ(GameClientSource.find("QmJellyHammerHitRadius"), std::string::npos);

	const size_t FinalizePos = OnNewSnapshot.find("FinalizeHammerHitEvents();");
	const size_t ComponentSnapshotPos = OnNewSnapshot.find("pComponent->OnNewSnapshot();");
	ASSERT_NE(FinalizePos, std::string::npos);
	ASSERT_NE(ComponentSnapshotPos, std::string::npos);
	EXPECT_LT(FinalizePos, ComponentSnapshotPos);

	const size_t UnspecPos = WakeupActions.find("Client()->SendPackMsg(Input.m_ActiveConnection");
	const size_t CloseChatPos = WakeupActions.find("GameClient()->m_Chat.DisableMode();");
	const size_t SwitchPos = WakeupActions.find("Console()->ExecuteLine(aCommand);");
	ASSERT_NE(UnspecPos, std::string::npos);
	ASSERT_NE(CloseChatPos, std::string::npos);
	ASSERT_NE(SwitchPos, std::string::npos);
	EXPECT_LT(UnspecPos, CloseChatPos);
	EXPECT_LT(CloseChatPos, SwitchPos);
}
