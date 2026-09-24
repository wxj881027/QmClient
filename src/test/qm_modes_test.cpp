#include "test.h"

#include <base/color.h>

#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <generated/protocol.h>

#include <game/client/components/emoticon.h>
#include <game/client/components/jump_hint_utils.h>
#include <game/client/components/qmclient/emoticon_commands.h>
#include <game/client/components/qmclient/emoticon_projectile.h>
#include <game/client/components/qmclient/friend_enter_tracker.h>
#include <game/client/components/qmclient/map_progress.h>
#include <game/client/components/qmclient/markdown_cache_writer.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/components/qmclient/route_start_index.h>
#include <game/client/components/qmclient/translate/translate_ui_settings.h>
#include <game/gamecore.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
	QmMapProgress::CMap MakeProgressLine(int Width)
	{
		QmMapProgress::CMap Map(Width, 1);
		for(int Index = 0; Index < Width; ++Index)
			Map.SetTile(Index, QmMapProgress::MakeTile(Index == 0 ? TILE_START : (Index == Width - 1 ? TILE_FINISH : TILE_AIR)));
		return Map;
	}

	QmMapProgress::SEstimate MoveProgressPlayer(QmMapProgress::CPlayer &Player, const QmMapProgress::CMap &Map, int Index, int TeleCheckpoint = 0)
	{
		Player.Observe(Map.Tile(Index), Index);
		// 小地图也分多次推进，以覆盖换段和回传后重建距离场的行为。
		for(int Step = 0; Step < 128; ++Step)
			Player.Update(Map, Index, TeleCheckpoint, 128);
		return Player.Estimate();
	}
}

TEST(QmMapProgress, DragThroughFreezeUsesRouteLengthAndCanGoBack)
{
	auto Map = MakeProgressLine(11);
	for(int Index = 4; Index <= 6; ++Index)
		Map.SetTile(Index, QmMapProgress::MakeTile(TILE_FREEZE));
	Map.Finalize();
	QmMapProgress::CPlayer Player;
	EXPECT_FLOAT_EQ(MoveProgressPlayer(Player, Map, 0).m_Progress, 0.0f);
	const auto InWater = MoveProgressPlayer(Player, Map, 5);
	ASSERT_TRUE(InWater.m_Valid);
	EXPECT_FLOAT_EQ(InWater.m_Progress, 0.5f);
	EXPECT_FLOAT_EQ(MoveProgressPlayer(Player, Map, 7).m_Progress, 0.7f);
	EXPECT_FLOAT_EQ(MoveProgressPlayer(Player, Map, 3).m_Progress, 0.3f);
	EXPECT_FLOAT_EQ(MoveProgressPlayer(Player, Map, 10).m_Progress, 1.0f);
	EXPECT_FLOAT_EQ(MoveProgressPlayer(Player, Map, 0).m_Progress, 0.0f);
}

TEST(QmMapProgress, SafeDetourStillWinsButPenaltyDoesNotBecomeLength)
{
	QmMapProgress::CMap Map(5, 2);
	for(int Index = 0; Index < 10; ++Index)
		Map.SetTile(Index, QmMapProgress::MakeTile(TILE_AIR));
	Map.SetTile(0, QmMapProgress::MakeTile(TILE_START));
	Map.SetTile(4, QmMapProgress::MakeTile(TILE_FINISH));
	Map.SetTile(1, QmMapProgress::MakeTile(TILE_FREEZE));
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_FREEZE));
	Map.Finalize();
	QmMapProgress::CField Field;
	Field.Start(Map, 0);
	while(!Field.Complete())
		Field.Step(Map, 1);
	EXPECT_EQ(Field.Length(0), 6);
	EXPECT_EQ(Field.Next(0), 5);
}

TEST(QmMapProgress, DeepFreezeIsPassableButDeathAndSolidAreNot)
{
	for(const int Tile : {TILE_DFREEZE, TILE_LFREEZE, TILE_DEATH, TILE_SOLID, TILE_NOHOOK})
	{
		auto Map = MakeProgressLine(5);
		Map.SetTile(2, QmMapProgress::MakeTile(Tile));
		Map.Finalize();
		QmMapProgress::CField Field;
		Field.Start(Map, 0);
		while(!Field.Complete())
			Field.Step(Map, 1);
		EXPECT_EQ(Field.Length(0), Tile == TILE_DFREEZE || Tile == TILE_LFREEZE ? 4 : -1);
	}
}

TEST(QmMapProgress, DirectTeleportLinksRoomsWithoutCountingItsWorldDistance)
{
	auto Map = MakeProgressLine(9);
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELEINEVIL, 17));
	Map.SetTile(3, QmMapProgress::MakeTile(TILE_SOLID));
	Map.SetTile(6, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELEOUT, 17));
	Map.Finalize();
	QmMapProgress::CField Field;
	Field.Start(Map, 0);
	while(!Field.Complete())
		Field.Step(Map, 1);
	EXPECT_EQ(Field.Length(0), 4);
	EXPECT_EQ(Field.Next(2), 6);
	EXPECT_EQ(Field.Length(2), Field.Length(6));
}

TEST(QmMapProgress, CheckpointReturnUsesRecordedNumberAndFallsBackToEarlierExit)
{
	auto Map = MakeProgressLine(9);
	Map.SetTile(1, QmMapProgress::MakeTile(ENTITY_OFFSET + ENTITY_SPAWN));
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELECHECKINEVIL, 0));
	Map.SetTile(3, QmMapProgress::MakeTile(TILE_SOLID));
	Map.SetTile(6, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELECHECKOUT, 2));
	Map.Finalize();
	QmMapProgress::CField Field;
	Field.Start(Map, 4);
	while(!Field.Complete())
		Field.Step(Map, 1);
	EXPECT_EQ(Field.Next(2), 6);
	EXPECT_EQ(Field.Length(0), 4);
	Field.Start(Map, 1);
	while(!Field.Complete())
		Field.Step(Map, 1);
	EXPECT_EQ(Field.Length(0), -1);
}

TEST(QmMapProgress, CheckpointReturnWithoutExitGoesToSpawn)
{
	auto Map = MakeProgressLine(9);
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELECHECKIN, 0));
	Map.SetTile(3, QmMapProgress::MakeTile(TILE_SOLID));
	Map.SetTile(6, QmMapProgress::MakeTile(ENTITY_OFFSET + ENTITY_SPAWN));
	Map.Finalize();
	QmMapProgress::CField Field;
	Field.Start(Map, 0);
	while(!Field.Complete())
		Field.Step(Map, 1);
	EXPECT_EQ(Field.Next(2), 6);
	EXPECT_EQ(Field.Length(0), 4);
}

TEST(QmMapProgress, TimeCheckpointsDivideStagesAndBacktrackingDecreasesProgress)
{
	auto Map = MakeProgressLine(11);
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_AIR, TILE_TIME_CHECKPOINT_FIRST));
	Map.SetTile(8, QmMapProgress::MakeTile(TILE_TIME_CHECKPOINT_FIRST + 1));
	Map.Finalize();
	ASSERT_EQ(Map.CheckpointCount(), 2);
	QmMapProgress::CPlayer Player;
	MoveProgressPlayer(Player, Map, 0);
	EXPECT_NEAR(MoveProgressPlayer(Player, Map, 2).m_Progress, 1.0f / 3.0f, 0.001f);
	EXPECT_NEAR(MoveProgressPlayer(Player, Map, 5).m_Progress, 0.5f, 0.001f);
	EXPECT_NEAR(MoveProgressPlayer(Player, Map, 8).m_Progress, 2.0f / 3.0f, 0.001f);
	EXPECT_NEAR(MoveProgressPlayer(Player, Map, 9).m_Progress, 5.0f / 6.0f, 0.001f);
	MoveProgressPlayer(Player, Map, 8);
	EXPECT_NEAR(MoveProgressPlayer(Player, Map, 7).m_Progress, 11.0f / 18.0f, 0.001f);
	EXPECT_NEAR(MoveProgressPlayer(Player, Map, 3).m_Progress, 7.0f / 18.0f, 0.001f);
	MoveProgressPlayer(Player, Map, 2);
	EXPECT_NEAR(MoveProgressPlayer(Player, Map, 1).m_Progress, 1.0f / 6.0f, 0.001f);
}

TEST(QmMapProgress, MissingAndReversedCheckpointOrderFallBackToWholeMap)
{
	for(const bool Reversed : {false, true})
	{
		auto Map = MakeProgressLine(11);
		Map.SetTile(2, QmMapProgress::MakeTile(TILE_TIME_CHECKPOINT_FIRST + 1));
		if(Reversed)
			Map.SetTile(8, QmMapProgress::MakeTile(TILE_TIME_CHECKPOINT_FIRST));
		Map.Finalize();
		QmMapProgress::CPlayer Player;
		MoveProgressPlayer(Player, Map, 0);
		const auto Estimate = MoveProgressPlayer(Player, Map, 5);
		ASSERT_TRUE(Estimate.m_Valid);
		EXPECT_FLOAT_EQ(Estimate.m_Progress, 0.5f);
	}
}

TEST(QmMapProgress, MidRunEnableCanEstimateWithoutHavingSeenStart)
{
	auto Map = MakeProgressLine(11);
	Map.Finalize();
	QmMapProgress::CPlayer Player;
	const auto Estimate = MoveProgressPlayer(Player, Map, 6);
	ASSERT_TRUE(Estimate.m_Valid);
	EXPECT_FLOAT_EQ(Estimate.m_Progress, 0.6f);
}

TEST(QmMapProgress, MainAndDummyCheckpointStatesAreIndependent)
{
	auto Map = MakeProgressLine(9);
	Map.SetTile(1, QmMapProgress::MakeTile(ENTITY_OFFSET + ENTITY_SPAWN));
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELECHECKIN, 0));
	Map.SetTile(3, QmMapProgress::MakeTile(TILE_SOLID));
	Map.SetTile(6, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, TILE_TELECHECKOUT, 2));
	Map.Finalize();
	QmMapProgress::CPlayer Main;
	QmMapProgress::CPlayer Dummy;
	EXPECT_TRUE(MoveProgressPlayer(Main, Map, 1, 2).m_Valid);
	EXPECT_FALSE(MoveProgressPlayer(Dummy, Map, 1, 0).m_Valid);
	EXPECT_FALSE(MoveProgressPlayer(Main, Map, 1, 0).m_Valid);
	EXPECT_TRUE(MoveProgressPlayer(Dummy, Map, 1, 2).m_Valid);
}

TEST(QmMapProgress, OneWayRestrictionsAreRespectedAndEmptyMapsStayUnknown)
{
	auto Map = MakeProgressLine(5);
	Map.SetTile(2, QmMapProgress::MakeTile(TILE_AIR, TILE_AIR, 0, 0, CANTMOVE_RIGHT));
	Map.Finalize();
	QmMapProgress::CField Field;
	Field.Start(Map, 0);
	while(!Field.Complete())
		Field.Step(Map, 1);
	EXPECT_EQ(Field.Length(0), -1);
	QmMapProgress::CMap Empty(1, 1);
	Empty.Finalize();
	QmMapProgress::CPlayer Player;
	EXPECT_FALSE(MoveProgressPlayer(Player, Empty, 0).m_Valid);
}

static void ExpectColorNear(const ColorRGBA &Color, const ColorRGBA &Expected)
{
	EXPECT_NEAR(Color.r, Expected.r, 0.02f);
	EXPECT_NEAR(Color.g, Expected.g, 0.02f);
	EXPECT_NEAR(Color.b, Expected.b, 0.02f);
	EXPECT_NEAR(Color.a, Expected.a, 0.02f);
}

TEST(QmPredictionMode, UpdatePredictionDoesNotOverrideClientOptIn)
{
	const std::string Source = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const size_t FunctionStart = Source.find("void CGameClient::UpdatePrediction()");
	ASSERT_NE(FunctionStart, std::string::npos);
	const size_t FunctionEnd = Source.find("\nvoid CGameClient::", FunctionStart + 1);
	ASSERT_NE(FunctionEnd, std::string::npos);
	const std::string FunctionBody = Source.substr(FunctionStart, FunctionEnd - FunctionStart);
	const std::string Assignment = "m_GameWorld.m_WorldConfig.m_PredictEvents =";
	const size_t FirstAssignment = FunctionBody.find(Assignment);
	ASSERT_NE(FirstAssignment, std::string::npos);
	EXPECT_EQ(FunctionBody.find(Assignment, FirstAssignment + Assignment.size()), std::string::npos);
	EXPECT_NE(FunctionBody.find("m_GameWorld.m_WorldConfig.m_PredictEvents = g_Config.m_ClPredictEvents && m_GameInfo.m_PredictEvents;"), std::string::npos);
}

TEST(QmGoresMode, ManualGuideRevealOverridesAutomaticGuideHiding)
{
	EXPECT_TRUE(ShouldHideGoresGuide(true, true, false));
	EXPECT_FALSE(ShouldHideGoresGuide(true, true, true));
	EXPECT_FALSE(ShouldHideGoresGuide(true, false, false));
	EXPECT_FALSE(ShouldHideGoresGuide(false, true, false));
}

TEST(QmGoresMode, DebugRouteDoesNotUseHideGuidesGate)
{
	EXPECT_TRUE(ShouldRenderGoresDebugRoute(true, true, true));
	EXPECT_FALSE(ShouldRenderGoresDebugRoute(false, true, true));
	EXPECT_FALSE(ShouldRenderGoresDebugRoute(true, false, true));
	EXPECT_FALSE(ShouldRenderGoresDebugRoute(true, true, false));
}

TEST(QmGoresMode, MovingWaterTilesRequireAxiomOrGoresContext)
{
	EXPECT_TRUE(ShouldEnableQmMovingWaterTiles("Gores", "", "", ""));
	EXPECT_TRUE(ShouldEnableQmMovingWaterTiles("", "DDNet Gores", "", ""));
	EXPECT_TRUE(ShouldEnableQmMovingWaterTiles("", "", "axiom-cn", ""));
	EXPECT_TRUE(ShouldEnableQmMovingWaterTiles("", "", "", "Axiom"));

	EXPECT_FALSE(ShouldEnableQmMovingWaterTiles("DDRaceNetwork", "DDNet", "kog", "DDNet"));
	EXPECT_FALSE(ShouldEnableQmMovingWaterTiles(nullptr, nullptr, nullptr, nullptr));
}

TEST(QmLocalSkinSource, DdnetAndAxiomKeepTeeMenuOverride)
{
	EXPECT_FALSE(ShouldUseServerControlledLocalSkin("DDRaceNetwork", "", "", ""));
	EXPECT_FALSE(ShouldUseServerControlledLocalSkin("ddnet", "", "", ""));
	EXPECT_FALSE(ShouldUseServerControlledLocalSkin("", "DDNet", "", ""));
	EXPECT_FALSE(ShouldUseServerControlledLocalSkin("Gores", "Gores", "axiom-cn", ""));
	EXPECT_FALSE(ShouldUseServerControlledLocalSkin("Gores", "Gores", "", "Axiom"));
}

TEST(QmLocalSkinSource, OtherServersUseServerControlledSkin)
{
	EXPECT_TRUE(ShouldUseServerControlledLocalSkin("InfClass", "InfClass", "", ""));
	EXPECT_TRUE(ShouldUseServerControlledLocalSkin("MMO", "MMO", "", ""));
	EXPECT_TRUE(ShouldUseServerControlledLocalSkin("Gores", "Gores", "kog", "KoG"));
	EXPECT_TRUE(ShouldUseServerControlledLocalSkin(nullptr, nullptr, nullptr, nullptr));
}

TEST(LocalSkinSource, DemoPlaybackUsesRecordedSnapshotForEitherLocalConnection)
{
	constexpr int MainClientId = 7;
	constexpr int DummyClientId = 19;

	EXPECT_EQ(ResolveLocalSkinConfigIndex(true, MainClientId, MainClientId, DummyClientId), -1);
	EXPECT_EQ(ResolveLocalSkinConfigIndex(true, DummyClientId, MainClientId, DummyClientId), -1);
}

TEST(LocalSkinSource, OnlinePlayUsesMatchingLocalConfiguration)
{
	constexpr int MainClientId = 7;
	constexpr int DummyClientId = 19;

	EXPECT_EQ(ResolveLocalSkinConfigIndex(false, MainClientId, MainClientId, DummyClientId), 0);
	EXPECT_EQ(ResolveLocalSkinConfigIndex(false, DummyClientId, MainClientId, DummyClientId), 1);
	EXPECT_EQ(ResolveLocalSkinConfigIndex(false, 23, MainClientId, DummyClientId), -1);
	EXPECT_EQ(ResolveLocalSkinConfigIndex(false, -1, -1, -1), -1);
}

TEST(QmGoresMode, LinkedFastInputDirectlyFollowsGoresMode)
{
	bool Changed = false;
	EXPECT_TRUE(ApplyQmGoresLinkedConfig(true, true, false, Changed));
	EXPECT_TRUE(Changed);

	EXPECT_FALSE(ApplyQmGoresLinkedConfig(false, true, true, Changed));
	EXPECT_TRUE(Changed);

	EXPECT_TRUE(ApplyQmGoresLinkedConfig(true, true, true, Changed));
	EXPECT_FALSE(Changed);
}

TEST(QmGoresMode, UnlinkedFastInputConfigIsNotChanged)
{
	bool Changed = false;
	EXPECT_TRUE(ApplyQmGoresLinkedConfig(false, false, true, Changed));
	EXPECT_FALSE(Changed);

	EXPECT_FALSE(ApplyQmGoresLinkedConfig(true, false, false, Changed));
	EXPECT_FALSE(Changed);
}

TEST(QmFastInputMode, NormalizesLegacyBestModesToFastInput)
{
	EXPECT_EQ(QmFastInputNormalizedMode(0), 0);
	EXPECT_EQ(QmFastInputNormalizedMode(1), 0);
	EXPECT_EQ(QmFastInputNormalizedMode(2), 0);
	EXPECT_EQ(QmFastInputNormalizedMode(3), 0);
	EXPECT_EQ(QmFastInputNormalizedMode(4), 4);
}

TEST(QmFastInputMode, ComputesFastAndSaikoOffsets)
{
	SQmFastInputSettings Settings;
	Settings.m_Enabled = true;

	Settings.m_Mode = 0;
	Settings.m_FastAmountMs = 40;
	EXPECT_FLOAT_EQ(QmEffectiveFastInputOffsetTicks(Settings), 2.0f);

	// 历史 Best(3) 已删除，必须回落到 Fast 的偏移。
	Settings.m_Mode = 3;
	EXPECT_FLOAT_EQ(QmEffectiveFastInputOffsetTicks(Settings), 2.0f);

	Settings.m_Mode = 4;
	Settings.m_SaikoPlusAmount = 175;
	EXPECT_FLOAT_EQ(QmEffectiveFastInputOffsetTicks(Settings), 1.75f);
}

TEST(QmFastInputMode, PredictionTicksUseSaikoPlusExtraLocalTickOnly)
{
	EXPECT_EQ(QmFastInputPredictionTicks(0.01f, 0), 1);
	EXPECT_EQ(QmFastInputPredictionTicks(1.25f, 0), 2);
	EXPECT_EQ(QmFastInputPredictionTicks(1.25f, 4), 3);
	EXPECT_EQ(QmFastInputPredictionTicksOthers(1.25f, 4), 2);
}

TEST(QmFastInputMode, AppliesOffsetWithoutNegativeIntra)
{
	int Tick = 100;
	float Intra = 0.20f;
	QmApplyFastInputOffset(1.25f, Tick, Intra);
	EXPECT_EQ(Tick, 101);
	EXPECT_FLOAT_EQ(Intra, 0.45f);
}

TEST(QmFastInputMode, ChoosesOthersToggleByMode)
{
	EXPECT_FALSE(QmEffectiveFastInputOthers(false, 0, true, true));
	EXPECT_TRUE(QmEffectiveFastInputOthers(true, 0, true, false));
	EXPECT_TRUE(QmEffectiveFastInputOthers(true, 3, true, false));
	EXPECT_TRUE(QmEffectiveFastInputOthers(true, 4, false, true));
	EXPECT_FALSE(QmEffectiveFastInputOthers(true, 3, false, true));
	EXPECT_FALSE(QmEffectiveFastInputOthers(true, 4, true, false));
}

TEST(QmFastInputMode, MarginUsesLargestFastInputContribution)
{
	SQmFastInputSettings Settings;
	Settings.m_Enabled = true;
	Settings.m_BasePredictionMarginMs = 10;

	Settings.m_Mode = 0;
	Settings.m_FastAmountMs = 40;
	EXPECT_EQ(QmFastInputBasePredictionMarginMs(Settings), 40);

	// 历史 Best(3) 已删除，边距同样回落到 Fast 的贡献值。
	Settings.m_Mode = 3;
	EXPECT_EQ(QmFastInputBasePredictionMarginMs(Settings), 40);

	Settings.m_Mode = 4;
	Settings.m_SaikoPlusAmount = 175;
	EXPECT_EQ(QmFastInputBasePredictionMarginMs(Settings), 35);
}

TEST(QmFastInputMode, AutoPredictionMarginKeepsStableBase)
{
	EXPECT_EQ(QmComputeAutoPredictionMargin(10, 0.0f, 10.0f, 10.0f, 0.0f, false), 10);
}

TEST(QmFastInputMode, AutoPredictionMarginAddsLatencyJitterAndConnectionProtection)
{
	EXPECT_EQ(QmComputeAutoPredictionMargin(10, 70.0f, 10.0f, 10.0f, 0.0f, false), 20);
	EXPECT_EQ(QmComputeAutoPredictionMargin(10, 0.0f, 10.0f, 10.0f, 14.0f, false), 19);
	EXPECT_EQ(QmComputeAutoPredictionMargin(10, 0.0f, 10.0f, 10.0f, 0.0f, true), 20);
}

TEST(QmFastInputMode, AutoPredictionMarginClampsToSupportedRange)
{
	EXPECT_EQ(QmComputeAutoPredictionMargin(0, 0.0f, 0.0f, 0.0f, 0.0f, false), 1);
	EXPECT_EQ(QmComputeAutoPredictionMargin(500, 0.0f, 0.0f, 0.0f, 0.0f, false), 300);
}

TEST(QmGoresMode, ActiveGoresClearsDummyHammerState)
{
	bool Changed = false;
	EXPECT_EQ(ApplyQmGoresDummyHammerConfig(true, 1, Changed), 0);
	EXPECT_TRUE(Changed);

	EXPECT_EQ(ApplyQmGoresDummyHammerConfig(true, 0, Changed), 0);
	EXPECT_FALSE(Changed);

	EXPECT_EQ(ApplyQmGoresDummyHammerConfig(false, 1, Changed), 1);
	EXPECT_FALSE(Changed);
}

TEST(QmGoresMode, DummyHammerOverrideRestoresOnlyAutomaticChanges)
{
	SQmConfigOverrideState State;
	bool Changed = false;
	EXPECT_EQ(ApplyQmGoresDummyHammerOverride(State, true, true, 1, Changed), 0);
	EXPECT_TRUE(Changed);
	EXPECT_EQ(ApplyQmGoresDummyHammerOverride(State, true, true, 0, Changed), 0);
	EXPECT_FALSE(Changed);
	EXPECT_EQ(ApplyQmGoresDummyHammerOverride(State, false, true, 0, Changed), 1);
	EXPECT_TRUE(Changed);

	State = {};
	EXPECT_EQ(ApplyQmGoresDummyHammerOverride(State, true, true, 1, Changed), 0);
	EXPECT_EQ(ApplyQmGoresDummyHammerOverride(State, true, true, 1, Changed), 1);
	EXPECT_FALSE(Changed);
	EXPECT_EQ(ApplyQmGoresDummyHammerOverride(State, false, true, 1, Changed), 1);
	EXPECT_FALSE(Changed);
}

TEST(QmGoresMode, HammerWakeupRequiresHeldHammerAndExternalWakeup)
{
	EXPECT_TRUE(ShouldTriggerQmGoresHammerWakeup(true, true, true));
	EXPECT_FALSE(ShouldTriggerQmGoresHammerWakeup(false, true, true));
	EXPECT_FALSE(ShouldTriggerQmGoresHammerWakeup(true, false, true));
	EXPECT_FALSE(ShouldTriggerQmGoresHammerWakeup(true, true, false));
}

TEST(QmGoresMode, KeepsHammerRequestWhileFrozen)
{
	EXPECT_TRUE(ShouldKeepQmGoresHammerInFreeze(true, true, true));
	EXPECT_FALSE(ShouldKeepQmGoresHammerInFreeze(false, true, true));
	EXPECT_FALSE(ShouldKeepQmGoresHammerInFreeze(true, false, true));
	EXPECT_FALSE(ShouldKeepQmGoresHammerInFreeze(true, true, false));
}

TEST(QmGoresMode, HammerWakeupFireStateCreatesNewPressWhileHeld)
{
	EXPECT_EQ(QmGoresHammerWakeupFireState(0), 1);
	EXPECT_EQ(QmGoresHammerWakeupFireState(1), 3);
	EXPECT_EQ(QmGoresHammerWakeupFireState(2), 3);
	EXPECT_EQ(QmGoresHammerWakeupFireState(3), 5);
}

TEST(QmGoresMode, HammerWakeupReleaseClearsOnlyPendingAutomaticPress)
{
	EXPECT_TRUE(ShouldReleaseQmGoresHammerWakeupFire(true, 1));
	EXPECT_TRUE(ShouldReleaseQmGoresHammerWakeupFire(true, 3));
	EXPECT_FALSE(ShouldReleaseQmGoresHammerWakeupFire(false, 1));
	EXPECT_FALSE(ShouldReleaseQmGoresHammerWakeupFire(true, 2));

	EXPECT_EQ(QmGoresHammerWakeupReleaseFireState(1), 2);
	EXPECT_EQ(QmGoresHammerWakeupReleaseFireState(3), 4);
}

TEST(QmGoresMode, RestoreWeaponAfterHammerUsesRecordedWeapon)
{
	EXPECT_EQ(GoresRestoreWeaponAfterHammer(WEAPON_LASER, true), WEAPON_LASER);
	EXPECT_EQ(GoresRestoreWeaponAfterHammer(WEAPON_GRENADE, true), WEAPON_GRENADE);
	EXPECT_EQ(GoresRestoreWeaponAfterHammer(WEAPON_GUN, false), WEAPON_GUN);
}

TEST(QmGoresMode, FireKeydownPulseRequiresActiveCycleAndNonHammerWeapon)
{
	EXPECT_TRUE(ShouldPulseGoresHammerOnFire(true, true, false, false));
	EXPECT_FALSE(ShouldPulseGoresHammerOnFire(false, true, false, false));
	EXPECT_FALSE(ShouldPulseGoresHammerOnFire(true, false, false, false));
	EXPECT_FALSE(ShouldPulseGoresHammerOnFire(true, true, true, false));
	EXPECT_FALSE(ShouldPulseGoresHammerOnFire(true, true, false, true));
}

TEST(QmGoresMode, RestoresRecordedWeaponEvenWhenTwoWeaponCycleIsInactive)
{
	EXPECT_TRUE(ShouldRestoreGoresWeaponAfterHammer(true, true));
	EXPECT_FALSE(ShouldRestoreGoresWeaponAfterHammer(false, true));
	EXPECT_FALSE(ShouldRestoreGoresWeaponAfterHammer(true, false));
}

TEST(QmNameplateHookStrongWeak, ScopeFiltersExpectedPlayers)
{
	EXPECT_TRUE(ShouldShowQmHookStrongWeakScope(QM_HOOK_STRONG_WEAK_SCOPE_SELF, true, false, false));
	EXPECT_FALSE(ShouldShowQmHookStrongWeakScope(QM_HOOK_STRONG_WEAK_SCOPE_SELF, false, true, false));

	EXPECT_FALSE(ShouldShowQmHookStrongWeakScope(QM_HOOK_STRONG_WEAK_SCOPE_OTHERS, true, false, false));
	EXPECT_TRUE(ShouldShowQmHookStrongWeakScope(QM_HOOK_STRONG_WEAK_SCOPE_OTHERS, false, true, false));
	EXPECT_TRUE(ShouldShowQmHookStrongWeakScope(QM_HOOK_STRONG_WEAK_SCOPE_OTHERS, false, false, true));

	EXPECT_TRUE(ShouldShowQmHookStrongWeakScope(QM_HOOK_STRONG_WEAK_SCOPE_STRONG, false, true, false));
	EXPECT_FALSE(ShouldShowQmHookStrongWeakScope(QM_HOOK_STRONG_WEAK_SCOPE_STRONG, false, false, true));

	EXPECT_FALSE(ShouldShowQmHookStrongWeakScope(QM_HOOK_STRONG_WEAK_SCOPE_WEAK, false, true, false));
	EXPECT_TRUE(ShouldShowQmHookStrongWeakScope(QM_HOOK_STRONG_WEAK_SCOPE_WEAK, false, false, true));

	EXPECT_TRUE(ShouldShowQmHookStrongWeakScope(QM_HOOK_STRONG_WEAK_SCOPE_ALL, true, false, false));
	EXPECT_TRUE(ShouldShowQmHookStrongWeakScope(QM_HOOK_STRONG_WEAK_SCOPE_ALL, false, true, false));
	EXPECT_FALSE(ShouldShowQmHookStrongWeakScope(99, false, true, false));
}

TEST(QmNameplateNameScope, OwnCharactersRespectCurrentAndLocalScopes)
{
	// 当前：只有当前操控角色显示自己的昵称（= 旧 cl_nameplates_own 行为）。
	EXPECT_TRUE(ShouldShowQmNameplateName(QM_NAMEPLATE_SHOW_SCOPE_CURRENT, true, true));
	// 当前：分身（本机但非当前角色）不显示。
	EXPECT_FALSE(ShouldShowQmNameplateName(QM_NAMEPLATE_SHOW_SCOPE_CURRENT, false, true));
	// 当前 + 本地：主号与分身都显示。
	EXPECT_TRUE(ShouldShowQmNameplateName(QM_NAMEPLATE_SHOW_SCOPE_LOCAL, false, true));
	// 本地 + 他人：当前操控角色不算在内。
	EXPECT_FALSE(ShouldShowQmNameplateName(QM_NAMEPLATE_SHOW_SCOPE_OTHERS_LOCAL, true, true));
	// 他人：只看别人，本机角色一律不显示。
	EXPECT_FALSE(ShouldShowQmNameplateName(QM_NAMEPLATE_SHOW_SCOPE_OTHERS, true, true));
	EXPECT_FALSE(ShouldShowQmNameplateName(QM_NAMEPLATE_SHOW_SCOPE_OTHERS, false, true));
}

TEST(QmNameplateNameScope, OtherPlayersRespectOthersAndAllScopes)
{
	// 他人：任何非本机玩家都显示（= 旧 cl_nameplates 行为）。
	EXPECT_TRUE(ShouldShowQmNameplateName(QM_NAMEPLATE_SHOW_SCOPE_OTHERS, false, false));
	// 本地 + 他人：非本机玩家同样显示。
	EXPECT_TRUE(ShouldShowQmNameplateName(QM_NAMEPLATE_SHOW_SCOPE_OTHERS_LOCAL, false, false));
	// 全体：三类玩家全显示。
	EXPECT_TRUE(ShouldShowQmNameplateName(QM_NAMEPLATE_SHOW_SCOPE_ALL, false, false));
	// 只覆盖本机角色的档位不能显示别人。
	EXPECT_FALSE(ShouldShowQmNameplateName(QM_NAMEPLATE_SHOW_SCOPE_CURRENT, false, false));
	EXPECT_FALSE(ShouldShowQmNameplateName(QM_NAMEPLATE_SHOW_SCOPE_LOCAL, false, false));
	// 关：谁都不显示；越界档位按关闭处理。
	EXPECT_FALSE(ShouldShowQmNameplateName(QM_NAMEPLATE_SHOW_SCOPE_OFF, true, true));
	EXPECT_FALSE(ShouldShowQmNameplateName(QM_NAMEPLATE_SHOW_SCOPE_OFF, false, false));
	EXPECT_FALSE(ShouldShowQmNameplateName(99, true, true));
	EXPECT_FALSE(ShouldShowQmNameplateName(99, false, false));
}

TEST(QmNameplateTextEffects, PlayingScopeSupportsSelfOthersFriendsAndAll)
{
	EXPECT_TRUE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_SELF, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OFF, QM_NAMEPLATE_TEXT_DEMO_MODE_OFF, -1, false, false, true, false, false, 0));
	EXPECT_FALSE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_SELF, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OFF, QM_NAMEPLATE_TEXT_DEMO_MODE_OFF, -1, false, false, false, true, false, 1));

	EXPECT_TRUE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_OTHERS, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OFF, QM_NAMEPLATE_TEXT_DEMO_MODE_OFF, -1, false, false, false, false, false, 2));
	EXPECT_FALSE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_OTHERS, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OFF, QM_NAMEPLATE_TEXT_DEMO_MODE_OFF, -1, false, false, true, false, false, 0));

	EXPECT_TRUE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_FRIENDS, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OFF, QM_NAMEPLATE_TEXT_DEMO_MODE_OFF, -1, false, false, false, true, false, 3));
	EXPECT_FALSE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_FRIENDS, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OFF, QM_NAMEPLATE_TEXT_DEMO_MODE_OFF, -1, false, false, true, false, false, 0));

	EXPECT_TRUE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_SELF_FRIENDS, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OFF, QM_NAMEPLATE_TEXT_DEMO_MODE_OFF, -1, false, false, true, false, false, 0));
	EXPECT_TRUE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_SELF_FRIENDS, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OFF, QM_NAMEPLATE_TEXT_DEMO_MODE_OFF, -1, false, false, false, true, false, 3));
	EXPECT_FALSE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_SELF_FRIENDS, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OFF, QM_NAMEPLATE_TEXT_DEMO_MODE_OFF, -1, false, false, false, false, false, 4));

	EXPECT_TRUE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_ALL, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OFF, QM_NAMEPLATE_TEXT_DEMO_MODE_OFF, -1, false, false, false, false, false, 4));
}

TEST(QmNameplateTextEffects, SpectateScopeDoesNotUsePlayingScope)
{
	EXPECT_TRUE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_OFF, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_TARGET, QM_NAMEPLATE_TEXT_DEMO_MODE_OFF, -1, false, true, false, false, true, 7));
	EXPECT_FALSE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_ALL, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OFF, QM_NAMEPLATE_TEXT_DEMO_MODE_OFF, -1, false, true, false, false, true, 7));

	EXPECT_TRUE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_OFF, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OTHERS, QM_NAMEPLATE_TEXT_DEMO_MODE_OFF, -1, false, true, false, false, false, 8));
	EXPECT_FALSE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_OFF, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OTHERS, QM_NAMEPLATE_TEXT_DEMO_MODE_OFF, -1, false, true, false, false, true, 7));

	EXPECT_TRUE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_OFF, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_TARGET_FRIENDS, QM_NAMEPLATE_TEXT_DEMO_MODE_OFF, -1, false, true, false, true, false, 8));
	EXPECT_TRUE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_OFF, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_TARGET_FRIENDS, QM_NAMEPLATE_TEXT_DEMO_MODE_OFF, -1, false, true, false, false, true, 7));
}

TEST(QmNameplateTextEffects, DemoModesOverridePlayingAndSpectateScopes)
{
	EXPECT_FALSE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_ALL, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_ALL, QM_NAMEPLATE_TEXT_DEMO_MODE_OFF, 5, true, true, true, true, true, 5));

	EXPECT_TRUE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_OFF, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OFF, QM_NAMEPLATE_TEXT_DEMO_MODE_SMART, -1, true, true, false, false, true, 5));
	EXPECT_FALSE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_ALL, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_ALL, QM_NAMEPLATE_TEXT_DEMO_MODE_SMART, -1, true, true, false, false, false, 6));

	EXPECT_TRUE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_OFF, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OFF, QM_NAMEPLATE_TEXT_DEMO_MODE_MANUAL_TARGET, 5, true, true, false, false, false, 5));
	EXPECT_FALSE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_ALL, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_ALL, QM_NAMEPLATE_TEXT_DEMO_MODE_MANUAL_TARGET, 5, true, true, true, true, true, 6));

	EXPECT_TRUE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_FRIENDS, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_OFF, QM_NAMEPLATE_TEXT_DEMO_MODE_MANUAL_SCOPE, -1, true, true, false, true, false, 6));
	EXPECT_FALSE(ShouldUseQmNameplateTextEffects(QM_NAMEPLATE_TEXT_PLAYING_SCOPE_FRIENDS, QM_NAMEPLATE_TEXT_SPECTATE_SCOPE_ALL, QM_NAMEPLATE_TEXT_DEMO_MODE_MANUAL_SCOPE, -1, true, true, false, false, true, 5));
}

TEST(QmGoresMode, BudgetedWorkConsumesAtMostBudget)
{
	int Cursor = 0;
	EXPECT_TRUE(ConsumeQmBudgetedWork(Cursor, 10, 3));
	EXPECT_EQ(Cursor, 3);

	EXPECT_TRUE(ConsumeQmBudgetedWork(Cursor, 10, 4));
	EXPECT_EQ(Cursor, 7);

	EXPECT_FALSE(ConsumeQmBudgetedWork(Cursor, 10, 8));
	EXPECT_EQ(Cursor, 10);
}

TEST(QmGoresMode, BudgetedWorkDoesNotAdvanceWithoutPositiveBudget)
{
	int Cursor = 2;
	EXPECT_TRUE(ConsumeQmBudgetedWork(Cursor, 5, 0));
	EXPECT_EQ(Cursor, 2);

	EXPECT_TRUE(ConsumeQmBudgetedWork(Cursor, 5, -4));
	EXPECT_EQ(Cursor, 2);

	EXPECT_FALSE(ConsumeQmBudgetedWork(Cursor, 2, 10));
	EXPECT_EQ(Cursor, 2);
}

TEST(QmConfigOverride, ConfigOverrideRestoresOnlyAutoHiddenValues)
{
	SQmConfigOverrideState State;
	bool Changed = false;

	int Value = ApplyQmConfigOverride(State, true, 1, 0, Changed);
	EXPECT_TRUE(Changed);
	EXPECT_EQ(Value, 0);
	EXPECT_TRUE(State.m_WasActive);
	EXPECT_EQ(State.m_SavedValue, 1);

	Value = ApplyQmConfigOverride(State, false, 0, 0, Changed);
	EXPECT_TRUE(Changed);
	EXPECT_EQ(Value, 1);
	EXPECT_FALSE(State.m_WasActive);
}

TEST(QmConfigOverride, ConfigOverrideKeepsUserChangesMadeWhileActive)
{
	SQmConfigOverrideState State;
	bool Changed = false;

	EXPECT_EQ(ApplyQmConfigOverride(State, true, 1, 0, Changed), 0);
	EXPECT_TRUE(Changed);

	const int UserChangedValue = 2;
	EXPECT_EQ(ApplyQmConfigOverride(State, false, UserChangedValue, 0, Changed), UserChangedValue);
	EXPECT_FALSE(Changed);
	EXPECT_FALSE(State.m_WasActive);
}

TEST(QmMapProgress, IndependentMapProgressUsesItsOwnToggleAndBottomStyle)
{
	EXPECT_FALSE(ShouldRenderMapProgressBar(false, 0, false, true));
	EXPECT_TRUE(ShouldRenderMapProgressBar(true, 1, false, true));
	EXPECT_FALSE(ShouldRenderMapProgressBar(true, 1, true, true));
	EXPECT_FALSE(ShouldRenderMapProgressBar(true, 0, false, false));
	EXPECT_TRUE(ShouldRenderMapProgressBar(true, 0, false, true));
}

TEST(QmLoadingProgress, UsesSharedTeeProgressVisuals)
{
	const std::string MenusSource = ReadTestSourceFile("src/game/client/components/menus.cpp");
	const size_t LoadingPos = MenusSource.find("void CMenus::RenderLoading");
	ASSERT_NE(LoadingPos, std::string::npos);
	const size_t LoadingEnd = MenusSource.find("void CMenus::FinishLoading", LoadingPos);
	ASSERT_NE(LoadingEnd, std::string::npos);
	const std::string LoadingBody = MenusSource.substr(LoadingPos, LoadingEnd - LoadingPos);
	EXPECT_NE(LoadingBody.find("RenderProgressBarWithTee"), std::string::npos);
	EXPECT_EQ(LoadingBody.find("Ui()->RenderProgressBar"), std::string::npos);
	EXPECT_EQ(LoadingBody.find("m_QmPlayerStatsMapProgress &&"), std::string::npos);
}

TEST(QmTranslateUiSettings, DefaultColorsMatchSettingsPreviewDefaults)
{
	ExpectColorNear(color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmTranslateBtnColorDisabled, true)), ColorRGBA(0.16f, 0.16f, 0.16f, 0.82f));
	ExpectColorNear(color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmTranslateBtnColorEnabled, true)), ColorRGBA(0.35f, 0.45f, 0.70f, 0.90f));
	ExpectColorNear(color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmTranslateMenuBgColor, true)), ColorRGBA(0.12f, 0.12f, 0.12f, 0.95f));
	ExpectColorNear(color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmTranslateMenuOptionSelected, true)), ColorRGBA(0.35f, 0.45f, 0.70f, 0.90f));
	ExpectColorNear(color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmTranslateMenuOptionNormal, true)), ColorRGBA(0.20f, 0.20f, 0.20f, 0.90f));
}

TEST(QmTranslateUiSettings, LegacyRgbColorsRestoreDeclaredAlpha)
{
	bool Migrated = false;
	unsigned Disabled = 0x005A6B7Cu;
	unsigned Enabled = 0x00010203u;
	unsigned Background = 0x00A1B2C3u;
	unsigned Selected = 0x00000000u;
	unsigned Normal = 0x00D4E5F6u;
	EXPECT_TRUE(NTranslateUiSettings::MigrateLegacyColorAlphas(Migrated, Disabled, Enabled, Background, Selected, Normal,
		DefaultConfig::QmTranslateBtnColorDisabled, DefaultConfig::QmTranslateBtnColorEnabled, DefaultConfig::QmTranslateMenuBgColor,
		DefaultConfig::QmTranslateMenuOptionSelected, DefaultConfig::QmTranslateMenuOptionNormal,
		EColorInputAlphaMode::PACKED, EColorInputAlphaMode::PACKED, EColorInputAlphaMode::PACKED, EColorInputAlphaMode::PACKED, EColorInputAlphaMode::PACKED));
	EXPECT_TRUE(Migrated);
	EXPECT_EQ(Disabled, 0xD15A6B7Cu);
	EXPECT_EQ(Enabled, 0xE6010203u);
	EXPECT_EQ(Background, 0xF2A1B2C3u);
	EXPECT_EQ(Selected, 0xE6000000u);
	EXPECT_EQ(Normal, 0xE6D4E5F6u);
}

TEST(QmTranslateUiSettings, AlphaAwareColorsAreNotChanged)
{
	bool Migrated = false;
	unsigned Disabled = 0x7F5A6B7Cu;
	unsigned Enabled = 0x805A6B7Cu;
	unsigned Background = 0x995A6B7Cu;
	unsigned Selected = 0xA05A6B7Cu;
	unsigned Normal = 0xB15A6B7Cu;
	EXPECT_TRUE(NTranslateUiSettings::MigrateLegacyColorAlphas(Migrated, Disabled, Enabled, Background, Selected, Normal,
		DefaultConfig::QmTranslateBtnColorDisabled, DefaultConfig::QmTranslateBtnColorEnabled, DefaultConfig::QmTranslateMenuBgColor,
		DefaultConfig::QmTranslateMenuOptionSelected, DefaultConfig::QmTranslateMenuOptionNormal,
		EColorInputAlphaMode::EXPLICIT, EColorInputAlphaMode::EXPLICIT, EColorInputAlphaMode::EXPLICIT, EColorInputAlphaMode::EXPLICIT, EColorInputAlphaMode::EXPLICIT));
	EXPECT_EQ(Disabled, 0x7F5A6B7Cu);
	EXPECT_EQ(Enabled, 0x805A6B7Cu);
	EXPECT_EQ(Background, 0x995A6B7Cu);
	EXPECT_EQ(Selected, 0xA05A6B7Cu);
	EXPECT_EQ(Normal, 0xB15A6B7Cu);
}

TEST(QmTranslateUiSettings, PackedColorsWithNonZeroAlphaAreNotChanged)
{
	bool Migrated = false;
	unsigned Disabled = 0x7F5A6B7Cu;
	unsigned Enabled = 0x805A6B7Cu;
	unsigned Background = 0x995A6B7Cu;
	unsigned Selected = 0xA05A6B7Cu;
	unsigned Normal = 0xB15A6B7Cu;
	EXPECT_TRUE(NTranslateUiSettings::MigrateLegacyColorAlphas(Migrated, Disabled, Enabled, Background, Selected, Normal,
		DefaultConfig::QmTranslateBtnColorDisabled, DefaultConfig::QmTranslateBtnColorEnabled, DefaultConfig::QmTranslateMenuBgColor,
		DefaultConfig::QmTranslateMenuOptionSelected, DefaultConfig::QmTranslateMenuOptionNormal,
		EColorInputAlphaMode::PACKED, EColorInputAlphaMode::PACKED, EColorInputAlphaMode::PACKED, EColorInputAlphaMode::PACKED, EColorInputAlphaMode::PACKED));
	EXPECT_EQ(Disabled, 0x7F5A6B7Cu);
	EXPECT_EQ(Enabled, 0x805A6B7Cu);
	EXPECT_EQ(Background, 0x995A6B7Cu);
	EXPECT_EQ(Selected, 0xA05A6B7Cu);
	EXPECT_EQ(Normal, 0xB15A6B7Cu);
}

TEST(QmTranslateUiSettings, ImplicitAlphaInputsRestoreDeclaredAlpha)
{
	bool Migrated = false;
	unsigned Disabled = 0xFF5A6B7Cu;
	unsigned Enabled = 0xFF010203u;
	unsigned Background = 0xFFA1B2C3u;
	unsigned Selected = 0xFF000000u;
	unsigned Normal = 0xFFD4E5F6u;
	EXPECT_TRUE(NTranslateUiSettings::MigrateLegacyColorAlphas(Migrated, Disabled, Enabled, Background, Selected, Normal,
		DefaultConfig::QmTranslateBtnColorDisabled, DefaultConfig::QmTranslateBtnColorEnabled, DefaultConfig::QmTranslateMenuBgColor,
		DefaultConfig::QmTranslateMenuOptionSelected, DefaultConfig::QmTranslateMenuOptionNormal,
		EColorInputAlphaMode::OMITTED, EColorInputAlphaMode::OMITTED, EColorInputAlphaMode::OMITTED, EColorInputAlphaMode::OMITTED, EColorInputAlphaMode::OMITTED));
	EXPECT_EQ(Disabled, 0xD15A6B7Cu);
	EXPECT_EQ(Enabled, 0xE6010203u);
	EXPECT_EQ(Background, 0xF2A1B2C3u);
	EXPECT_EQ(Selected, 0xE6000000u);
	EXPECT_EQ(Normal, 0xE6D4E5F6u);
}

TEST(QmTranslateUiSettings, ConfigManagerRecordsColorAlphaInputModes)
{
	struct SConfigRestore
	{
		CConfig m_Config = g_Config;
		~SConfigRestore() { g_Config = m_Config; }
	} ConfigRestore;
	CTestInfo TestInfo;
	std::unique_ptr<IStorage> pStorage = TestInfo.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	std::unique_ptr<IKernel> pKernel(IKernel::Create());
	pKernel->RegisterInterface(pStorage.get(), false);
	IConsole *pConsole = CreateConsole(CFGFLAG_CLIENT).release();
	pKernel->RegisterInterface(pConsole);
	IConfigManager *pConfigManager = CreateConfigManager();
	pKernel->RegisterInterface(pConfigManager);
	pConsole->Init();
	pConfigManager->Init();

	const auto MigrateDisabledColor = [pConfigManager]() {
		bool Migrated = false;
		unsigned Disabled = g_Config.m_QmTranslateBtnColorDisabled;
		unsigned Enabled = DefaultConfig::QmTranslateBtnColorEnabled;
		unsigned Background = DefaultConfig::QmTranslateMenuBgColor;
		unsigned Selected = DefaultConfig::QmTranslateMenuOptionSelected;
		unsigned Normal = DefaultConfig::QmTranslateMenuOptionNormal;
		EXPECT_TRUE(NTranslateUiSettings::MigrateLegacyColorAlphas(Migrated, Disabled, Enabled, Background, Selected, Normal,
			DefaultConfig::QmTranslateBtnColorDisabled, DefaultConfig::QmTranslateBtnColorEnabled, DefaultConfig::QmTranslateMenuBgColor,
			DefaultConfig::QmTranslateMenuOptionSelected, DefaultConfig::QmTranslateMenuOptionNormal,
			pConfigManager->ColorValueInputAlphaMode("qm_translate_btn_color_disabled"), EColorInputAlphaMode::EXPLICIT, EColorInputAlphaMode::EXPLICIT, EColorInputAlphaMode::EXPLICIT, EColorInputAlphaMode::EXPLICIT));
		return Disabled;
	};

	pConsole->ExecuteLine("qm_translate_btn_color_disabled $5A6B7C");
	EXPECT_EQ(pConfigManager->ColorValueInputAlphaMode("qm_translate_btn_color_disabled"), EColorInputAlphaMode::OMITTED);
	const unsigned OmittedRgb = g_Config.m_QmTranslateBtnColorDisabled;
	EXPECT_EQ(MigrateDisabledColor(), (OmittedRgb & ~NTranslateUiSettings::COLOR_ALPHA_MASK) | (DefaultConfig::QmTranslateBtnColorDisabled & NTranslateUiSettings::COLOR_ALPHA_MASK));

	pConsole->ExecuteLine("qm_translate_btn_color_disabled $ABC");
	EXPECT_EQ(pConfigManager->ColorValueInputAlphaMode("qm_translate_btn_color_disabled"), EColorInputAlphaMode::OMITTED);
	const unsigned OmittedShortRgb = g_Config.m_QmTranslateBtnColorDisabled;
	EXPECT_EQ(MigrateDisabledColor(), (OmittedShortRgb & ~NTranslateUiSettings::COLOR_ALPHA_MASK) | (DefaultConfig::QmTranslateBtnColorDisabled & NTranslateUiSettings::COLOR_ALPHA_MASK));

	pConsole->ExecuteLine("qm_translate_btn_color_disabled $5A6B7C7F");
	EXPECT_EQ(pConfigManager->ColorValueInputAlphaMode("qm_translate_btn_color_disabled"), EColorInputAlphaMode::EXPLICIT);
	const unsigned ExplicitAlpha = g_Config.m_QmTranslateBtnColorDisabled;
	EXPECT_EQ(MigrateDisabledColor(), ExplicitAlpha);

	pConsole->ExecuteLine("qm_translate_btn_color_disabled $ABCD");
	EXPECT_EQ(pConfigManager->ColorValueInputAlphaMode("qm_translate_btn_color_disabled"), EColorInputAlphaMode::EXPLICIT);
	const unsigned ExplicitShortAlpha = g_Config.m_QmTranslateBtnColorDisabled;
	EXPECT_EQ(MigrateDisabledColor(), ExplicitShortAlpha);

	pConsole->ExecuteLine("qm_translate_btn_color_disabled $5A6B7C00");
	EXPECT_EQ(pConfigManager->ColorValueInputAlphaMode("qm_translate_btn_color_disabled"), EColorInputAlphaMode::EXPLICIT);
	const unsigned ExplicitTransparent = g_Config.m_QmTranslateBtnColorDisabled;
	EXPECT_EQ(ExplicitTransparent & NTranslateUiSettings::COLOR_ALPHA_MASK, 0u);
	EXPECT_EQ(MigrateDisabledColor(), ExplicitTransparent);

	pConsole->ExecuteLine("qm_translate_btn_color_disabled red");
	EXPECT_EQ(pConfigManager->ColorValueInputAlphaMode("qm_translate_btn_color_disabled"), EColorInputAlphaMode::OMITTED);
	const unsigned NamedColor = g_Config.m_QmTranslateBtnColorDisabled;
	EXPECT_EQ(MigrateDisabledColor(), (NamedColor & ~NTranslateUiSettings::COLOR_ALPHA_MASK) | (DefaultConfig::QmTranslateBtnColorDisabled & NTranslateUiSettings::COLOR_ALPHA_MASK));

	pConsole->ExecuteLine("qm_translate_btn_color_disabled -16777216");
	EXPECT_EQ(pConfigManager->ColorValueInputAlphaMode("qm_translate_btn_color_disabled"), EColorInputAlphaMode::SIGNED_PACKED);
	EXPECT_EQ(MigrateDisabledColor() & NTranslateUiSettings::COLOR_ALPHA_MASK, DefaultConfig::QmTranslateBtnColorDisabled & NTranslateUiSettings::COLOR_ALPHA_MASK);

	pConsole->ExecuteLine("qm_translate_btn_color_disabled 2153407356");
	EXPECT_EQ(pConfigManager->ColorValueInputAlphaMode("qm_translate_btn_color_disabled"), EColorInputAlphaMode::PACKED);
	const unsigned UnsignedPackedAlpha = g_Config.m_QmTranslateBtnColorDisabled;
	EXPECT_NE(UnsignedPackedAlpha & NTranslateUiSettings::COLOR_ALPHA_MASK, 0u);
	EXPECT_EQ(MigrateDisabledColor(), UnsignedPackedAlpha);

	pConsole->ExecuteLine("qm_translate_btn_color_disabled +2153407356");
	EXPECT_EQ(pConfigManager->ColorValueInputAlphaMode("qm_translate_btn_color_disabled"), EColorInputAlphaMode::PACKED);
	EXPECT_EQ(MigrateDisabledColor(), g_Config.m_QmTranslateBtnColorDisabled);
}

TEST(QmTranslateUiSettings, MigrationMarkerPreservesIntentionalTransparentColor)
{
	bool Migrated = true;
	unsigned Disabled = 0x005A6B7Cu;
	unsigned Enabled = 0x00010203u;
	unsigned Background = 0x00A1B2C3u;
	unsigned Selected = 0x00000000u;
	unsigned Normal = 0x00D4E5F6u;
	EXPECT_FALSE(NTranslateUiSettings::MigrateLegacyColorAlphas(Migrated, Disabled, Enabled, Background, Selected, Normal,
		DefaultConfig::QmTranslateBtnColorDisabled, DefaultConfig::QmTranslateBtnColorEnabled, DefaultConfig::QmTranslateMenuBgColor,
		DefaultConfig::QmTranslateMenuOptionSelected, DefaultConfig::QmTranslateMenuOptionNormal));
	EXPECT_TRUE(Migrated);
	EXPECT_EQ(Disabled, 0x005A6B7Cu);
	EXPECT_EQ(Enabled, 0x00010203u);
	EXPECT_EQ(Background, 0x00A1B2C3u);
	EXPECT_EQ(Selected, 0x00000000u);
	EXPECT_EQ(Normal, 0x00D4E5F6u);
}

TEST(QmJumpHint, DefaultsAreChineseAndDisabled)
{
	EXPECT_EQ(DefaultConfig::QmJumpHint, 0);
	EXPECT_EQ(DefaultConfig::QmJumpHintDefaultsMigrated, 0);
	EXPECT_STREQ(DefaultConfig::QmJumpHintText, "三格边缘跳:\\n左起跳: .34|.31|.16\\n左二段跳: .41|.28|.25|.13\\n右起跳: .63|.66|.81\\n右二段跳: .56|.69|.72|.84");
	EXPECT_STREQ(JUMP_HINT_DEFAULT_TEXT, DefaultConfig::QmJumpHintText);
}

TEST(QmJumpHint, UpgradeReplacesOriginalEnglishAndDisablesOnce)
{
	int Migrated = 0;
	int Enabled = 1;
	char aText[512] = "3 Tiles Edge Jump:\\nLeft Jump: .34|.31|.16\\nLeft Double Jump: .41|.28|.25|.13\\nRight Jump: .63|.66|.81\\nRight Double Jump: .56|.69|.72|.84";
	MigrateJumpHintDefaults(Migrated, Enabled, aText, sizeof(aText));
	EXPECT_EQ(Migrated, 1);
	EXPECT_EQ(Enabled, 0);
	EXPECT_STREQ(aText, JUMP_HINT_DEFAULT_TEXT);

	// 用户重新开启后，后续启动不得再次关闭。
	Enabled = 1;
	MigrateJumpHintDefaults(Migrated, Enabled, aText, sizeof(aText));
	EXPECT_EQ(Enabled, 1);
	EXPECT_STREQ(aText, JUMP_HINT_DEFAULT_TEXT);
}

TEST(QmJumpHint, UpgradePreservesCustomTextIncludingModifiedEnglish)
{
	for(const char *pText : {"我的三跳提示\\n保留这一行", "3 Tiles Edge Jump:\\nLeft Jump: .34", ""})
	{
		int Migrated = 0;
		int Enabled = 1;
		char aText[512];
		str_copy(aText, pText, sizeof(aText));
		MigrateJumpHintDefaults(Migrated, Enabled, aText, sizeof(aText));
		EXPECT_EQ(Migrated, 1);
		EXPECT_EQ(Enabled, 0);
		EXPECT_STREQ(aText, pText);
	}
}

TEST(QmJumpHint, CompletedMigrationPreservesSubsequentUserChoices)
{
	int Migrated = 1;
	int Enabled = 1;
	char aText[512] = "3 Tiles Edge Jump:\\nLeft Jump: .34|.31|.16\\nLeft Double Jump: .41|.28|.25|.13\\nRight Jump: .63|.66|.81\\nRight Double Jump: .56|.69|.72|.84";
	const std::string UserText = aText;
	MigrateJumpHintDefaults(Migrated, Enabled, aText, sizeof(aText));
	EXPECT_EQ(Enabled, 1);
	EXPECT_STREQ(aText, UserText.c_str());
}

namespace
{
	class CQmEmoteCommandsTest : public ::testing::Test
	{
	protected:
		struct SRequest
		{
			int m_Emoticon;
			bool m_ForceLaunch;
		};
		struct SReceiver
		{
			std::vector<SRequest> m_vRequests;

			// 记录公共 Emote 入口的调用，不替代控制台解析或表情校验。
			void Emote(int Emoticon, bool ForceLaunch = false)
			{
				m_vRequests.push_back({Emoticon, ForceLaunch});
			}
		} m_Receiver;
		std::unique_ptr<IConsole> m_pConsole = CreateConsole(CFGFLAG_CLIENT);

		void SetUp() override
		{
			QmEmoticon::RegisterCommands(m_pConsole.get(), &m_Receiver);
		}
	};
}

TEST_F(CQmEmoteCommandsTest, RegistersClientConsoleCommandWithExistingEmoteSyntax)
{
	const auto *pEmote = m_pConsole->GetCommandInfo("emote", CFGFLAG_CLIENT, false);
	const auto *pQmEmote = m_pConsole->GetCommandInfo("qm_emote", CFGFLAG_CLIENT, false);
	ASSERT_NE(pEmote, nullptr);
	ASSERT_NE(pQmEmote, nullptr);
	EXPECT_STREQ(pQmEmote->Params(), pEmote->Params());
	EXPECT_STREQ(pQmEmote->Params(), "i[emote-id]");
	EXPECT_EQ(pQmEmote->Flags(), CFGFLAG_CLIENT);
	EXPECT_EQ(m_pConsole->GetCommandInfo("qm_emote", CFGFLAG_CHAT, false), nullptr);
	// 硬改名：旧命令 shot_emote 不再注册。
	EXPECT_EQ(m_pConsole->GetCommandInfo("shot_emote", CFGFLAG_CLIENT, false), nullptr);
}

TEST_F(CQmEmoteCommandsTest, QmEmoteForwardsEveryExistingIdOnceToEmote)
{
	for(int Emoticon = 0; Emoticon < NUM_EMOTICONS; ++Emoticon)
	{
		SCOPED_TRACE(Emoticon);
		m_Receiver.m_vRequests.clear();
		const std::string Command = "qm_emote " + std::to_string(Emoticon);
		m_pConsole->ExecuteLine(Command.c_str());
		ASSERT_EQ(m_Receiver.m_vRequests.size(), 1u);
		EXPECT_EQ(m_Receiver.m_vRequests[0].m_Emoticon, Emoticon);
		EXPECT_TRUE(m_Receiver.m_vRequests[0].m_ForceLaunch);
	}
}

TEST_F(CQmEmoteCommandsTest, PreservesExistingIntegerParsing)
{
	struct SCase
	{
		const char *m_pArguments;
		bool m_Dispatched;
		int m_Emoticon;
	};
	const SCase aCases[] = {
		{"", false, 0},
		{"invalid", false, 0},
		{"2147483647", false, 0},
		{"-2147483648", false, 0},
		{"99999999999999999999", false, 0},
		{"-1", true, -1},
		{"16", true, 16},
		{"+7", true, 7},
		{"\"7\"", true, 7},
		{"7 extra", true, 7},
		// 采用上游 821d5ae4b4 后：引号参数同样参与校验，非法整数一律不派发。
		{"\"invalid\"", false, 0},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_pArguments);
		for(const char *pCommandName : {"emote", "qm_emote"})
		{
			SCOPED_TRACE(pCommandName);
			m_Receiver.m_vRequests.clear();
			const std::string Command = std::string(pCommandName) + " " + Case.m_pArguments;
			EXPECT_EQ(m_pConsole->LineIsValid(Command.c_str()), Case.m_Dispatched);
			m_pConsole->ExecuteLine(Command.c_str());
			ASSERT_EQ(m_Receiver.m_vRequests.size(), Case.m_Dispatched ? 1u : 0u);
			if(Case.m_Dispatched)
				EXPECT_EQ(m_Receiver.m_vRequests[0].m_Emoticon, Case.m_Emoticon);
		}
	}
}

TEST_F(CQmEmoteCommandsTest, QmEmoteDoesNotForceSubsequentEmote)
{
	m_pConsole->ExecuteLine("qm_emote 2; emote 3");
	ASSERT_EQ(m_Receiver.m_vRequests.size(), 2u);
	EXPECT_EQ(m_Receiver.m_vRequests[0].m_Emoticon, 2);
	EXPECT_TRUE(m_Receiver.m_vRequests[0].m_ForceLaunch);
	EXPECT_EQ(m_Receiver.m_vRequests[1].m_Emoticon, 3);
	EXPECT_FALSE(m_Receiver.m_vRequests[1].m_ForceLaunch);
}

TEST(QmEmoticon, ForcedLaunchPreservesWheelModeAndExistingSuperConsumption)
{
	for(int Emoticon = 0; Emoticon < NUM_EMOTICONS; ++Emoticon)
	{
		for(const bool LaunchMode : {false, true})
		{
			for(const bool Super : {false, true})
			{
				bool SuperPending = Super;
				EXPECT_EQ(QmEmoticon::ConsumeEffect(Emoticon, LaunchMode, SuperPending, true), Super ? QmEmoticon::EEffect::SUPER_PROJECTILE : QmEmoticon::EEffect::PROJECTILE);
				EXPECT_FALSE(SuperPending);
				EXPECT_EQ(QmEmoticon::ConsumeEffect(Emoticon, LaunchMode, SuperPending), LaunchMode ? QmEmoticon::EEffect::PROJECTILE : QmEmoticon::EEffect::NONE);
			}
		}
	}
}

TEST(QmEmoticon, ForcedLaunchUsesExistingIdValidation)
{
	for(const int Emoticon : {-1, static_cast<int>(NUM_EMOTICONS), std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
	{
		for(const bool LaunchMode : {false, true})
		{
			bool SuperPending = true;
			EXPECT_EQ(QmEmoticon::ConsumeEffect(Emoticon, LaunchMode, SuperPending, true), QmEmoticon::EEffect::INVALID);
			EXPECT_FALSE(SuperPending);
		}
	}
}

TEST(QmEmoticon, InvalidRequestsConsumePendingSuperEmote)
{
	for(const int Emoticon : {-1, static_cast<int>(NUM_EMOTICONS), std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
	{
		bool SuperPending = true;
		EXPECT_EQ(QmEmoticon::ConsumeEffect(Emoticon, true, SuperPending), QmEmoticon::EEffect::INVALID);
		EXPECT_FALSE(SuperPending);
		EXPECT_EQ(QmEmoticon::ConsumeEffect(0, true, SuperPending), QmEmoticon::EEffect::PROJECTILE);
	}
}

TEST(QmEmoticon, ValidBoundaryIdsConsumeSuperEmoteOnce)
{
	for(const int Emoticon : {0, NUM_EMOTICONS - 1})
	{
		bool SuperPending = true;
		EXPECT_EQ(QmEmoticon::ConsumeEffect(Emoticon, false, SuperPending), QmEmoticon::EEffect::SUPER_HEAD);
		EXPECT_FALSE(SuperPending);
		EXPECT_EQ(QmEmoticon::ConsumeEffect(Emoticon, false, SuperPending), QmEmoticon::EEffect::NONE);
	}
}

TEST(QmEmoticon, LocalAndRemoteEffectsAgreeForEveryLaunchMode)
{
	struct SCase
	{
		bool m_Launch;
		bool m_Super;
		QmEmoticon::EEffect m_Expected;
	};
	const SCase aCases[] = {
		{false, false, QmEmoticon::EEffect::NONE},
		{false, true, QmEmoticon::EEffect::SUPER_HEAD},
		{true, false, QmEmoticon::EEffect::PROJECTILE},
		{true, true, QmEmoticon::EEffect::SUPER_PROJECTILE},
	};
	for(const auto &Case : aCases)
	{
		bool SuperPending = Case.m_Super;
		EXPECT_EQ(QmEmoticon::ConsumeEffect(4, Case.m_Launch, SuperPending), Case.m_Expected);
		EXPECT_EQ(QmEmoticon::ResolveRemoteEffect(4, Case.m_Launch, Case.m_Super, true, false, true, true), Case.m_Expected);
	}
}

TEST(QmEmoticon, GlobalAndPerPlayerMuteSuppressAllRemoteEffects)
{
	for(const bool Launch : {false, true})
	{
		for(const bool Super : {false, true})
		{
			EXPECT_EQ(QmEmoticon::ResolveRemoteEffect(4, Launch, Super, false, false, true, true), QmEmoticon::EEffect::NONE);
			EXPECT_EQ(QmEmoticon::ResolveRemoteEffect(4, Launch, Super, true, true, true, true), QmEmoticon::EEffect::NONE);
		}
	}
}

TEST(QmEmoticon, RemoteVisibilityFiltersHeadAndProjectileIndependently)
{
	for(const bool ShowSuper : {false, true})
	{
		for(const bool ShowLaunch : {false, true})
		{
			EXPECT_EQ(QmEmoticon::ResolveRemoteEffect(4, false, true, true, false, ShowSuper, ShowLaunch), ShowSuper ? QmEmoticon::EEffect::SUPER_HEAD : QmEmoticon::EEffect::NONE);
			EXPECT_EQ(QmEmoticon::ResolveRemoteEffect(4, true, false, true, false, ShowSuper, ShowLaunch), ShowLaunch ? QmEmoticon::EEffect::PROJECTILE : QmEmoticon::EEffect::NONE);
			EXPECT_EQ(QmEmoticon::ResolveRemoteEffect(4, true, true, true, false, ShowSuper, ShowLaunch), ShowLaunch ? QmEmoticon::EEffect::SUPER_PROJECTILE : QmEmoticon::EEffect::NONE);
		}
	}
}

TEST(QmFriendEnterTracker, InitialRosterIsSilentAndNewFriendEntersOnce)
{
	qm_friend_notify::CEnterTracker Tracker;
	const qm_friend_notify::CEnterTracker::CClient Existing{1, "Existing", "Clan", true, false};
	const qm_friend_notify::CEnterTracker::CClient NewFriend{2, "NewFriend", "Clan", true, false};
	EXPECT_TRUE(Tracker.Update({Existing}, 0.0, false).empty());
	EXPECT_EQ(Tracker.Update({Existing, NewFriend}, 0.2, false), std::vector<std::string>{"NewFriend"});
	EXPECT_TRUE(Tracker.Update({Existing, NewFriend}, 0.4, false).empty());
}

TEST(QmFriendEnterTracker, SameSlotIdentityAndFriendChangesAreSilent)
{
	qm_friend_notify::CEnterTracker Tracker;
	EXPECT_TRUE(Tracker.Update({{1, "Stranger", "OldClan", false, false}}, 0.0, false).empty());
	EXPECT_TRUE(Tracker.Update({{1, "Friend", "OldClan", true, false}}, 0.2, false).empty());
	EXPECT_TRUE(Tracker.Update({{1, "Friend", "NewClan", true, false}}, 0.4, false).empty());
	EXPECT_TRUE(Tracker.Update({{1, "Friend", "NewClan", false, false}}, 0.6, false).empty());
	EXPECT_TRUE(Tracker.Update({{1, "Friend", "NewClan", true, false}}, 0.8, false).empty());
}

TEST(QmFriendEnterTracker, ShortSnapshotAbsenceDoesNotReannounceFriend)
{
	qm_friend_notify::CEnterTracker Tracker;
	const qm_friend_notify::CEnterTracker::CClient Friend{1, "Friend", "Clan", true, false};
	EXPECT_TRUE(Tracker.Update({Friend}, 0.0, false).empty());
	EXPECT_TRUE(Tracker.Update({}, 1.0, false).empty());
	EXPECT_TRUE(Tracker.Update({}, 3.8, false).empty());
	EXPECT_TRUE(Tracker.Update({Friend}, 3.9, false).empty());
	EXPECT_TRUE(Tracker.Update({}, 4.0, false).empty());
	EXPECT_TRUE(Tracker.Update({Friend}, 6.9, false).empty());
}

TEST(QmFriendEnterTracker, ConfirmedAbsenceAllowsReentryAtGraceBoundary)
{
	qm_friend_notify::CEnterTracker Tracker;
	const qm_friend_notify::CEnterTracker::CClient Friend{1, "Friend", "Clan", true, false};
	EXPECT_TRUE(Tracker.Update({Friend}, 0.0, false).empty());
	EXPECT_TRUE(Tracker.Update({}, 1.0, false).empty());
	EXPECT_EQ(Tracker.Update({Friend}, 4.0, false), std::vector<std::string>{"Friend"});
	EXPECT_TRUE(Tracker.Update({Friend}, 4.2, false).empty());
}

TEST(QmFriendEnterTracker, ObservationPauseDoesNotCountAsAbsence)
{
	qm_friend_notify::CEnterTracker Tracker;
	const qm_friend_notify::CEnterTracker::CClient Friend{1, "Friend", "Clan", true, false};
	EXPECT_TRUE(Tracker.Update({Friend}, 0.0, false).empty());
	EXPECT_TRUE(Tracker.Update({Friend}, 100.0, false).empty());
	EXPECT_TRUE(Tracker.Update({}, 200.0, false).empty());
	EXPECT_TRUE(Tracker.Update({Friend}, 200.2, false).empty());
}

TEST(QmFriendEnterTracker, IdentityMovingSlotsWithinGraceIsSilent)
{
	qm_friend_notify::CEnterTracker Tracker;
	EXPECT_TRUE(Tracker.Update({{1, "Friend", "Clan", true, false}}, 0.0, false).empty());
	EXPECT_TRUE(Tracker.Update({}, 1.0, false).empty());
	EXPECT_TRUE(Tracker.Update({{2, "Friend", "Clan", true, false}}, 3.9, false).empty());
	EXPECT_EQ(Tracker.Update({{1, "NewFriend", "Clan", true, false}, {2, "Friend", "Clan", true, false}}, 4.0, false), std::vector<std::string>{"NewFriend"});
}

TEST(QmFriendEnterTracker, IdentityMovingSlotsAfterConfirmedAbsenceEnters)
{
	qm_friend_notify::CEnterTracker Tracker;
	EXPECT_TRUE(Tracker.Update({{1, "Friend", "Clan", true, false}}, 0.0, false).empty());
	EXPECT_TRUE(Tracker.Update({}, 1.0, false).empty());
	EXPECT_TRUE(Tracker.Update({}, 4.0, false).empty());
	EXPECT_EQ(Tracker.Update({{2, "Friend", "Clan", true, false}}, 4.2, false), std::vector<std::string>{"Friend"});
}

TEST(QmFriendEnterTracker, SlotMigrationRespectsIgnoreClan)
{
	for(const bool IgnoreClan : {false, true})
	{
		qm_friend_notify::CEnterTracker Tracker;
		EXPECT_TRUE(Tracker.Update({{1, "Friend", "OldClan", true, false}}, 0.0, IgnoreClan).empty());
		EXPECT_TRUE(Tracker.Update({}, 1.0, IgnoreClan).empty());
		const auto vNames = Tracker.Update({{2, "Friend", "NewClan", true, false}}, 1.2, IgnoreClan);
		EXPECT_EQ(vNames, IgnoreClan ? std::vector<std::string>{} : std::vector<std::string>{"Friend"});
	}
}

TEST(QmFriendEnterTracker, LocalPlayersAndNonFriendsDoNotNotify)
{
	qm_friend_notify::CEnterTracker Tracker;
	EXPECT_TRUE(Tracker.Update({}, 0.0, false).empty());
	EXPECT_TRUE(Tracker.Update({{1, "Main", "Clan", true, true}, {2, "Dummy", "Clan", true, true}, {3, "Stranger", "Clan", false, false}}, 0.2, false).empty());
	EXPECT_TRUE(Tracker.Update({{1, "Main", "Clan", true, false}, {2, "Dummy", "Clan", true, false}, {3, "Stranger", "Clan", true, false}}, 0.4, false).empty());
}

TEST(QmFriendEnterTracker, ResetBuildsANewSilentBaseline)
{
	qm_friend_notify::CEnterTracker Tracker;
	EXPECT_TRUE(Tracker.Update({}, 0.0, false).empty());
	EXPECT_EQ(Tracker.Update({{1, "Friend", "Clan", true, false}}, 0.2, false), std::vector<std::string>{"Friend"});
	Tracker.Reset();
	EXPECT_TRUE(Tracker.Update({{1, "Friend", "Clan", true, false}, {2, "Another", "Clan", true, false}}, 1.0, false).empty());
}

TEST(QmEmoticonProjectile, TransparentPixelsDoNotCollideAndRotationFollowsImage)
{
	unsigned char aPixels[4 * 4 * 4] = {};
	for(int Y = 0; Y < 4; ++Y)
		aPixels[(Y * 4 + 3) * 4 + 3] = 255;
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(aPixels, 4, 4);
	const auto Wall = [](int X, int Y) { return X == 1 && Y == 0; };
	EXPECT_FALSE(Mask.Overlaps(vec2(16, 16), 32, pi, Wall));
	EXPECT_TRUE(Mask.Overlaps(vec2(20, 16), 32, 0, Wall));
	EXPECT_FALSE(Mask.Overlaps(vec2(20, 16), 32, pi, Wall));
	EXPECT_TRUE(Mask.Overlaps(vec2(16, 16), 64, 0, Wall));
}

TEST(QmEmoticonProjectile, EmptyAndHollowImagesPreserveTransparentAreas)
{
	unsigned char aPixels[3 * 3 * 4] = {};
	QmEmoticon::CAlphaMask Mask;
	const auto CenterTile = [](int X, int Y) { return X == 0 && Y == 0; };
	Mask.Build(aPixels, 3, 3);
	EXPECT_FALSE(Mask.Overlaps(vec2(16, 16), 96, 0, CenterTile));
	for(int I = 0; I < 9; ++I)
		aPixels[I * 4 + 3] = I == 4 ? 0 : 255;
	Mask.Build(aPixels, 3, 3);
	EXPECT_FALSE(Mask.Overlaps(vec2(16, 16), 96, 0, CenterTile));
	EXPECT_TRUE(Mask.Overlaps(vec2(18, 16), 96, 0, CenterTile));
}

TEST(QmEmoticonProjectile, FastProjectileCannotCrossOneTileWall)
{
	const unsigned char aPixel[] = {255, 255, 255, 255};
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(aPixel, 1, 1);
	CEmoticonProjectile Projectile;
	Projectile.Init(vec2(-100, 16), vec2(1200, 0), 0);
	Projectile.m_AngVel = 0;
	const auto Wall = [](int X, int) { return X == 0; };
	Projectile.Update(0.2f, Mask, Wall);
	EXPECT_LT(Projectile.m_Pos.x, -31.9f);
	EXPECT_LT(Projectile.m_Vel.x, 0);
	EXPECT_FALSE(Mask.Overlaps(Projectile.m_Pos, Projectile.Size(), Projectile.m_Angle, Wall));
}

TEST(QmEmoticonProjectile, FramePartitionsHaveSameMotion)
{
	QmEmoticon::CAlphaMask Mask;
	CEmoticonProjectile First, Second;
	First.Init(vec2(0, 0), vec2(1200, -400), 0);
	Second = First;
	const auto Air = [](int, int) { return false; };
	for(int I = 0; I < 30; ++I)
		First.Update(1.0f / 30, Mask, Air);
	for(int I = 0; I < 144; ++I)
		Second.Update(1.0f / 144, Mask, Air);
	EXPECT_NEAR(First.m_Pos.x, Second.m_Pos.x, 0.01f);
	EXPECT_NEAR(First.m_Pos.y, Second.m_Pos.y, 0.01f);
}

TEST(QmEmoticonProjectile, PoolAlwaysAcceptsNewestLaunch)
{
	CEmoticonProjectile aProjectiles[2];
	EXPECT_EQ(QmEmoticon::ProjectileSlot(aProjectiles), &aProjectiles[0]);
	aProjectiles[0].Init(vec2(0, 0), vec2(0, 0), 0);
	EXPECT_EQ(QmEmoticon::ProjectileSlot(aProjectiles), &aProjectiles[1]);
	aProjectiles[1].Init(vec2(0, 0), vec2(0, 0), 1);
	aProjectiles[0].m_LifeTime = 1;
	EXPECT_EQ(QmEmoticon::ProjectileSlot(aProjectiles), &aProjectiles[0]);
}

TEST(QmEmoticonProjectile, LatestCursorSelectsWithoutRendering)
{
	EXPECT_EQ(QmEmoticon::SelectedSector(vec2(170, 0), 110, NUM_EMOTICONS), 0);
	EXPECT_EQ(QmEmoticon::SelectedSector(vec2(0, -170), 110, NUM_EMOTICONS), 12);
	EXPECT_EQ(QmEmoticon::SelectedSector(vec2(0, 0), 110, NUM_EMOTICONS), -1);
}

TEST(QmEmoticonProjectile, SuperProjectileStartsOutsideNearbyFloor)
{
	const unsigned char aPixel[] = {255, 255, 255, 255};
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(aPixel, 1, 1);
	CEmoticonProjectile Projectile;
	Projectile.Init(vec2(0, -20), vec2(1200, -400), 0, 2.35f);
	const auto Floor = [](int, int Y) { return Y >= 0; };
	ASSERT_TRUE(Projectile.PlaceOutside(Mask, Floor));
	EXPECT_LE(Projectile.m_Pos.y + Projectile.Size() / 2, 0);
	EXPECT_FALSE(Mask.Overlaps(Projectile.m_Pos, Projectile.Size(), Projectile.m_Angle, Floor));
}

TEST(QmEmoticonProjectile, LifetimeEndsEvenAfterLongFrame)
{
	QmEmoticon::CAlphaMask Mask;
	CEmoticonProjectile Projectile;
	Projectile.Init(vec2(0, 0), vec2(1200, -400), 0);
	Projectile.Update(4, Mask, [](int, int) { return false; });
	EXPECT_FALSE(Projectile.m_Active);
}

TEST(QmEmoticonProjectile, FadeGrowthCannotForceImageThroughNarrowCorridor)
{
	const unsigned char aPixel[] = {255, 255, 255, 255};
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(aPixel, 1, 1);
	CEmoticonProjectile Projectile;
	Projectile.Init(vec2(32, 32), vec2(0, 0), 0);
	Projectile.m_AngVel = 0;
	Projectile.m_LifeTime = 0.5f;
	const auto Corridor = [](int, int Y) { return Y < 0 || Y >= 2; };
	Projectile.Update(0.1f, Mask, Corridor);
	EXPECT_TRUE(Projectile.m_Active);
	EXPECT_FLOAT_EQ(Projectile.Size(), 64);
	EXPECT_FALSE(Mask.Overlaps(Projectile.m_Pos, Projectile.Size(), Projectile.m_Angle, Corridor));
}

// 表情与 Tee 的身体盒碰撞：撞到不动的玩家要反弹，而不是穿过去。
TEST(QmEmoticonProjectile, PlayerBoxReboundsProjectile)
{
	const unsigned char aPixel[] = {255, 255, 255, 255};
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(aPixel, 1, 1);
	CEmoticonProjectile Projectile;
	Projectile.Init(vec2(-100, 16), vec2(1200, 0), 0, 1.0f, 0);
	Projectile.m_AngVel = 0;
	const auto Air = [](int, int) { return false; };
	const QmEmoticon::SPlayerBox aBoxes[] = {{1, vec2(0, 16), CCharacterCore::PhysicalSize() * 0.5f}};
	Projectile.Update(0.2f, Mask, Air, aBoxes, 1);
	// 表情半宽 32、玩家半宽 14，中心最远只能推进到 -46。
	EXPECT_LT(Projectile.m_Pos.x, -45.9f);
	EXPECT_LT(Projectile.m_Vel.x, 0);
}

// 发射者自身不算障碍，否则弹道会在出生点被自己的身体挡住。
TEST(QmEmoticonProjectile, OwnerIsExcludedFromPlayerCollision)
{
	const unsigned char aPixel[] = {255, 255, 255, 255};
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(aPixel, 1, 1);
	CEmoticonProjectile Projectile;
	Projectile.Init(vec2(0, 0), vec2(1200, 0), 0, 1.0f, 7);
	Projectile.m_AngVel = 0;
	const auto Air = [](int, int) { return false; };
	const QmEmoticon::SPlayerBox aBoxes[] = {{7, vec2(0, 0), CCharacterCore::PhysicalSize() * 0.5f}};
	Projectile.Update(0.05f, Mask, Air, aBoxes, 1);
	EXPECT_GT(Projectile.m_Pos.x, 50.0f);
}

// 贴墙时消失动画的膨胀只能冻结尺寸，不能像脱困逻辑那样把表情挪到墙的另一侧。
TEST(QmEmoticonProjectile, GrowthNeverTeleportsProjectileAcrossWall)
{
	const unsigned char aPixel[] = {255, 255, 255, 255};
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(aPixel, 1, 1);
	CEmoticonProjectile Projectile;
	Projectile.Init(vec2(-32, 16), vec2(0, 0), 0);
	Projectile.m_AngVel = 0;
	Projectile.m_LifeTime = 0.6f;
	const auto Wall = [](int X, int) { return X >= 0; };
	for(int I = 0; I < 40; ++I)
	{
		Projectile.Update(1.0f / 60, Mask, Wall);
		if(!Projectile.m_Active)
			break;
		EXPECT_FLOAT_EQ(Projectile.m_Pos.x, -32.0f);
		EXPECT_LE(Projectile.m_Pos.x + Projectile.Size() / 2, 0.0001f);
	}
}

// 与 Tee 的碰撞同样遵守贴图透明区域，空心表情不会凭空挡住玩家。
TEST(QmEmoticonProjectile, TransparentPixelsDoNotBlockPlayerBox)
{
	unsigned char aPixels[4 * 4 * 4] = {};
	for(int Y = 0; Y < 4; ++Y)
		aPixels[(Y * 4 + 3) * 4 + 3] = 255;
	QmEmoticon::CAlphaMask Mask;
	Mask.Build(aPixels, 4, 4);
	// 不透明像素集中在图片右侧，玩家盒压在左侧透明区时不算碰撞。
	EXPECT_FALSE(Mask.OverlapsBox(vec2(64, 16), 32, 0, vec2(54, 16), vec2(14, 14)));
	EXPECT_TRUE(Mask.OverlapsBox(vec2(64, 16), 32, 0, vec2(72, 16), vec2(14, 14)));
}

TEST(QmRouteStartIndex, MatchesFullMapScanForGameAndFrontStarts)
{
	const int Width = 128;
	const int MapSize = Width * 128;
	std::vector<int> vGame(MapSize, TILE_AIR), vFront(MapSize, TILE_AIR);
	std::vector<bool> vReachable(MapSize, true);
	vGame[0] = TILE_START;
	vGame[74] = TILE_START;
	vFront[120] = TILE_START;
	vGame[MapSize - 1] = TILE_START;
	vFront[MapSize - 1] = TILE_START;
	vReachable[74] = false;
	CQmRouteStartIndex Starts;
	for(int Index = 0; Index < MapSize; ++Index)
		Starts.AddTile(Index, vGame[Index], vFront[Index]);
	const auto IsStart = [&](int Index) { return vGame[Index] == TILE_START || vFront[Index] == TILE_START; };
	const auto PositionOf = [&](int Index) { return vec2((Index % Width) * 32.0f + 16.0f, (Index / Width) * 32.0f + 16.0f); };
	for(const vec2 Position : {vec2(-100, -100), vec2(2000, 16), vec2(9000, 9000), vec2(128, 128)})
	{
		int Expected = -1;
		float BestDistance = std::numeric_limits<float>::max();
		for(int Index = 0; Index < MapSize; ++Index)
		{
			if(!vReachable[Index] || !IsStart(Index))
				continue;
			const float Distance = length_squared(Position - PositionOf(Index));
			if(Distance < BestDistance)
			{
				BestDistance = Distance;
				Expected = Index;
			}
		}
		EXPECT_EQ(Starts.FindClosest(Position, -1, [&](int Index) { return vReachable[Index] && IsStart(Index); }, PositionOf), Expected);
	}
	// 与查询时当前格子一致；旧起点被清除时不能再选中它。
	vGame[0] = TILE_AIR;
	EXPECT_EQ(Starts.FindClosest(vec2(16, 16), -1, [&](int Index) { return vReachable[Index] && IsStart(Index); }, PositionOf), 120);
}

TEST(QmRouteStartIndex, QueriesOnlyStartTilesAndPreservesTiesAndFallback)
{
	CQmRouteStartIndex Starts;
	const int MapSize = 1024 * 1024;
	for(int Index = 0; Index < MapSize; ++Index)
		Starts.AddTile(Index, Index == 7 || Index == 19 ? TILE_START : TILE_AIR, Index == 33 ? TILE_START : TILE_AIR);
	int Checks = 0;
	const auto Eligible = [&](int) { ++Checks; return true; };
	const auto PositionOf = [](int Index) { return vec2(Index == 7 ? -10.0f : 10.0f, 0); };
	for(int Frame = 0; Frame < 128; ++Frame)
		EXPECT_EQ(Starts.FindClosest(vec2(0, 0), -1, Eligible, PositionOf), 7);
	EXPECT_EQ(Checks, 3 * 128);
	EXPECT_EQ(Starts.FindClosest(vec2(0, 0), 999, [](int) { return false; }, PositionOf), 999);
	Starts.Reset();
	EXPECT_EQ(Starts.FindClosest(vec2(0, 0), -1, Eligible, PositionOf), -1);
	EXPECT_EQ(Checks, 3 * 128);
	EXPECT_FALSE(Starts.AddTile(0, TILE_AIR, TILE_AIR));
	EXPECT_TRUE(Starts.AddTile(1, TILE_AIR, TILE_START));
	EXPECT_EQ(Starts.FindClosest(vec2(0, 0), -1, [](int) { return true; }, PositionOf), 1);
}

TEST(QmMarkdownCache, WritingIsDeferredAndNewestSnapshotSurvivesOwner)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	char aPath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, "qmclient/news_cache.json", aPath, sizeof(aPath));
	const std::string Expected = "公告\n\"引号\" 与 \\ 路径";
	std::shared_ptr<IJob> pJob;
	{
		CQmMarkdownCacheWriter Writer;
		pJob = Writer.Enqueue(aPath, 1, 10, "old");
		ASSERT_NE(pJob, nullptr);
		EXPECT_FALSE(pJob->IsAbortable());
		std::string Markdown = Expected;
		EXPECT_EQ(Writer.Enqueue(aPath, 1, 11, Markdown), nullptr);
		Markdown = "changed after enqueue";
		EXPECT_FALSE(pStorage->FileExists("qmclient/news_cache.json", IStorage::TYPE_SAVE));
	}
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	Pool.Shutdown();
	void *pData = nullptr;
	unsigned Size = 0;
	ASSERT_TRUE(pStorage->ReadFile("qmclient/news_cache.json", IStorage::TYPE_SAVE, &pData, &Size));
	json_value *pRoot = json_parse(static_cast<const char *>(pData), Size);
	free(pData);
	ASSERT_NE(pRoot, nullptr);
	EXPECT_EQ(pRoot->type, json_object);
	EXPECT_EQ(pRoot->u.object.length, 3u);
	EXPECT_EQ(json_int_get(json_object_get(pRoot, "cache_version")), 1);
	EXPECT_EQ(json_int_get(json_object_get(pRoot, "version")), 11);
	EXPECT_STREQ(json_string_get(json_object_get(pRoot, "markdown")), Expected.c_str());
	json_value_free(pRoot);
}

TEST(QmMarkdownCache, FinishedOrFailedWriteAllowsNextRequest)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	char aPath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, "qmclient/sponsors_cache.json", aPath, sizeof(aPath));
	CQmMarkdownCacheWriter Writer;
	CJobPool Pool;
	// 目录不能当文件打开；失败后仍可派发下一次写入。
	char aDirectory[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, "", aDirectory, sizeof(aDirectory));
	auto pJob = Writer.Enqueue(aDirectory, 1, 2, "unwritable");
	Pool.Init(1);
	Pool.Add(pJob);
	Pool.Shutdown();
	for(int Version : {3, 4})
	{
		pJob = Writer.Enqueue(aPath, 1, Version, "");
		ASSERT_NE(pJob, nullptr);
		Pool.Init(1);
		Pool.Add(pJob);
		Pool.Shutdown();
		void *pData = nullptr;
		unsigned Size = 0;
		ASSERT_TRUE(pStorage->ReadFile("qmclient/sponsors_cache.json", IStorage::TYPE_SAVE, &pData, &Size));
		json_value *pRoot = json_parse(static_cast<const char *>(pData), Size);
		free(pData);
		ASSERT_NE(pRoot, nullptr);
		EXPECT_EQ(json_int_get(json_object_get(pRoot, "version")), Version);
		EXPECT_STREQ(json_string_get(json_object_get(pRoot, "markdown")), "");
		json_value_free(pRoot);
	}
}

// —— 禅模式（Focus / Zen Mode）决策与覆盖 —— 语义与删除前实现对齐。

TEST(QmFocusMode, ConfigOverrideRestoresOnlyAutoHiddenValues)
{
	SQmConfigOverrideState State;
	bool Changed = false;

	int Value = ApplyQmConfigOverride(State, true, 1, 0, Changed);
	EXPECT_TRUE(Changed);
	EXPECT_EQ(Value, 0);
	EXPECT_TRUE(State.m_WasActive);
	EXPECT_EQ(State.m_SavedValue, 1);

	Value = ApplyQmConfigOverride(State, false, 0, 0, Changed);
	EXPECT_TRUE(Changed);
	EXPECT_EQ(Value, 1);
	EXPECT_FALSE(State.m_WasActive);
}

TEST(QmFocusMode, ConfigOverrideKeepsUserChangesMadeWhileActive)
{
	SQmConfigOverrideState State;
	bool Changed = false;

	EXPECT_EQ(ApplyQmConfigOverride(State, true, 1, 0, Changed), 0);
	EXPECT_TRUE(Changed);

	const int UserChangedValue = 2;
	EXPECT_EQ(ApplyQmConfigOverride(State, false, UserChangedValue, 0, Changed), UserChangedValue);
	EXPECT_FALSE(Changed);
	EXPECT_FALSE(State.m_WasActive);
}

TEST(QmFocusMode, HudScoreboardNamesAndNameplatesRequireFocusModeAndTheirOwnToggle)
{
	EXPECT_TRUE(ShouldHideFocusHud(true, true));
	EXPECT_FALSE(ShouldHideFocusHud(true, false));
	EXPECT_FALSE(ShouldHideFocusHud(false, true));

	EXPECT_TRUE(ShouldHideFocusScoreboard(true, true));
	EXPECT_FALSE(ShouldHideFocusScoreboard(true, false));
	EXPECT_FALSE(ShouldHideFocusScoreboard(false, true));

	EXPECT_TRUE(ShouldHideFocusNames(true, true));
	EXPECT_FALSE(ShouldHideFocusNames(true, false));
	EXPECT_FALSE(ShouldHideFocusNames(false, true));

	EXPECT_TRUE(ShouldHideFocusNameplates(true, true));
	EXPECT_FALSE(ShouldHideFocusNameplates(true, false));
	EXPECT_FALSE(ShouldHideFocusNameplates(false, true));
}

TEST(QmFocusMode, SpectatorHudStaysVisibleWhenFocusModeAutoHidesMainHud)
{
	EXPECT_TRUE(ShouldRenderFocusSpectatorHud(true, true, false, true, true));
	EXPECT_TRUE(ShouldRenderFocusSpectatorHud(true, true, true, true, true));
	EXPECT_FALSE(ShouldRenderFocusSpectatorHud(false, true, false, true, true));
	EXPECT_FALSE(ShouldRenderFocusSpectatorHud(true, false, false, true, true));
	EXPECT_FALSE(ShouldRenderFocusSpectatorHud(true, true, false, false, true));
	EXPECT_FALSE(ShouldRenderFocusSpectatorHud(true, true, false, true, false));
}

TEST(QmFocusMode, VisualEffectChildrenDoNotInheritTheLegacyVisualParentToggle)
{
	EXPECT_FALSE(ShouldHideFocusJumpEffects(true, false));
	EXPECT_TRUE(ShouldHideFocusJumpEffects(true, true));
	EXPECT_FALSE(ShouldHideFocusKillEffects(true, false));
	EXPECT_TRUE(ShouldHideFocusKillEffects(true, true));
	EXPECT_FALSE(ShouldHideFocusExplosionEffects(true, false));
	EXPECT_TRUE(ShouldHideFocusExplosionEffects(true, true));
	EXPECT_FALSE(ShouldHideFocusFreezeEffects(true, false));
	EXPECT_TRUE(ShouldHideFocusFreezeEffects(true, true));
	EXPECT_FALSE(ShouldHideFocusFreezeEffects(false, true));
	EXPECT_FALSE(ShouldHideFocusHammerEffects(true, false));
	EXPECT_TRUE(ShouldHideFocusHammerEffects(true, true));
	EXPECT_FALSE(ShouldHideFocusHammerEffects(false, true));
	EXPECT_FALSE(ShouldHideFocusMuzzleEffects(true, false));
	EXPECT_TRUE(ShouldHideFocusMuzzleEffects(true, true));
	EXPECT_FALSE(ShouldHideFocusMuzzleEffects(false, true));
	EXPECT_FALSE(ShouldHideFocusJumpEffects(false, true));
	EXPECT_FALSE(ShouldHideFocusKillEffects(false, true));
	EXPECT_FALSE(ShouldHideFocusExplosionEffects(false, true));
}

TEST(QmFocusMode, MapProgressAndInfoMessagesUseTheirOwnChildToggles)
{
	EXPECT_FALSE(ShouldHideFocusMapProgress(true, false));
	EXPECT_TRUE(ShouldHideFocusMapProgress(true, true));
	EXPECT_FALSE(ShouldHideFocusInfoMessages(true, false));
	EXPECT_TRUE(ShouldHideFocusInfoMessages(true, true));
	EXPECT_FALSE(ShouldHideFocusMapProgress(false, true));
	EXPECT_FALSE(ShouldHideFocusInfoMessages(false, true));
}

TEST(QmFocusMode, IndependentMapProgressUsesItsOwnToggleAndBottomStyle)
{
	EXPECT_FALSE(ShouldRenderMapProgressBar(false, 0, false, true));
	EXPECT_TRUE(ShouldRenderMapProgressBar(true, 1, false, true));
	EXPECT_FALSE(ShouldRenderMapProgressBar(true, 1, true, true));
	EXPECT_FALSE(ShouldRenderMapProgressBar(true, 0, false, false));
	EXPECT_TRUE(ShouldRenderMapProgressBar(true, 0, false, true));
}

TEST(QmFocusMode, JumpSoundMuteIsIndependentFromJumpVisualEffects)
{
	EXPECT_TRUE(ShouldPlayFocusJumpSound(true, false, true));
	EXPECT_FALSE(ShouldPlayFocusJumpSound(true, true, true));
	EXPECT_TRUE(ShouldPlayFocusJumpSound(false, true, true));
	EXPECT_FALSE(ShouldPlayFocusJumpSound(true, false, false));
}

TEST(QmFocusMode, DeathOrSpawnSoundUsesDeathSoundMuteToggle)
{
	EXPECT_TRUE(ShouldPlayFocusDeathOrSpawnSound(true, false, true));
	EXPECT_FALSE(ShouldPlayFocusDeathOrSpawnSound(true, true, true));
	EXPECT_TRUE(ShouldPlayFocusDeathOrSpawnSound(false, true, true));
	EXPECT_FALSE(ShouldPlayFocusDeathOrSpawnSound(true, false, false));
}

TEST(QmFocusMode, HammerSoundMuteRequiresFocusModeAndHammerSoundToggle)
{
	EXPECT_FALSE(ShouldMuteFocusHammerSounds(true, false));
	EXPECT_TRUE(ShouldMuteFocusHammerSounds(true, true));
	EXPECT_FALSE(ShouldMuteFocusHammerSounds(false, true));
}

TEST(QmFocusMode, AirJumpDecisionSeparatesParticlesAndSound)
{
	SQmAirJumpEffectDecision Decision = GetQmAirJumpEffectDecision(true, false, true, true);
	EXPECT_TRUE(Decision.m_SpawnParticles);
	EXPECT_FALSE(Decision.m_PlaySound);

	Decision = GetQmAirJumpEffectDecision(true, true, false, true);
	EXPECT_FALSE(Decision.m_SpawnParticles);
	EXPECT_TRUE(Decision.m_PlaySound);

	Decision = GetQmAirJumpEffectDecision(true, false, false, false);
	EXPECT_TRUE(Decision.m_SpawnParticles);
	EXPECT_FALSE(Decision.m_PlaySound);
}

TEST(QmFocusMode, DirectionIndicatorsAndGuideLinesAreControlledSeparately)
{
	EXPECT_TRUE(ShouldHideFocusDirectionIndicators(true, true));
	EXPECT_FALSE(ShouldHideFocusDirectionIndicators(true, false));
	EXPECT_FALSE(ShouldHideFocusDirectionIndicators(false, true));

	EXPECT_TRUE(ShouldHideFocusGuideLines(true, true));
	EXPECT_FALSE(ShouldHideFocusGuideLines(true, false));
	EXPECT_FALSE(ShouldHideFocusGuideLines(false, true));
}

TEST(QmFocusMode, ForceVisibleClientLinesRemainVisibleWhenChatIsHidden)
{
	EXPECT_TRUE(ShouldRenderFocusFilteredChatLine(true, true, true, true, -2, true, false));
	EXPECT_FALSE(ShouldRenderAnyFocusFilteredChat(true, true, true, true, false));
	EXPECT_TRUE(ShouldRenderAnyFocusFilteredChat(true, true, true, true, true));
}

TEST(QmFocusMode, ChatFiltersSeparatePlayerSystemAndEchoMessages)
{
	EXPECT_FALSE(ShouldRenderFocusFilteredChatLine(true, false, false, false, 3, false, false));
	EXPECT_TRUE(ShouldRenderFocusFilteredChatLine(true, false, false, false, -1, false, true));
	EXPECT_TRUE(ShouldRenderFocusFilteredChatLine(true, false, false, false, -1, false, false));
	EXPECT_TRUE(ShouldRenderFocusFilteredChatLine(true, false, false, false, -2, false, false));

	EXPECT_FALSE(ShouldRenderFocusFilteredChatLine(false, true, false, false, -1, false, true));
	EXPECT_TRUE(ShouldRenderFocusFilteredChatLine(false, true, false, false, -1, false, false));
	EXPECT_TRUE(ShouldRenderFocusFilteredChatLine(false, true, false, false, 3, false, false));
	EXPECT_TRUE(ShouldRenderFocusFilteredChatLine(false, true, false, false, -2, false, false));

	EXPECT_FALSE(ShouldRenderFocusFilteredChatLine(false, false, true, false, -1, false, false));
	EXPECT_TRUE(ShouldRenderFocusFilteredChatLine(false, false, true, false, -1, false, true));
	EXPECT_TRUE(ShouldRenderFocusFilteredChatLine(false, false, true, false, 3, false, false));
	EXPECT_TRUE(ShouldRenderFocusFilteredChatLine(false, false, true, false, -2, false, false));

	EXPECT_FALSE(ShouldRenderFocusFilteredChatLine(false, false, false, true, -2, false, false));
	EXPECT_TRUE(ShouldRenderFocusFilteredChatLine(false, false, false, true, 3, false, false));
	EXPECT_TRUE(ShouldRenderFocusFilteredChatLine(false, false, false, true, -1, false, false));
}

TEST(QmFocusMode, ConfigSnapshotSeparatesChatMessageClasses)
{
	SQmFocusModeConfig Config;
	Config.m_FocusActive = true;
	Config.m_HidePlayerMessages = true;
	Config.m_HideSystemInfoMessages = false;
	Config.m_HideSystemPromptMessages = true;
	Config.m_HideEchoMessages = true;
	Config.m_HideHud = true;
	Config.m_HideScoreboard = true;
	Config.m_HideNames = true;
	Config.m_HideNameplates = true;

	const SQmFocusModeDecisions Decisions = GetQmFocusModeDecisions(Config);
	EXPECT_TRUE(Decisions.m_HideHud);
	EXPECT_TRUE(Decisions.m_HideScoreboard);
	EXPECT_TRUE(Decisions.m_HideNames);
	EXPECT_TRUE(Decisions.m_HideNameplates);
	EXPECT_FALSE(ShouldRenderFocusFilteredChatLine(Decisions.m_HidePlayerMessages, Decisions.m_HideSystemInfoMessages, Decisions.m_HideSystemPromptMessages, Decisions.m_HideEchoMessages, 0, false, false));
	EXPECT_TRUE(ShouldRenderFocusFilteredChatLine(Decisions.m_HidePlayerMessages, Decisions.m_HideSystemInfoMessages, Decisions.m_HideSystemPromptMessages, Decisions.m_HideEchoMessages, -1, false, true));
	EXPECT_FALSE(ShouldRenderFocusFilteredChatLine(Decisions.m_HidePlayerMessages, Decisions.m_HideSystemInfoMessages, Decisions.m_HideSystemPromptMessages, Decisions.m_HideEchoMessages, -1, false, false));
	EXPECT_FALSE(ShouldRenderFocusFilteredChatLine(Decisions.m_HidePlayerMessages, Decisions.m_HideSystemInfoMessages, Decisions.m_HideSystemPromptMessages, Decisions.m_HideEchoMessages, -2, false, false));
}

TEST(QmFocusMode, ConfigSnapshotMapProgressRequiresStyleAndGoresProgressAndChildToggle)
{
	SQmFocusModeConfig Config;
	Config.m_FocusActive = true;
	Config.m_MapProgressEnabled = true;
	Config.m_MapProgressStyle = 0;
	Config.m_PlayerStatsHudEnabled = false;
	Config.m_GoresMapProgressEnabled = true;
	Config.m_HideMapProgress = false;

	EXPECT_TRUE(GetQmFocusModeDecisions(Config).m_RenderMapProgressBar);

	Config.m_MapProgressStyle = 1;
	EXPECT_TRUE(GetQmFocusModeDecisions(Config).m_RenderMapProgressBar);

	Config.m_PlayerStatsHudEnabled = true;
	EXPECT_FALSE(GetQmFocusModeDecisions(Config).m_RenderMapProgressBar);

	Config.m_MapProgressStyle = 0;
	Config.m_PlayerStatsHudEnabled = false;
	Config.m_HideMapProgress = true;
	EXPECT_FALSE(GetQmFocusModeDecisions(Config).m_RenderMapProgressBar);
}
