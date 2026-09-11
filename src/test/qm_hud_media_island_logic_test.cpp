// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/QmUi/QmIslandNotice.h>
#include <game/client/components/hud_frozen_tee_state.h>
#include <game/client/components/hud_media_island_logic.h>
#include <game/client/components/tclient/pet.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

namespace
{
	std::string FunctionBody(const std::string &Source, const std::string &Signature)
	{
		const size_t FunctionStart = Source.find(Signature);
		if(FunctionStart == std::string::npos)
			return {};
		const size_t BodyStart = Source.find('{', FunctionStart);
		if(BodyStart == std::string::npos)
			return {};
		int Depth = 0;
		for(size_t Index = BodyStart; Index < Source.size(); ++Index)
		{
			if(Source[Index] == '{')
				++Depth;
			else if(Source[Index] == '}' && --Depth == 0)
				return Source.substr(BodyStart, Index - BodyStart);
		}
		return {};
	}

	SHudMediaIslandTrackInput Track(const char *pTitle, const char *pArtist = "", const char *pAlbum = "")
	{
		SHudMediaIslandTrackInput Input;
		Input.m_pTitle = pTitle;
		Input.m_pArtist = pArtist;
		Input.m_pAlbum = pAlbum;
		return Input;
	}

	EHudMediaIslandTrackUpdate ApplyTrack(
		SHudMediaIslandTrackSnapshot &Current,
		SHudMediaIslandTrackSnapshot &Outgoing,
		bool &HasIdentity,
		bool &TransitionActive,
		bool &NeedsNodeReset,
		int64_t &StartTick,
		int64_t Now,
		const SHudMediaIslandTrackInput &Input)
	{
		return QmHudMediaIslandUpdateTrackSnapshots(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, Now, Input);
	}

	void AdvanceIslandRuntime(CUiV2AnimationRuntime &Runtime, float Seconds)
	{
		const float Dt = 1.0f / 60.0f;
		int Steps = static_cast<int>(Seconds / Dt) + 1;
		for(int i = 0; i < Steps; ++i)
			Runtime.Advance(Dt);
	}

	// 以固定帧步推进分离弹簧；FrameSeconds 可用来验证帧率无关性。
	// 步数向上取整，保证请求的时长一定被走完（否则会差一帧、落在窗口之前）；
	// Seconds == 0 时一步都不走——那是"只切目标、不推进时间"的用法。
	const int StepCount(float Seconds, float FrameSeconds)
	{
		if(Seconds <= 0.0f)
			return 0;
		return static_cast<int>(Seconds / FrameSeconds + 0.999f);
	}

	float StepBlobSpring(SHudMediaIslandBlobSpring &Spring, bool TargetVisible, float Seconds, float FrameSeconds = 1.0f / 60.0f)
	{
		const float Period = QmHudMediaIslandBlobSpringWindowSeconds();
		const int Steps = StepCount(Seconds, FrameSeconds);
		for(int i = 0; i < Steps; ++i)
			QmHudMediaIslandBlobSpringAdvance(Spring, FrameSeconds, Period, TargetVisible);
		return QmHudMediaIslandBlobProgress(Spring);
	}

	float PeakBlobTravel(SHudMediaIslandBlobSpring &Spring, bool TargetVisible, float Seconds, float FrameSeconds = 1.0f / 240.0f)
	{
		const float Period = QmHudMediaIslandBlobSpringWindowSeconds();
		float Peak = 0.0f;
		const int Steps = std::max(1, static_cast<int>(Seconds / FrameSeconds));
		for(int i = 0; i < Steps; ++i)
		{
			QmHudMediaIslandBlobSpringAdvance(Spring, FrameSeconds, Period, TargetVisible);
			Peak = std::max(Peak, QmHudMediaIslandBlobPose(Spring).m_Travel);
		}
		return Peak;
	}
}

TEST(QmHudFrozenTeeState, ConfirmedDeathSuppressesStaleTimedAndDeepFreeze)
{
	SHudFrozenTeeState State;
	EXPECT_TRUE(QmHudTeeIsFrozen(State, 200, false));
	EXPECT_TRUE(QmHudTeeIsFrozen(State, -1, true));

	QmHudMarkTeeDead(State, 100, 105);

	EXPECT_TRUE(State.m_DeathOverride);
	EXPECT_EQ(State.m_DeathBarrierTick, 105);
	EXPECT_FALSE(QmHudTeeIsFrozen(State, 200, false));
	EXPECT_FALSE(QmHudTeeIsFrozen(State, -1, true));
}

TEST(QmHudFrozenTeeState, BufferedPreDeathCharacterCannotRemoveDeathOverride)
{
	SHudFrozenTeeState State;
	QmHudMarkTeeDead(State, 100, 105);

	QmHudObserveTeeCharacterSnapshot(State, true, 104);
	EXPECT_TRUE(State.m_DeathOverride);
	QmHudObserveTeeCharacterSnapshot(State, true, 105);
	EXPECT_TRUE(State.m_DeathOverride);
}

TEST(QmHudFrozenTeeState, NetworkClippingDoesNotPretendTheTeeRespawned)
{
	SHudFrozenTeeState AliveState;
	QmHudObserveTeeCharacterSnapshot(AliveState, false, 200);
	EXPECT_TRUE(QmHudTeeIsFrozen(AliveState, 250, false));

	SHudFrozenTeeState DeadState;
	QmHudMarkTeeDead(DeadState, 100, 105);
	QmHudObserveTeeCharacterSnapshot(DeadState, false, 200);
	EXPECT_TRUE(DeadState.m_DeathOverride);
	EXPECT_FALSE(QmHudTeeIsFrozen(DeadState, 250, false));
}

TEST(QmHudFrozenTeeState, FreshRespawnUsesTheNewFreezeState)
{
	SHudFrozenTeeState State;
	QmHudMarkTeeDead(State, 100, 105);

	QmHudObserveTeeCharacterSnapshot(State, true, 106);

	EXPECT_FALSE(State.m_DeathOverride);
	EXPECT_EQ(State.m_DeathBarrierTick, -1);
	EXPECT_FALSE(QmHudTeeIsFrozen(State, 0, false));
	EXPECT_TRUE(QmHudTeeIsFrozen(State, 200, false));
	EXPECT_TRUE(QmHudTeeIsFrozen(State, -1, true));
}

TEST(QmHudFrozenTeeSource, TracksConfirmedKillsWithoutTreatingDeathEffectsAsKills)
{
	const std::string CMakeSource = ReadTestSourceFile("CMakeLists.txt");
	const std::string GameClientSource = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const std::string GameClientHeader = ReadTestSourceFile("src/game/client/gameclient.h");
	const std::string HudSource = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string OnMessageBody = FunctionBody(GameClientSource, "void CGameClient::OnMessage(");
	const std::string ProcessEventsBody = FunctionBody(GameClientSource, "void CGameClient::ProcessEvents()");
	const std::string ResetDemoPlaybackStateBody = FunctionBody(GameClientSource, "void CGameClient::ResetDemoPlaybackState()");
	const std::string OnNewSnapshotBody = FunctionBody(GameClientSource, "void CGameClient::OnNewSnapshot()");
	const std::string FrozenTeamInfoBody = FunctionBody(HudSource, "SHudFrozenTeamInfo BuildHudFrozenTeamInfo(");
	const std::string RenderTextInfoBody = FunctionBody(HudSource, "void CHud::RenderTextInfo()");

	EXPECT_NE(CMakeSource.find("components/hud_frozen_tee_state.h"), std::string::npos);
	EXPECT_NE(GameClientHeader.find("SHudFrozenTeeState m_HudFrozenTeeState"), std::string::npos);
	EXPECT_NE(OnMessageBody.find("QmHudMarkTeeDead(m_aClients[pMsg->m_Victim].m_HudFrozenTeeState"), std::string::npos);
	EXPECT_NE(OnMessageBody.find("QmHudMarkTeeDead(m_aClients[i].m_HudFrozenTeeState"), std::string::npos);
	EXPECT_EQ(ProcessEventsBody.find("QmHudMarkTeeDead"), std::string::npos);
	EXPECT_NE(ResetDemoPlaybackStateBody.find("Client.m_HudFrozenTeeState = {}"), std::string::npos);
	EXPECT_NE(OnNewSnapshotBody.find("QmHudObserveTeeCharacterSnapshot"), std::string::npos);
	EXPECT_NE(FrozenTeamInfoBody.find("QmHudTeeIsFrozen"), std::string::npos);
	EXPECT_NE(RenderTextInfoBody.find("QmHudTeeIsFrozen"), std::string::npos);
}

TEST(QmHudMediaIslandSource, RemovedTuningSatelliteDoesNotRemain)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string Header = ReadTestSourceFile("src/game/client/components/hud.h");
	const std::string CountdownLogic = ReadTestSourceFile("src/game/client/components/hud_media_island_logic.h");
	const std::string Menus = ReadTestSourceFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Config = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	const std::string IconHeader = ReadTestSourceFile("src/game/client/qm_icon_manager.h");
	const std::string IconSource = ReadTestSourceFile("src/game/client/qm_icon_manager.cpp");

	EXPECT_EQ(Source.find("TuneZoneEffect"), std::string::npos);
	EXPECT_EQ(Header.find("TuneZoneEffect"), std::string::npos);
	EXPECT_EQ(CountdownLogic.find("TUNE_ZONE"), std::string::npos);
	EXPECT_EQ(Menus.find("qmclient-dynamic-island-tune-zone-icon-legend"), std::string::npos);
	EXPECT_EQ(Config.find("QmHudIslandShowTuneZoneEffects"), std::string::npos);
	EXPECT_EQ(IconHeader.find("TUNE_GRAVITY"), std::string::npos);
	EXPECT_EQ(IconSource.find("tune-gravity"), std::string::npos);
}

TEST(QmHudMediaIslandSource, DynamicIslandUsesCompactSharedSpacing)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");

	EXPECT_NE(Source.find("QmHudMediaIslandScaled(2.0f)"), std::string::npos);
	EXPECT_NE(Source.find("QmHudMediaIslandScaled(3.0f)"), std::string::npos);
	EXPECT_NE(Source.find("QmHudMediaIslandScaled(7.0f)"), std::string::npos);
	EXPECT_NE(Source.find("QmHudMediaIslandScaled(5.0f)"), std::string::npos);
}

TEST(QmHudDummyMiniViewSource, VulkanUsesOffscreenTargetForEveryVendor)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string Header = ReadTestSourceFile("src/game/client/components/hud.h");
	const std::string VulkanSource = ReadTestSourceFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const std::string RectBody = FunctionBody(Source, "bool CHud::GetDummyMiniMapRect(");
	const std::string RenderBody = FunctionBody(Source, "void CHud::RenderDummyMiniMap(");
	const std::string ReleaseBody = FunctionBody(Source, "void CHud::OnRelease(");

	ASSERT_FALSE(RectBody.empty());
	ASSERT_FALSE(RenderBody.empty());
	ASSERT_FALSE(ReleaseBody.empty());
	EXPECT_EQ(Source.find("IsVulkanAmdBackend"), std::string::npos);
	EXPECT_EQ(Source.find("known driver crash"), std::string::npos);
	EXPECT_NE(Source.find("bool IsVulkanBackend(IGraphics *pGraphics)"), std::string::npos);
	EXPECT_NE(Source.find("GetDetectedContextVersion"), std::string::npos);
	EXPECT_NE(Header.find("m_DummyMiniViewRenderTarget"), std::string::npos);
	EXPECT_NE(Header.find("DestroyDummyMiniViewRenderTarget"), std::string::npos);
	EXPECT_EQ(RectBody.find("Vulkan"), std::string::npos);
	EXPECT_NE(RenderBody.find("IsVulkanBackend(Graphics())"), std::string::npos);
	EXPECT_NE(RenderBody.find("Graphics()->BeginRenderTarget(m_DummyMiniViewRenderTarget"), std::string::npos);
	EXPECT_NE(RenderBody.find("Graphics()->EndRenderTarget()"), std::string::npos);
	EXPECT_NE(RenderBody.find("Graphics()->DrawRenderTarget(m_DummyMiniViewRenderTarget"), std::string::npos);
	EXPECT_NE(RenderBody.find("DrawParams.m_V0 = 0.0f;"), std::string::npos);
	EXPECT_NE(RenderBody.find("DrawParams.m_V1 = 1.0f;"), std::string::npos);
	EXPECT_NE(ReleaseBody.find("DestroyDummyMiniViewRenderTarget"), std::string::npos);
	EXPECT_EQ(VulkanSource.find("VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT"), std::string::npos);
	EXPECT_NE(VulkanSource.find("MultiSamplingColorAttachment.storeOp = HasMultiSamplingTargets ? VK_ATTACHMENT_STORE_OP_STORE"), std::string::npos);
	EXPECT_NE(VulkanSource.find("LoadAttachments ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : InitialLayout"), std::string::npos);
}

TEST(QmHudMediaIslandLayout, ScalesTheCompleteDesignToEightyPercent)
{
	EXPECT_FLOAT_EQ(QmHudMediaIslandDesignScale, 0.8f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandScaled(16.0f), 12.8f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandScaled(12.0f), 9.6f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandScaled(5.8f), 4.64f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandScaled(3.0f), 2.4f);
}

// 意图：无媒体、无队伍内容、无左侧倒计时副岛时不得为不存在的主内容保留空胶囊，
// 否则药丸左侧会白留一段谁都不画的空槽（看起来就是占位符）；
// 观战卫星长在主岛右侧，同样不构成保留依据。
TEST(QmHudMediaIslandLayout, EmptyMainCapsuleIsNotReservedWithoutContentOrCountdownSatellite)
{
	EXPECT_FALSE(QmHudMediaIslandShouldReserveMainCapsule(false, false, false));
	EXPECT_TRUE(QmHudMediaIslandShouldReserveMainCapsule(true, false, false));
	EXPECT_TRUE(QmHudMediaIslandShouldReserveMainCapsule(false, true, false));
	EXPECT_TRUE(QmHudMediaIslandShouldReserveMainCapsule(false, false, true));
	EXPECT_TRUE(QmHudMediaIslandShouldReserveMainCapsule(true, true, true));
}

TEST(QmHudMediaIslandLayout, InfoStackMirrorsRowsAroundHorizontalMidlineWithCompactGap)
{
	constexpr float IslandY = 1.0f;
	constexpr float IslandHeight = QmHudMediaIslandScaled(16.0f);
	constexpr float TextHeight = QmHudMediaIslandScaled(4.4f);
	constexpr float TextGap = QmHudMediaIslandScaled(0.8f);
	const SHudMediaIslandInfoStackLayout Layout = QmHudMediaIslandMirroredInfoStack(IslandY, IslandHeight, TextHeight, TextGap);
	const float MidY = IslandY + IslandHeight * 0.5f;

	EXPECT_FLOAT_EQ(MidY - Layout.m_TopCenterY, Layout.m_BottomCenterY - MidY);
	EXPECT_FLOAT_EQ(Layout.m_BottomCenterY - Layout.m_TopCenterY, QmHudMediaIslandScaled(5.2f));
	EXPECT_NEAR(
		(Layout.m_BottomCenterY - TextHeight * 0.5f) - (Layout.m_TopCenterY + TextHeight * 0.5f),
		TextGap,
		0.0001f);
}

TEST(QmHudMediaIslandLogic, FirstMediaStateDoesNotStartTrackTransition)
{
	SHudMediaIslandTrackSnapshot Current;
	SHudMediaIslandTrackSnapshot Outgoing;
	bool HasIdentity = false;
	bool TransitionActive = false;
	bool NeedsNodeReset = false;
	int64_t StartTick = 0;

	const EHudMediaIslandTrackUpdate Update = ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 100, Track("Song A", "Artist A"));

	EXPECT_EQ(Update, EHudMediaIslandTrackUpdate::FIRST_IDENTITY);
	EXPECT_TRUE(HasIdentity);
	EXPECT_FALSE(TransitionActive);
	EXPECT_FALSE(NeedsNodeReset);
	EXPECT_STREQ(Current.m_aTitle, "Song A");
	EXPECT_FALSE(Outgoing.HasMeaningfulIdentity());
}

TEST(QmHudMediaIslandLogic, FirstOrLateMetadataStartsTheTrackDetailsDeadline)
{
	EXPECT_TRUE(QmHudMediaIslandShouldRevealTrackDetails(EHudMediaIslandTrackUpdate::FIRST_IDENTITY, false, true));
	EXPECT_TRUE(QmHudMediaIslandShouldRevealTrackDetails(EHudMediaIslandTrackUpdate::NONE, false, true));
	EXPECT_FALSE(QmHudMediaIslandShouldRevealTrackDetails(EHudMediaIslandTrackUpdate::NONE, true, true));
	EXPECT_FALSE(QmHudMediaIslandShouldRevealTrackDetails(EHudMediaIslandTrackUpdate::FIRST_IDENTITY, false, false));
	EXPECT_TRUE(QmHudMediaIslandShouldRevealTrackDetails(EHudMediaIslandTrackUpdate::TRACK_CHANGED, true, true));
}

TEST(QmHudMediaIslandLogic, TrackChangeCopiesCurrentSnapshotToOutgoing)
{
	SHudMediaIslandTrackSnapshot Current;
	SHudMediaIslandTrackSnapshot Outgoing;
	bool HasIdentity = false;
	bool TransitionActive = false;
	bool NeedsNodeReset = false;
	int64_t StartTick = 0;

	ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 100, Track("Song A", "Artist A", "Album A"));
	const EHudMediaIslandTrackUpdate Update = ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 200, Track("Song B", "Artist B", "Album B"));

	EXPECT_EQ(Update, EHudMediaIslandTrackUpdate::TRACK_CHANGED);
	EXPECT_TRUE(TransitionActive);
	EXPECT_TRUE(NeedsNodeReset);
	EXPECT_EQ(StartTick, 200);
	EXPECT_STREQ(Outgoing.m_aTitle, "Song A");
	EXPECT_STREQ(Current.m_aTitle, "Song B");
}

TEST(QmHudMediaIslandLogic, ContinuousTrackChangesKeepOnlyLatestTrack)
{
	SHudMediaIslandTrackSnapshot Current;
	SHudMediaIslandTrackSnapshot Outgoing;
	bool HasIdentity = false;
	bool TransitionActive = false;
	bool NeedsNodeReset = false;
	int64_t StartTick = 0;

	ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 100, Track("Song A", "Artist A"));
	ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 200, Track("Song B", "Artist B"));
	const EHudMediaIslandTrackUpdate Update = ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 250, Track("Song C", "Artist C"));

	EXPECT_EQ(Update, EHudMediaIslandTrackUpdate::TRACK_CHANGED);
	EXPECT_STREQ(Outgoing.m_aTitle, "Song B");
	EXPECT_STREQ(Current.m_aTitle, "Song C");
	EXPECT_TRUE(TransitionActive);
}

TEST(QmHudMediaIslandLogic, TitleChangeWithEmptyArtistStillCountsAsTrackChange)
{
	SHudMediaIslandTrackSnapshot Current;
	Current.SetFrom(Track("Song A"));

	EXPECT_TRUE(QmHudMediaIslandTrackChanged(Current, Track("Song B")));
}

TEST(QmHudMediaIslandLogic, SameTrackRefreshDoesNotRestartTransition)
{
	SHudMediaIslandTrackSnapshot Current;
	SHudMediaIslandTrackSnapshot Outgoing;
	bool HasIdentity = false;
	bool TransitionActive = false;
	bool NeedsNodeReset = false;
	int64_t StartTick = 0;

	ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 100, Track("Song A", "Artist A"));
	const EHudMediaIslandTrackUpdate Update = ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 200, Track("Song A", "Artist A"));

	EXPECT_EQ(Update, EHudMediaIslandTrackUpdate::NONE);
	EXPECT_FALSE(TransitionActive);
	EXPECT_FALSE(NeedsNodeReset);
	EXPECT_FALSE(Outgoing.HasMeaningfulIdentity());
}

TEST(QmHudMediaIslandLogic, EmptyMetadataRefreshKeepsLastStableSnapshot)
{
	SHudMediaIslandTrackSnapshot Current;
	SHudMediaIslandTrackSnapshot Outgoing;
	bool HasIdentity = false;
	bool TransitionActive = false;
	bool NeedsNodeReset = false;
	int64_t StartTick = 0;

	ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 100, Track("Song A", "Artist A"));
	const EHudMediaIslandTrackUpdate Update = ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 200, Track(""));

	EXPECT_EQ(Update, EHudMediaIslandTrackUpdate::NONE);
	EXPECT_STREQ(Current.m_aTitle, "Song A");
	EXPECT_STREQ(Current.m_aArtist, "Artist A");
	EXPECT_FALSE(TransitionActive);
}

TEST(QmHudMediaIslandLogic, MissingCoverSnapshotsStillSwitch)
{
	SHudMediaIslandTrackSnapshot Current;
	SHudMediaIslandTrackSnapshot Outgoing;
	bool HasIdentity = false;
	bool TransitionActive = false;
	bool NeedsNodeReset = false;
	int64_t StartTick = 0;

	ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 100, Track("Song A"));
	const EHudMediaIslandTrackUpdate Update = ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 200, Track("Song B"));

	EXPECT_EQ(Update, EHudMediaIslandTrackUpdate::TRACK_CHANGED);
	EXPECT_FALSE(Current.m_HasCover);
	EXPECT_FALSE(Outgoing.m_HasCover);
}

TEST(QmHudMediaIslandLogic, Utf8TitlesStayNulTerminatedInFixedSnapshot)
{
	SHudMediaIslandTrackSnapshot Current;
	const char *pTitle = "很长的中文标题 Mixed UTF-8 Title 很长的中文标题 Mixed UTF-8 Title 很长的中文标题 Mixed UTF-8 Title 很长的中文标题 Mixed UTF-8 Title";

	Current.SetFrom(Track(pTitle, "艺术家"));

	EXPECT_NE(Current.m_aTitle[0], '\0');
	EXPECT_EQ(Current.m_aTitle[sizeof(Current.m_aTitle) - 1], '\0');
	EXPECT_STREQ(Current.m_aArtist, "艺术家");
}

TEST(QmHudMediaIslandLogic, TopEffectMovesBelowOverlappingIsland)
{
	const CUIRect Island = {120.0f, 1.0f, 80.0f, 38.0f};

	EXPECT_FLOAT_EQ(QmHudTopEffectY(20.0f, 10.0f, 140.0f, 180.0f, Island, true), 42.0f);
}

TEST(QmHudMediaIslandLogic, TopEffectDoesNotMoveForHorizontalSeparationOrHiddenIsland)
{
	const CUIRect SideIsland = {20.0f, 1.0f, 60.0f, 38.0f};
	const CUIRect CenterIsland = {120.0f, 1.0f, 80.0f, 38.0f};

	EXPECT_FLOAT_EQ(QmHudTopEffectY(20.0f, 10.0f, 140.0f, 180.0f, SideIsland, true), 20.0f);
	EXPECT_FLOAT_EQ(QmHudTopEffectY(20.0f, 10.0f, 140.0f, 180.0f, CenterIsland, false), 20.0f);
}

TEST(QmHudMediaIslandLogic, TopEffectDoesNotMoveForIslandBelowIt)
{
	const CUIRect LowerIsland = {120.0f, 100.0f, 80.0f, 38.0f};

	EXPECT_FLOAT_EQ(QmHudTopEffectY(20.0f, 24.0f, 140.0f, 180.0f, LowerIsland, true), 20.0f);
}

TEST(QmHudMediaIslandLogic, TeamZeroDoesNotCreateTeamDisplay)
{
	EXPECT_FALSE(QmHudMediaIslandShouldShowTeam(true, true, 0));
	EXPECT_TRUE(QmHudMediaIslandShouldShowTeam(true, true, 1));
	EXPECT_FALSE(QmHudMediaIslandShouldShowTeam(false, true, 1));
	EXPECT_FALSE(QmHudMediaIslandShouldShowTeam(true, false, 1));
}

TEST(QmHudMediaIslandEntrance, StartsAsOpaqueBlackCircleAtTargetCenter)
{
	const CUIRect Target = {100.0f, 1.0f, 80.0f, 32.0f};
	const ColorRGBA TargetColor(0.25f, 0.50f, 0.75f, 0.60f);

	const SHudMediaIslandEntrancePose Pose = QmHudMediaIslandEntrancePose(Target, 8.0f, TargetColor, 0.0f);

	EXPECT_FLOAT_EQ(Pose.m_Rect.x, 133.6f);
	EXPECT_FLOAT_EQ(Pose.m_Rect.y, 10.6f);
	EXPECT_FLOAT_EQ(Pose.m_Rect.w, 12.8f);
	EXPECT_FLOAT_EQ(Pose.m_Rect.h, 12.8f);
	EXPECT_FLOAT_EQ(Pose.m_Radius, 6.4f);
	EXPECT_FLOAT_EQ(Pose.m_DisabledCornerRadius, 6.4f);
	EXPECT_FLOAT_EQ(Pose.m_BackgroundColor.r, 0.0f);
	EXPECT_FLOAT_EQ(Pose.m_BackgroundColor.g, 0.0f);
	EXPECT_FLOAT_EQ(Pose.m_BackgroundColor.b, 0.0f);
	EXPECT_FLOAT_EQ(Pose.m_BackgroundColor.a, 1.0f);
	EXPECT_FLOAT_EQ(Pose.m_ContentAlpha, 0.0f);
}

TEST(QmHudMediaIslandEntrance, SettlesExactlyAtConfiguredAppearance)
{
	const CUIRect Target = {100.0f, 1.0f, 80.0f, 32.0f};
	const ColorRGBA TargetColor(0.25f, 0.50f, 0.75f, 0.60f);

	const SHudMediaIslandEntrancePose Pose = QmHudMediaIslandEntrancePose(Target, 6.0f, TargetColor, 1.0f);

	EXPECT_FLOAT_EQ(Pose.m_Rect.x, Target.x);
	EXPECT_FLOAT_EQ(Pose.m_Rect.y, Target.y);
	EXPECT_FLOAT_EQ(Pose.m_Rect.w, Target.w);
	EXPECT_FLOAT_EQ(Pose.m_Rect.h, Target.h);
	EXPECT_FLOAT_EQ(Pose.m_Radius, 6.0f);
	EXPECT_FLOAT_EQ(Pose.m_DisabledCornerRadius, 0.0f);
	EXPECT_FLOAT_EQ(Pose.m_BackgroundColor.r, TargetColor.r);
	EXPECT_FLOAT_EQ(Pose.m_BackgroundColor.g, TargetColor.g);
	EXPECT_FLOAT_EQ(Pose.m_BackgroundColor.b, TargetColor.b);
	EXPECT_FLOAT_EQ(Pose.m_BackgroundColor.a, TargetColor.a);
	EXPECT_FLOAT_EQ(Pose.m_ContentAlpha, 1.0f);
}

TEST(QmHudMediaIslandEntranceSpring, DropRunsFirstAndExpandWaitsForSettle)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2AnimationRuntime Runtime;

	const SHudMediaIslandEntranceSpringResult Early = QmHudMediaIslandResolveEntranceSprings(Runtime, 101, 102, true);
	EXPECT_NEAR(Early.m_DropProgress, 0.0f, 1e-6f);
	EXPECT_NEAR(Early.m_ExpandProgress, 0.0f, 1e-6f);
	EXPECT_TRUE(Runtime.HasActiveAnimation(101, EUiAnimProperty::ALPHA));
	EXPECT_FALSE(Runtime.HasActiveAnimation(102, EUiAnimProperty::ALPHA));

	// 掉落阶段进行中：展开不启动（阶段语义：掉落先完成，展开才开始）。
	AdvanceIslandRuntime(Runtime, 0.10f);
	const SHudMediaIslandEntranceSpringResult Mid = QmHudMediaIslandResolveEntranceSprings(Runtime, 101, 102, true);
	EXPECT_GT(Mid.m_DropProgress, 0.0f);
	EXPECT_LT(Mid.m_DropProgress, 1.0f);
	EXPECT_NEAR(Mid.m_ExpandProgress, 0.0f, 1e-6f);

	// 落定后展开才推进，两阶段最终都到位。
	AdvanceIslandRuntime(Runtime, 1.0f);
	const SHudMediaIslandEntranceSpringResult Done = QmHudMediaIslandResolveEntranceSprings(Runtime, 101, 102, true);
	EXPECT_NEAR(Done.m_DropProgress, 1.0f, 1e-3f);
	EXPECT_NEAR(Done.m_ExpandProgress, 1.0f, 1e-3f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(101, EUiAnimProperty::ALPHA));
	EXPECT_FALSE(Runtime.HasActiveAnimation(102, EUiAnimProperty::ALPHA));
}

TEST(QmHudMediaIslandEntranceSpring, HiddenRelaxesAndReappearInheritsVelocity)
{
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2AnimationRuntime Runtime;

	QmHudMediaIslandResolveEntranceSprings(Runtime, 201, 202, true);
	AdvanceIslandRuntime(Runtime, 0.06f);
	const SHudMediaIslandEntranceSpringResult BeforeHide = QmHudMediaIslandResolveEntranceSprings(Runtime, 201, 202, true);
	EXPECT_GT(BeforeHide.m_DropProgress, 0.0f);
	EXPECT_LT(BeforeHide.m_DropProgress, 1.0f);

	// 隐藏即打断：目标回落 0，弹簧以当前速度回落（不瞬移）。
	QmHudMediaIslandResolveEntranceSprings(Runtime, 201, 202, false);
	AdvanceIslandRuntime(Runtime, 0.05f);
	const SHudMediaIslandEntranceSpringResult Relaxed = QmHudMediaIslandResolveEntranceSprings(Runtime, 201, 202, false);
	EXPECT_GT(Relaxed.m_DropProgress, 0.0f);
	EXPECT_LT(Relaxed.m_DropProgress, BeforeHide.m_DropProgress);

	// 重现：值连续（不从 0 重放），并继承回落速度平滑掉头继续入场。
	const SHudMediaIslandEntranceSpringResult Reappeared = QmHudMediaIslandResolveEntranceSprings(Runtime, 201, 202, true);
	EXPECT_NEAR(Reappeared.m_DropProgress, Relaxed.m_DropProgress, 1e-4f);
	AdvanceIslandRuntime(Runtime, 0.05f);
	const SHudMediaIslandEntranceSpringResult Continued = QmHudMediaIslandResolveEntranceSprings(Runtime, 201, 202, true);
	EXPECT_GT(Continued.m_DropProgress, Reappeared.m_DropProgress);
	EXPECT_LT(Continued.m_DropProgress, 1.0f);
}

TEST(QmHudMediaIslandEntranceSpring, MotionLevelZeroSnapsToSettled)
{
	g_Config.m_QmUiMotionLevel = 0;
	CUiV2AnimationRuntime Runtime;

	const SHudMediaIslandEntranceSpringResult Result = QmHudMediaIslandResolveEntranceSprings(Runtime, 301, 302, true);
	EXPECT_NEAR(Result.m_DropProgress, 1.0f, 1e-6f);
	EXPECT_NEAR(Result.m_ExpandProgress, 1.0f, 1e-6f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(301, EUiAnimProperty::ALPHA));
	EXPECT_FALSE(Runtime.HasActiveAnimation(302, EUiAnimProperty::ALPHA));
	g_Config.m_QmUiMotionLevel = 2;
}

TEST(QmHudMediaIslandEntranceSpring, ReducedMotionStillAnimatesAndSettles)
{
	g_Config.m_QmUiMotionLevel = 1;
	CUiV2AnimationRuntime Runtime;

	QmHudMediaIslandResolveEntranceSprings(Runtime, 401, 402, true);
	AdvanceIslandRuntime(Runtime, 0.10f);
	const SHudMediaIslandEntranceSpringResult Mid = QmHudMediaIslandResolveEntranceSprings(Runtime, 401, 402, true);
	EXPECT_GT(Mid.m_DropProgress, 0.0f);
	EXPECT_LT(Mid.m_DropProgress, 1.0f);
	EXPECT_NEAR(Mid.m_ExpandProgress, 0.0f, 1e-6f);

	AdvanceIslandRuntime(Runtime, 1.5f);
	const SHudMediaIslandEntranceSpringResult Done = QmHudMediaIslandResolveEntranceSprings(Runtime, 401, 402, true);
	EXPECT_NEAR(Done.m_DropProgress, 1.0f, 1e-3f);
	EXPECT_NEAR(Done.m_ExpandProgress, 1.0f, 1e-3f);
	g_Config.m_QmUiMotionLevel = 2;
}

TEST(QmHudMediaIslandEntranceSpring, CapsuleSqueezeScalesWithAmount)
{
	constexpr float BaseIslandHeight = 32.0f;
	constexpr float PaddingX = 8.0f;
	constexpr float ScreenPadding = 4.0f;
	constexpr float ScreenWidth = 800.0f;

	float X = 0.0f, W = 0.0f, H = 0.0f;
	QmHudMediaIslandApplyCapsuleSqueeze(400.0f, 300.0f, 32.0f, 1.0f, BaseIslandHeight, PaddingX, ScreenPadding, ScreenWidth, X, W, H);
	EXPECT_LT(W, 300.0f);
	EXPECT_GT(H, 30.0f);
	EXPECT_LT(H, 32.0f);
	EXPECT_NEAR(X + W * 0.5f, 400.0f, 1e-4f);

	float NoSqueezeX = 0.0f, NoSqueezeW = 0.0f, NoSqueezeH = 0.0f;
	QmHudMediaIslandApplyCapsuleSqueeze(400.0f, 300.0f, 32.0f, 0.0f, BaseIslandHeight, PaddingX, ScreenPadding, ScreenWidth, NoSqueezeX, NoSqueezeW, NoSqueezeH);
	EXPECT_FLOAT_EQ(NoSqueezeW, 300.0f);
	EXPECT_FLOAT_EQ(NoSqueezeH, 32.0f);
	EXPECT_NEAR(NoSqueezeX + NoSqueezeW * 0.5f, 400.0f, 1e-4f);
}

TEST(QmHudMediaIslandEntrance, DropStartsFullyAboveScreenAndEndsAtExpansionOrigin)
{
	const CUIRect Target = {100.0f, 1.0f, 80.0f, 32.0f};
	const ColorRGBA TargetColor(0.25f, 0.50f, 0.75f, 0.60f);
	constexpr float ScreenTop = -20.0f;

	const SHudMediaIslandEntrancePose Hidden = QmHudMediaIslandEntrancePose(Target, 8.0f, TargetColor, 0.0f, 0.0f, ScreenTop);
	const SHudMediaIslandEntrancePose Arrived = QmHudMediaIslandEntrancePose(Target, 8.0f, TargetColor, 0.0f, 1.0f, ScreenTop);

	EXPECT_LT(Hidden.m_Rect.y + Hidden.m_Rect.h, ScreenTop);
	EXPECT_FLOAT_EQ(Hidden.m_Rect.x + Hidden.m_Rect.w * 0.5f, Target.x + Target.w * 0.5f);
	EXPECT_FLOAT_EQ(Arrived.m_Rect.y, Target.y + Target.h * 0.5f - 6.4f);
	EXPECT_FLOAT_EQ(Arrived.m_Rect.w, 12.8f);
	EXPECT_FLOAT_EQ(Arrived.m_Rect.h, 12.8f);
	EXPECT_FLOAT_EQ(Arrived.m_ContentAlpha, 0.0f);
}

TEST(QmHudMediaIslandEntrance, KeepsContentHiddenUntilShapeNearlySettlesThenFadesItIn)
{
	const CUIRect Target = {100.0f, 1.0f, 80.0f, 32.0f};
	const ColorRGBA TargetColor(0.25f, 0.50f, 0.75f, 0.60f);

	EXPECT_FLOAT_EQ(QmHudMediaIslandEntrancePose(Target, 8.0f, TargetColor, 0.92f).m_ContentAlpha, 0.0f);
	EXPECT_GT(QmHudMediaIslandEntrancePose(Target, 8.0f, TargetColor, 0.96f).m_ContentAlpha, 0.0f);
	EXPECT_LT(QmHudMediaIslandEntrancePose(Target, 8.0f, TargetColor, 0.96f).m_ContentAlpha, 1.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandEntrancePose(Target, 8.0f, TargetColor, 1.0f).m_ContentAlpha, 1.0f);

	const CUIRect WideTarget = {0.0f, 1.0f, 300.0f, 32.0f};
	const SHudMediaIslandEntrancePose FirstVisibleContent = QmHudMediaIslandEntrancePose(WideTarget, 8.0f, TargetColor, 0.93f);
	EXPECT_GT(FirstVisibleContent.m_ContentAlpha, 0.0f);
	EXPECT_LE(FirstVisibleContent.m_Rect.x, WideTarget.x + 2.05f);
	EXPECT_GE(FirstVisibleContent.m_Rect.x + FirstVisibleContent.m_Rect.w, WideTarget.x + WideTarget.w - 2.05f);
}

TEST(QmHudMediaIslandEntrance, IntermediatePoseMorphsGeometryAndConfiguredBackgroundTogether)
{
	const CUIRect Target = {100.0f, 1.0f, 80.0f, 32.0f};
	const ColorRGBA TargetColor(0.25f, 0.50f, 0.75f, 0.60f);

	const SHudMediaIslandEntrancePose Pose = QmHudMediaIslandEntrancePose(Target, 8.0f, TargetColor, 0.5f);

	EXPECT_GT(Pose.m_Rect.w, 12.8f);
	EXPECT_LT(Pose.m_Rect.w, Target.w);
	EXPECT_GT(Pose.m_Rect.h, 12.8f);
	EXPECT_LT(Pose.m_Rect.h, Target.h);
	EXPECT_GT(Pose.m_BackgroundColor.b, 0.0f);
	EXPECT_LT(Pose.m_BackgroundColor.b, TargetColor.b);
	EXPECT_GT(Pose.m_BackgroundColor.a, TargetColor.a);
	EXPECT_LT(Pose.m_BackgroundColor.a, 1.0f);
}

TEST(QmHudMediaIslandSatellite, SortsByTypeThenTriggerOrder)
{
	std::array<SHudMediaIslandCountdownInput, 6> aInputs = {{
		{EHudMediaIslandCountdownType::MUTE, 0, 10, 70, 60},
		{EHudMediaIslandCountdownType::SWITCH, 4, 30, 80, 50},
		{EHudMediaIslandCountdownType::SWAP, 1, 20, 50, 30},
		{EHudMediaIslandCountdownType::SWITCH, 2, 12, 62, 50},
		{EHudMediaIslandCountdownType::SWAP, 0, 5, 35, 30},
		{EHudMediaIslandCountdownType::SWITCH, 9, 30, 90, 60},
	}};

	QmHudSortMediaIslandCountdowns(aInputs.data(), aInputs.size());

	EXPECT_EQ(aInputs[0].m_Type, EHudMediaIslandCountdownType::SWAP);
	EXPECT_EQ(aInputs[0].m_Id, 0);
	EXPECT_EQ(aInputs[1].m_Type, EHudMediaIslandCountdownType::SWAP);
	EXPECT_EQ(aInputs[1].m_Id, 1);
	EXPECT_EQ(aInputs[2].m_Type, EHudMediaIslandCountdownType::SWITCH);
	EXPECT_EQ(aInputs[2].m_Id, 2);
	EXPECT_EQ(aInputs[3].m_Type, EHudMediaIslandCountdownType::SWITCH);
	EXPECT_EQ(aInputs[3].m_Id, 4);
	EXPECT_EQ(aInputs[4].m_Type, EHudMediaIslandCountdownType::SWITCH);
	EXPECT_EQ(aInputs[4].m_Id, 9);
	EXPECT_EQ(aInputs[5].m_Type, EHudMediaIslandCountdownType::MUTE);
}

TEST(QmHudMediaIslandSatellite, ProgressClampsAtLifecycleBounds)
{
	const SHudMediaIslandCountdownInput Input{EHudMediaIslandCountdownType::SWAP, 0, 100, 400, 300};

	EXPECT_FLOAT_EQ(QmHudMediaIslandCountdownProgress(Input, 50), 1.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandCountdownProgress(Input, 250), 0.5f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandCountdownProgress(Input, 450), 0.0f);
}

TEST(QmHudMediaIslandSatellite, MultipleItemsKeepThreePixelEdgeGap)
{
	EXPECT_FLOAT_EQ(QmHudMediaIslandSatelliteWidth(0, 16.0f, 3.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandSatelliteWidth(1, 16.0f, 3.0f), 16.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandSatelliteWidth(2, 16.0f, 3.0f), 35.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandSatelliteWidth(3, 16.0f, 3.0f), 54.0f);
}

TEST(QmHudMediaIslandLayout, ActiveLyricsKeepAFixedViewportWithAnExistingTopRow)
{
	EXPECT_FLOAT_EQ(QmHudMediaIslandDesiredBottomWidth(true, true, false, 0.0f, 72.0f, 300.0f, 10.0f), 92.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandDesiredBottomWidth(true, true, false, 0.0f, 500.0f, 300.0f, 10.0f), 300.0f);
}

TEST(QmHudMediaIslandLayout, LyricsOnlyUsesFixedTitleAreaWidth)
{
	EXPECT_FLOAT_EQ(QmHudMediaIslandDesiredBottomWidth(true, false, false, 0.0f, 72.0f, 300.0f, 10.0f), 92.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandDesiredBottomWidth(true, false, false, 0.0f, 500.0f, 100.0f, 10.0f), 100.0f);
}

TEST(QmHudMediaIslandLayout, UtilityBottomContentStillControlsRequestedWidth)
{
	EXPECT_FLOAT_EQ(QmHudMediaIslandDesiredBottomWidth(true, true, true, 45.0f, 72.0f, 300.0f, 10.0f), 92.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandDesiredBottomWidth(false, false, true, 45.0f, 72.0f, 300.0f, 10.0f), 65.0f);
}

TEST(QmHudMediaIslandLyrics, ActiveLyricsKeepTheIslandExpandedWithoutRepeatedMorphs)
{
	SHudMediaIslandExpansionState State;
	State = QmHudMediaIslandUpdateExpansion(State, true, true, false, 1000, 3000);
	EXPECT_TRUE(State.m_Expanded);
	EXPECT_TRUE(State.m_LyricsActive);
	EXPECT_EQ(State.m_ExpandUntilTick, 0);
	EXPECT_TRUE(State.m_StartCapsuleMorph);

	State.m_StartCapsuleMorph = false;
	State = QmHudMediaIslandUpdateExpansion(State, true, true, false, 100000, 3000);
	EXPECT_TRUE(State.m_Expanded);
	EXPECT_EQ(State.m_ExpandUntilTick, 0);
	EXPECT_FALSE(State.m_StartCapsuleMorph);
}

TEST(QmHudMediaIslandLyrics, TrackDetailsUseAnIndependentThreeSecondDeadline)
{
	EXPECT_TRUE(QmHudMediaIslandShouldShowTrackDetails(1000, 4000));
	EXPECT_FALSE(QmHudMediaIslandShouldShowTrackDetails(4000, 4000));
	EXPECT_FALSE(QmHudMediaIslandShouldShowTrackDetails(5000, 0));
}

TEST(QmHudMediaIslandLyrics, LosingLyricsRestoresTheNormalAutoCollapseDeadline)
{
	SHudMediaIslandExpansionState State;
	State.m_Expanded = true;
	State.m_LyricsActive = true;
	State = QmHudMediaIslandUpdateExpansion(State, true, false, false, 1000, 3000);
	EXPECT_TRUE(State.m_Expanded);
	EXPECT_EQ(State.m_ExpandUntilTick, 4000);
	EXPECT_FALSE(State.m_StartCapsuleMorph);

	State = QmHudMediaIslandUpdateExpansion(State, true, false, false, 4000, 3000);
	EXPECT_FALSE(State.m_Expanded);
	EXPECT_EQ(State.m_ExpandUntilTick, 0);
	EXPECT_TRUE(State.m_StartCapsuleMorph);
}

TEST(QmHudMediaIslandLyrics, ShortLyricsNeverScroll)
{
	EXPECT_FLOAT_EQ(QmHudMediaIslandMarqueeOffset(80.0f, 100.0f, 0.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandMarqueeOffset(100.0f, 100.0f, 500.0f), 0.0f);
}

TEST(QmHudMediaIslandLyrics, LongLyricsPauseTravelAndReturnWithinTheViewport)
{
	constexpr float TextWidth = 200.0f;
	constexpr float ViewportWidth = 100.0f;
	constexpr float Speed = 50.0f;
	EXPECT_FLOAT_EQ(QmHudMediaIslandMarqueeOffset(TextWidth, ViewportWidth, 0.0f, Speed), 0.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandMarqueeOffset(TextWidth, ViewportWidth, 1.2f, Speed), 0.0f);
	EXPECT_NEAR(QmHudMediaIslandMarqueeOffset(TextWidth, ViewportWidth, 2.2f, Speed), 50.0f, 0.001f);
	EXPECT_NEAR(QmHudMediaIslandMarqueeOffset(TextWidth, ViewportWidth, 3.2f, Speed), 100.0f, 0.001f);
	EXPECT_NEAR(QmHudMediaIslandMarqueeOffset(TextWidth, ViewportWidth, 4.2f, Speed), 100.0f, 0.001f);
	EXPECT_NEAR(QmHudMediaIslandMarqueeOffset(TextWidth, ViewportWidth, 5.2f, Speed), 50.0f, 0.001f);
	EXPECT_NEAR(QmHudMediaIslandMarqueeOffset(TextWidth, ViewportWidth, 6.2f, Speed), 0.0f, 0.001f);
}

TEST(QmHudMediaIslandLyrics, NewLineResetsTheMarqueeAtItsStartingPause)
{
	EXPECT_FALSE(QmHudMediaIslandShouldResetMarquee("same", "same"));
	EXPECT_TRUE(QmHudMediaIslandShouldResetMarquee("first", "second"));
	EXPECT_TRUE(QmHudMediaIslandShouldResetMarquee(nullptr, "line"));
	EXPECT_FLOAT_EQ(QmHudMediaIslandMarqueeOffset(200.0f, 100.0f, 0.0f), 0.0f);
}

TEST(QmHudMediaIslandLayout, FirstIncomingSwapReplacesCheckpointAndLyricsRemainLast)
{
	const SHudMediaIslandSwapRows Rows = QmHudMediaIslandSwapRows(3, true, true);
	EXPECT_EQ(Rows.m_InlineSwapCount, 1);
	EXPECT_EQ(Rows.m_BottomSwapCount, 2);
	EXPECT_EQ(Rows.m_BottomLineCount, 3);
	EXPECT_EQ(Rows.m_LyricsLineIndex, 2);
}

TEST(QmHudMediaIslandLayout, SwapsUseBottomRowsWhenRaceTimerIsUnavailable)
{
	const SHudMediaIslandSwapRows Rows = QmHudMediaIslandSwapRows(3, false, true);
	EXPECT_EQ(Rows.m_InlineSwapCount, 0);
	EXPECT_EQ(Rows.m_BottomSwapCount, 3);
	EXPECT_EQ(Rows.m_BottomLineCount, 4);
	EXPECT_EQ(Rows.m_LyricsLineIndex, 3);
}

TEST(QmHudMediaIslandSatellite, KeepsLatestVisibleSwitchesAndSeparatesTeamIdentity)
{
	EXPECT_EQ(QmHudMediaIslandVisibleSuffixStart(5, 3), 2);
	EXPECT_EQ(QmHudMediaIslandVisibleSuffixStart(3, 3), 0);
	EXPECT_EQ(QmHudMediaIslandVisibleSuffixStart(2, 0), 2);
	EXPECT_NE(QmHudMediaIslandSwitchInstanceId(1, 7), QmHudMediaIslandSwitchInstanceId(2, 7));
	EXPECT_EQ(QmHudMediaIslandSwitchInstanceId(2, 7) & 0xff, 7);
}

TEST(QmHudSwitchCountdown, SelectsTheLatestThreeActiveTriggersAndKeepsTheirOwners)
{
	const std::array<SHudSwitchCountdownEntry, 5> aEntries = {{
		{1, 4, 11, 0, 10, 90, 50},
		{1, 7, 12, 1, 40, 100, 55},
		{2, 3, 11, 0, 30, 110, 50},
		{2, 9, 12, 1, 60, 45, 50},
		{1, 8, 11, 0, 50, 120, 50},
	}};
	std::array<SHudSwitchCountdownEntry, 3> aSelected{};

	const int Count = QmHudSelectLatestSwitchCountdowns(aEntries.data(), aEntries.size(), aSelected.data(), aSelected.size());

	ASSERT_EQ(Count, 3);
	EXPECT_EQ(aSelected[0].m_Number, 8);
	EXPECT_EQ(aSelected[0].m_ClientId, 11);
	EXPECT_EQ(aSelected[1].m_Number, 7);
	EXPECT_EQ(aSelected[1].m_ClientId, 12);
	EXPECT_EQ(aSelected[2].m_Number, 3);
	EXPECT_EQ(aSelected[2].m_ClientId, 11);
}

TEST(QmHudSwitchCountdown, FollowTargetsDefaultLeftAndStayOppositeThePet)
{
	const vec2 TeePosition(100.0f, 200.0f);
	EXPECT_EQ(QmHudSwitchCountdownFollowSide(TeePosition.x, false, 0.0f), -1);
	EXPECT_EQ(QmHudSwitchCountdownFollowSide(TeePosition.x, true, 60.0f), 1);
	EXPECT_EQ(QmHudSwitchCountdownFollowSide(TeePosition.x, true, 140.0f), -1);

	const vec2 NearestLeft = QmHudSwitchCountdownFollowTarget(TeePosition, -1, 0, 0.0f);
	const vec2 OlderLeft = QmHudSwitchCountdownFollowTarget(TeePosition, -1, 1, 0.0f);
	const vec2 NearestRight = QmHudSwitchCountdownFollowTarget(TeePosition, 1, 0, 0.0f);
	const vec2 OlderRight = QmHudSwitchCountdownFollowTarget(TeePosition, 1, 1, 0.0f);
	EXPECT_GT(NearestLeft.x, OlderLeft.x);
	EXPECT_LT(NearestRight.x, OlderRight.x);
	EXPECT_LT(NearestLeft.y, TeePosition.y);
	EXPECT_FLOAT_EQ(NearestLeft.y, OlderLeft.y);
}

TEST(QmHudSwitchCountdown, LocationModeKeepsLegacyValuesAndAllowsBothSurfaces)
{
	const int FollowTee = static_cast<int>(EQmSwitchCountdownMode::FOLLOW_TEE);
	const int MediaIsland = static_cast<int>(EQmSwitchCountdownMode::MEDIA_ISLAND);
	const int Both = static_cast<int>(EQmSwitchCountdownMode::BOTH);

	EXPECT_TRUE(QmHudSwitchCountdownShowsFollowTee(FollowTee));
	EXPECT_FALSE(QmHudSwitchCountdownShowsMediaIsland(FollowTee));
	EXPECT_FALSE(QmHudSwitchCountdownShowsFollowTee(MediaIsland));
	EXPECT_TRUE(QmHudSwitchCountdownShowsMediaIsland(MediaIsland));
	EXPECT_TRUE(QmHudSwitchCountdownShowsFollowTee(Both));
	EXPECT_TRUE(QmHudSwitchCountdownShowsMediaIsland(Both));

	EXPECT_EQ(QmHudSwitchCountdownModeFromLocations(true, false, MediaIsland), FollowTee);
	EXPECT_EQ(QmHudSwitchCountdownModeFromLocations(false, true, FollowTee), MediaIsland);
	EXPECT_EQ(QmHudSwitchCountdownModeFromLocations(true, true, FollowTee), Both);
	EXPECT_EQ(QmHudSwitchCountdownModeFromLocations(false, false, FollowTee), FollowTee);
	EXPECT_EQ(QmHudSwitchCountdownModeFromLocations(false, false, MediaIsland), MediaIsland);
	EXPECT_EQ(QmHudSwitchCountdownModeFromLocations(false, false, Both), Both);
}

TEST(QmHudSwitchCountdownSource, FollowRingsReuseMediaIslandSatelliteStyleWithoutIconsOrText)
{
	const std::string HudSource = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string PetSource = ReadTestSourceFile("src/game/client/components/tclient/pet.cpp");
	const std::string FollowBody = FunctionBody(HudSource, "void CHud::RenderFollowSwitchCountdowns()");
	const std::string SatelliteBody = FunctionBody(HudSource, "void DrawMediaIslandCountdownSatellite(");

	EXPECT_NE(PetSource.find("QmTClientPetAdvanceSpring"), std::string::npos);
	EXPECT_NE(FollowBody.find("QmTClientPetAdvanceSpring"), std::string::npos);
	EXPECT_NE(FollowBody.find("GameClient()->m_Pet.IsVisibleForClient"), std::string::npos);
	EXPECT_NE(FollowBody.find("QmHudSwitchCountdownFollowSide"), std::string::npos);
	EXPECT_NE(FollowBody.find("DrawMediaIslandCountdownSatellite"), std::string::npos);
	EXPECT_NE(FollowBody.find("constexpr float SatelliteRadius = 9.0f + 2.5f * 0.5f"), std::string::npos);
	EXPECT_NE(FollowBody.find("RingRadius = SatelliteRadius * MEDIA_ISLAND_SATELLITE_RING_RADIUS_SCALE"), std::string::npos);
	EXPECT_NE(FollowBody.find("SatelliteRadius * MEDIA_ISLAND_SATELLITE_RING_THICKNESS_SCALE"), std::string::npos);
	EXPECT_NE(FollowBody.find("g_Config.m_QmHudIslandBgColor"), std::string::npos);
	EXPECT_NE(FollowBody.find("g_Config.m_QmHudIslandBgOpacity"), std::string::npos);
	EXPECT_EQ(FollowBody.find("DrawMediaIslandArcGeometry"), std::string::npos);
	EXPECT_EQ(FollowBody.find("MediaIslandCountdownIcon"), std::string::npos);
	EXPECT_EQ(FollowBody.find("TextRender()"), std::string::npos);

	EXPECT_NE(SatelliteBody.find("SHudMediaIslandSdfRenderState"), std::string::npos);
	EXPECT_NE(SatelliteBody.find("m_Radii = vec2()"), std::string::npos);
	EXPECT_NE(SatelliteBody.find("MEDIA_ISLAND_OUTER_SHADOW_PIXELS"), std::string::npos);
	EXPECT_NE(SatelliteBody.find("MEDIA_ISLAND_OUTER_SHADOW_OPACITY"), std::string::npos);
	EXPECT_NE(SatelliteBody.find("QmHudMediaIslandBuildGpuSdfParams"), std::string::npos);
	EXPECT_NE(SatelliteBody.find("RenderMediaIslandSdf"), std::string::npos);
	EXPECT_NE(SatelliteBody.find("DrawMediaIslandGeometryFallback"), std::string::npos);
	EXPECT_EQ(SatelliteBody.find("MediaIslandCountdownIcon"), std::string::npos);
	EXPECT_EQ(SatelliteBody.find("TextRender()"), std::string::npos);
}

TEST(QmHudSwitchCountdownSource, ModesShareTrackingAndCanFeedBothSurfaces)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string UpdateBody = FunctionBody(Source, "void CHud::UpdateSwitchCountdownTracker()");
	const std::string HasIslandBody = FunctionBody(Source, "bool CHud::HasActiveSwitchCountdown() const");
	const std::string FollowBody = FunctionBody(Source, "void CHud::RenderFollowSwitchCountdowns()");
	const std::string OnRenderBody = FunctionBody(Source, "void CHud::OnRender()");
	const size_t IslandBegin = Source.find("void CHud::RenderMediaIsland()");
	ASSERT_NE(IslandBegin, std::string::npos);
	const size_t IslandEnd = Source.find("void CHud::RenderPlayerState", IslandBegin);
	ASSERT_NE(IslandEnd, std::string::npos);
	const std::string IslandBody = Source.substr(IslandBegin, IslandEnd - IslandBegin);

	EXPECT_NE(UpdateBody.find("m_aaClientId[Team][SwitchNumber] = ClientId"), std::string::npos);
	EXPECT_NE(UpdateBody.find("m_aaConnection[Team][SwitchNumber] = Connection"), std::string::npos);
	EXPECT_NE(UpdateBody.find("QmHudSwitchCountdownShowsFollowTee"), std::string::npos);
	EXPECT_NE(HasIslandBody.find("QmHudSwitchCountdownShowsMediaIsland"), std::string::npos);
	EXPECT_NE(IslandBody.find("g_Config.m_QmSwitchCountdown"), std::string::npos);
	EXPECT_NE(IslandBody.find("QmHudSwitchCountdownShowsMediaIsland"), std::string::npos);
	EXPECT_NE(OnRenderBody.find("RenderFollowSwitchCountdowns();"), std::string::npos);
	EXPECT_NE(FollowBody.find("QmHudSwitchCountdownShowsFollowTee"), std::string::npos);
}

TEST(QmHudMediaIslandBlob, UnderdampedTravelOvershootsThenPullsBackToRest)
{
	// 关键回归：分离必须是"冲过头再被拉回"，而不是在终点急刹车。
	// 旧实现峰值只有 +1.20%（约 0.15px）且发生在 p≈0.90，肉眼不可见。
	SHudMediaIslandBlobSpring Spring;
	// 峰值与落定都需要覆盖完整窗口（停车判据在窗口走完后才生效）。
	const float SettleSeconds = QmHudMediaIslandBlobSpringWindowSeconds() * 2.5f;
	const float Peak = PeakBlobTravel(Spring, true, SettleSeconds);
	EXPECT_GT(Peak, 1.05f) << "过冲必须明显可见";
	EXPECT_LT(Peak, 1.15f) << "过冲仍需克制";
	// zeta=0.60 的理论过冲 +9.48%；数值积分实测 +9.28%。
	EXPECT_NEAR(Peak, 1.0928f, 0.005f);

	// 峰值之后要回落并稳定在 1.0（精确落位）。
	StepBlobSpring(Spring, true, SettleSeconds);
	const SHudMediaIslandBlobPose Settled = QmHudMediaIslandBlobPose(Spring);
	EXPECT_FLOAT_EQ(Settled.m_Travel, 1.0f);
	// 落定后形变必须回到正圆（速度项归零）。
	EXPECT_FLOAT_EQ(Settled.m_StretchX, 1.0f);
	EXPECT_FLOAT_EQ(Settled.m_StretchY, 1.0f);
	EXPECT_FLOAT_EQ(Settled.m_RadiusScale, 1.0f);
	EXPECT_FLOAT_EQ(Settled.m_ContentAlpha, 1.0f);
}

TEST(QmHudMediaIslandBlob, SettlesExactlyOnTargetOnceTheWindowIsComplete)
{
	// 减少动效（motion level 0）路径必须精确落到 1 / 0，不能留残差。
	SHudMediaIslandBlobSpring Spring;
	QmHudMediaIslandBlobSetBinary(Spring, true);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Spring).m_Travel, 1.0f);
	QmHudMediaIslandBlobSetBinary(Spring, false);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Spring).m_Travel, 0.0f);

	// 动画路径走完窗口后同样精确落在目标上（指数尾巴由停车判据收尾）。
	SHudMediaIslandBlobSpring Animated;
	StepBlobSpring(Animated, true, QmHudMediaIslandBlobSpringWindowSeconds() * 2.5f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Animated).m_Travel, 1.0f);
	EXPECT_FLOAT_EQ(Animated.m_Value, 1.0f);
	EXPECT_FLOAT_EQ(Animated.m_Velocity, 0.0f);
	StepBlobSpring(Animated, false, QmHudMediaIslandBlobSpringWindowSeconds() * 2.5f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Animated).m_Travel, 0.0f);
	EXPECT_FLOAT_EQ(Animated.m_Value, 0.0f);
	EXPECT_FLOAT_EQ(Animated.m_Velocity, 0.0f);
}

TEST(QmHudMediaIslandBlob, OvershootPeaksAfterTheRushAndNotDuringTheBridgePhase)
{
	// 峰值必须出现在行程后段：若峰值落在中段，液滴会在还连着主岛时来回抖。
	SHudMediaIslandBlobSpring Spring;
	constexpr float Frame = 1.0f / 240.0f;
	float Peak = 0.0f;
	float PeakAt = 0.0f;
	float AtQuarter = 0.0f;
	for(int i = 0; i < 320; ++i)
	{
		QmHudMediaIslandBlobSpringAdvance(Spring, Frame, QmHudMediaIslandBlobSpringWindowSeconds(), true);
		const float Travel = QmHudMediaIslandBlobPose(Spring).m_Travel;
		if(i + 1 == 60)
			AtQuarter = Travel;
		if(Travel > Peak)
		{
			Peak = Travel;
			PeakAt = (i + 1) * Frame;
		}
	}
	// 0.25s 时仍在冲向目标，还没有过冲（连接阶段保持稳定）。
	EXPECT_GT(AtQuarter, 0.85f);
	EXPECT_LT(AtQuarter, 1.0f);
	// 理论峰值时刻 π/wd = 0.436s（zeta=0.60, wn=9.0），落在原 0.44s 节奏之内。
	EXPECT_NEAR(PeakAt, 0.436f, 0.03f);
}

TEST(QmHudMediaIslandBlob, TravelIsFrameRateIndependent)
{
	// 解析式求值：串行小步与一次性大步必须给出相同结果。
	SHudMediaIslandBlobSpring Sixty;
	SHudMediaIslandBlobSpring TwoForty;
	float SixtyTravel = 0.0f;
	float TwoFortyTravel = 0.0f;
	for(int i = 0; i < 30; ++i)
		SixtyTravel = StepBlobSpring(Sixty, true, 1.0f / 60.0f, 1.0f / 60.0f);
	for(int i = 0; i < 120; ++i)
		TwoFortyTravel = StepBlobSpring(TwoForty, true, 1.0f / 240.0f, 1.0f / 240.0f);
	EXPECT_FLOAT_EQ(SixtyTravel, TwoFortyTravel);
}

TEST(QmHudMediaIslandBlob, ReverseKeepsVelocityContinuousAndPoseHasNoJump)
{
	SHudMediaIslandBlobSpring Spring;
	StepBlobSpring(Spring, true, 0.220f);
	const SHudMediaIslandBlobPose BeforeReverse = QmHudMediaIslandBlobPose(Spring);
	ASSERT_GT(BeforeReverse.m_Travel, 0.0f);

	// 目标是常数，切目标不换算任何状态，所以位移与速度都逐位保持
	// ——这正是旧实现（进度反向时速度突变）做不到的地方。
	const float ValueBefore = Spring.m_Value;
	const float VelocityBefore = Spring.m_Velocity;
	StepBlobSpring(Spring, false, 0.0f);
	EXPECT_FLOAT_EQ(Spring.m_Value, ValueBefore);
	EXPECT_FLOAT_EQ(Spring.m_Velocity, VelocityBefore);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Spring).m_Travel, BeforeReverse.m_Travel);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Spring).m_Velocity, BeforeReverse.m_Velocity);

	// 收回途中位姿连续回落。
	StepBlobSpring(Spring, false, 0.110f);
	const SHudMediaIslandBlobPose MidReverse = QmHudMediaIslandBlobPose(Spring);
	EXPECT_LT(MidReverse.m_Travel, BeforeReverse.m_Travel);

	// 反向途中再切回，位姿仍然连续（不允许折角/跳变）。
	StepBlobSpring(Spring, true, 0.0f);
	const SHudMediaIslandBlobPose AfterReverse = QmHudMediaIslandBlobPose(Spring);
	EXPECT_FLOAT_EQ(AfterReverse.m_Travel, MidReverse.m_Travel);
	EXPECT_FLOAT_EQ(AfterReverse.m_Velocity, MidReverse.m_Velocity);
	EXPECT_FLOAT_EQ(AfterReverse.m_RadiusScale, MidReverse.m_RadiusScale);
	EXPECT_FLOAT_EQ(AfterReverse.m_StretchX, MidReverse.m_StretchX);
	EXPECT_FLOAT_EQ(AfterReverse.m_StretchY, MidReverse.m_StretchY);
}

TEST(QmHudMediaIslandBlob, VelocityStretchIsSubtleAndReturnsToACircleAtRest)
{
	SHudMediaIslandBlobSpring Spring;
	StepBlobSpring(Spring, true, 0.120f);
	const SHudMediaIslandBlobPose Moving = QmHudMediaIslandBlobPose(Spring);
	EXPECT_GT(Moving.m_StretchX, 1.0f);
	EXPECT_LT(Moving.m_StretchY, 1.0f);
	EXPECT_GT(Moving.m_Velocity, 0.0f) << "拉伸必须由真实弹簧速度驱动";

	// 旧实现里速度通道是死代码（Pose 从未取用速度函数），这里确认它真的接上了。
	const SHudMediaIslandBlobPose PeakStretch = QmHudMediaIslandBlobPose(Spring);
	EXPECT_GE(PeakStretch.m_StretchX, Moving.m_StretchX);

	SHudMediaIslandBlobSpring Settled;
	StepBlobSpring(Settled, true, QmHudMediaIslandBlobSpringWindowSeconds() * 2.5f);
	const SHudMediaIslandBlobPose AtRest = QmHudMediaIslandBlobPose(Settled);
	EXPECT_FLOAT_EQ(AtRest.m_StretchX, 1.0f);
	EXPECT_FLOAT_EQ(AtRest.m_StretchY, 1.0f);
	EXPECT_FLOAT_EQ(AtRest.m_Velocity, 0.0f);
}

TEST(QmHudMediaIslandBlob, StretchStaysWithinTheHistoricBounds)
{
	SHudMediaIslandBlobSpring Spring;
	constexpr float Frame = 1.0f / 240.0f;
	float MaxStretchX = 1.0f;
	float MinStretchY = 1.0f;
	for(int i = 0; i < 320; ++i)
	{
		QmHudMediaIslandBlobSpringAdvance(Spring, Frame, QmHudMediaIslandBlobSpringWindowSeconds(), true);
		const SHudMediaIslandBlobPose Pose = QmHudMediaIslandBlobPose(Spring);
		MaxStretchX = std::max(MaxStretchX, Pose.m_StretchX);
		MinStretchY = std::min(MinStretchY, Pose.m_StretchY);
	}
	EXPECT_LE(MaxStretchX, 1.09f);
	EXPECT_GE(MinStretchY, 0.95f);
}

TEST(QmHudMediaIslandBlob, SmoothMergeDetachesAtRestAndRemainsDuringTravel)
{
	const float Blend = QmHudMediaIslandBlobBlend(8.0f, 1.0f);
	EXPECT_GT(Blend, 0.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobBlend(8.0f, 0.0f), 0.0f);

	SHudMediaIslandBlobSpring Moving;
	StepBlobSpring(Moving, true, 0.100f);
	SHudMediaIslandBlobSpring Settled;
	StepBlobSpring(Settled, true, QmHudMediaIslandBlobSpringWindowSeconds() * 2.5f);
	const float MovingConnection = QmHudMediaIslandBlobConnectionStrength(QmHudMediaIslandBlobPose(Moving).m_Travel);
	const float SettledConnection = QmHudMediaIslandBlobConnectionStrength(QmHudMediaIslandBlobPose(Settled).m_Travel);
	EXPECT_GT(MovingConnection, 0.0f);
	EXPECT_FLOAT_EQ(SettledConnection, 0.0f);
	const float NearBridge = QmHudMediaIslandSdfSmoothUnion(1.0f, 1.0f, Blend * MovingConnection);
	const float SettledGap = QmHudMediaIslandSdfSmoothUnion(1.5f, 1.5f, Blend * SettledConnection);
	EXPECT_LT(NearBridge, 0.0f);
	EXPECT_FLOAT_EQ(SettledGap, 1.5f);
}

TEST(QmHudMediaIslandSatellite, LiquidProgressSnapsUnderReducedMotionAndResolvesOnTarget)
{
	SHudMediaIslandBlobSpring Spring;
	// motion level 0：直接落到目标位姿。
	QmHudMediaIslandBlobSetBinary(Spring, true);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Spring).m_Travel, 1.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobProgress(Spring), 1.0f);
	QmHudMediaIslandBlobSetBinary(Spring, false);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Spring).m_Travel, 0.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobProgress(Spring), 0.0f);

	// 退出与进入共用同一窗口，只是方向相反，且同样精确收敛到 0。
	SHudMediaIslandBlobSpring Exiting;
	StepBlobSpring(Exiting, true, QmHudMediaIslandBlobSpringWindowSeconds() * 2.5f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Exiting).m_Travel, 1.0f);
	StepBlobSpring(Exiting, false, QmHudMediaIslandBlobSpringWindowSeconds() * 2.5f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Exiting).m_Travel, 0.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobProgress(Exiting), 0.0f);
}

TEST(QmHudMediaIslandSatellite, AdvanceIsIdempotentWithinTheSameTick)
{
	SHudMediaIslandBlobSpring Spring;
	int64_t LastTick = 0;
	QmHudAdvanceMediaIslandLiquidProgress(Spring, LastTick, 100, true, true);
	const SHudMediaIslandBlobPose First = QmHudMediaIslandBlobPose(Spring);
	// 同一 tick 内再次调用不得重复推进（卫星在同帧会被采样两次）。
	QmHudAdvanceMediaIslandLiquidProgress(Spring, LastTick, 100, true, true);
	const SHudMediaIslandBlobPose Second = QmHudMediaIslandBlobPose(Spring);
	EXPECT_FLOAT_EQ(First.m_Travel, Second.m_Travel);
	EXPECT_EQ(LastTick, 100);
}

TEST(QmHudMediaIslandSpectatorEye, OpeningTransitionHonorsMotionLevel)
{
	EXPECT_LT(QmHudAdvanceMediaIslandSpectatorIconProgress(0.0f, 0.179f, 2), 1.0f);
	EXPECT_FLOAT_EQ(QmHudAdvanceMediaIslandSpectatorIconProgress(0.0f, 0.180f, 2), 1.0f);
	EXPECT_LT(QmHudAdvanceMediaIslandSpectatorIconProgress(0.0f, 0.080f, 1), 1.0f);
	EXPECT_FLOAT_EQ(QmHudAdvanceMediaIslandSpectatorIconProgress(0.0f, 0.081f, 1), 1.0f);
	EXPECT_FLOAT_EQ(QmHudAdvanceMediaIslandSpectatorIconProgress(0.3f, 0.001f, 0), 1.0f);
	EXPECT_FLOAT_EQ(QmHudAdvanceMediaIslandSpectatorIconProgress(0.3f, -1.0f, 2), 0.3f);
}

TEST(QmHudMediaIslandSpectatorEye, ApprovedOpeningPoseCrossfadesAndOpensVertically)
{
	const SHudMediaIslandSpectatorIconPose Closed = QmHudMediaIslandSpectatorIconPose(0.0f);
	EXPECT_FLOAT_EQ(Closed.m_ClosedAlpha, 1.0f);
	EXPECT_FLOAT_EQ(Closed.m_OpenAlpha, 0.0f);
	EXPECT_FLOAT_EQ(Closed.m_OpenScaleX, 0.88f);
	EXPECT_FLOAT_EQ(Closed.m_OpenScaleY, 0.44f);
	EXPECT_FLOAT_EQ(Closed.m_CountAlpha, 0.0f);
	EXPECT_FLOAT_EQ(Closed.m_CountOffsetX, -2.4f);

	const SHudMediaIslandSpectatorIconPose Mid = QmHudMediaIslandSpectatorIconPose(0.5f);
	EXPECT_FLOAT_EQ(Mid.m_ClosedAlpha, 0.5f);
	EXPECT_FLOAT_EQ(Mid.m_OpenAlpha, 0.5f);

	const SHudMediaIslandSpectatorIconPose Open = QmHudMediaIslandSpectatorIconPose(1.0f);
	EXPECT_FLOAT_EQ(Open.m_ClosedAlpha, 0.0f);
	EXPECT_FLOAT_EQ(Open.m_OpenAlpha, 1.0f);
	EXPECT_FLOAT_EQ(Open.m_OpenScaleX, 1.0f);
	EXPECT_FLOAT_EQ(Open.m_OpenScaleY, 1.0f);
	EXPECT_FLOAT_EQ(Open.m_CountAlpha, 1.0f);
	EXPECT_FLOAT_EQ(Open.m_CountOffsetX, 0.0f);
}

TEST(QmHudMediaIslandSpectatorEye, ClosingProgressFollowsTheRightCapsuleRetraction)
{
	// 收回与分离共用同一对 (zeta, wn)。收回途中进度应落在 0..1 之间，
	// 图标按同一比例闭合。
	SHudMediaIslandBlobSpring Spring;
	StepBlobSpring(Spring, true, 0.20f);
	ASSERT_GT(QmHudMediaIslandBlobPose(Spring).m_Travel, 0.0f);
	const float LiquidProgress = StepBlobSpring(Spring, false, 0.06f);
	const float IconProgress = QmHudMediaIslandSpectatorIconProgressDuringExit(1.0f, 1.0f, LiquidProgress);
	EXPECT_GT(LiquidProgress, 0.0f);
	EXPECT_LT(LiquidProgress, 1.0f);
	EXPECT_NEAR(IconProgress, LiquidProgress, 0.0001f);

	// 收回走完必须精确归零（飞过生成点的那点冲量会被采样端夹住）。
	StepBlobSpring(Spring, false, QmHudMediaIslandBlobSpringWindowSeconds() * 3.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Spring).m_Travel, 0.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobProgress(Spring), 0.0f);

	EXPECT_FLOAT_EQ(QmHudMediaIslandSpectatorIconProgressDuringExit(1.0f, 0.25f, 0.25f), 1.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandSpectatorIconProgressDuringExit(1.0f, 0.25f, 0.125f), 0.5f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandSpectatorIconProgressDuringExit(1.0f, 0.25f, 0.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandSpectatorIconProgressDuringExit(1.0f, 0.0f, 0.0f), 0.0f);

	const SHudMediaIslandSpectatorIconPose HalfClosed = QmHudMediaIslandSpectatorIconPose(0.5f);
	EXPECT_FLOAT_EQ(HalfClosed.m_OpenAlpha, 0.5f);
	EXPECT_FLOAT_EQ(HalfClosed.m_ClosedAlpha, 0.5f);
}

TEST(QmHudMediaIslandSpectatorEye, ReopeningContinuesFromTheCurrentClosingPose)
{
	const float HalfClosed = QmHudMediaIslandSpectatorIconProgressDuringExit(1.0f, 1.0f, 0.5f);
	const float Reopened = QmHudAdvanceMediaIslandSpectatorIconProgress(HalfClosed, 0.045f, 2);
	EXPECT_NEAR(Reopened, 0.75f, 0.0001f);
}

TEST(QmHudMediaIslandSpectatorEye, ReopensOnlyWhileTheRightCapsuleIsBeingReclaimed)
{
	EXPECT_TRUE(QmHudMediaIslandShouldAnimateSpectatorEyeOpen(true, false, 0.4f));
	EXPECT_FALSE(QmHudMediaIslandShouldAnimateSpectatorEyeOpen(true, false, 0.0f));
	EXPECT_FALSE(QmHudMediaIslandShouldAnimateSpectatorEyeOpen(true, true, 0.4f));
	EXPECT_FALSE(QmHudMediaIslandShouldAnimateSpectatorEyeOpen(false, false, 0.4f));
	EXPECT_FLOAT_EQ(QmHudMediaIslandSpectatorCountAlpha(false, QmHudMediaIslandSpectatorIconPose(1.0f)), 0.0f);
}

TEST(QmHudMediaIslandSpectatorEye, OpenGlUsesTextIconWithoutChangingTheAtlasPath)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string RenderBody = FunctionBody(Source, "void CHud::RenderMediaIsland()");
	ASSERT_FALSE(RenderBody.empty());

	EXPECT_NE(Source.find("bool IsOpenGlBackend()"), std::string::npos);
	EXPECT_NE(Source.find("str_comp_nocase(g_Config.m_GfxBackend, \"OpenGL\") == 0"), std::string::npos);
	EXPECT_NE(RenderBody.find("if(IsOpenGlBackend())"), std::string::npos);
	EXPECT_NE(RenderBody.find("FontIcons::FONT_ICON_EYE_SLASH"), std::string::npos);
	EXPECT_NE(RenderBody.find("FontIcons::FONT_ICON_EYE"), std::string::npos);
	EXPECT_NE(RenderBody.find("else if(CQmIconManager *pIconManager = GameClient()->QmIconManager())"), std::string::npos);
}

TEST(QmHudMediaIslandBlob, RightCapsuleSettlesOutsideMainIsland)
{
	SHudMediaIslandBlobSpring SettledSpring;
	StepBlobSpring(SettledSpring, true, QmHudMediaIslandBlobSpringWindowSeconds() * 2.5f);
	const SHudMediaIslandLiquidCapsule Capsule = QmHudMediaIslandRightBlobCapsule(100.0f, 20.0f, 8.0f, 24.0f, 4.0f, QmHudMediaIslandBlobPose(SettledSpring));

	EXPECT_FLOAT_EQ(Capsule.m_Rect.x, 104.0f);
	EXPECT_FLOAT_EQ(Capsule.m_Rect.y, 12.0f);
	EXPECT_FLOAT_EQ(Capsule.m_Rect.w, 24.0f);
	EXPECT_FLOAT_EQ(Capsule.m_Rect.h, 16.0f);
	EXPECT_FLOAT_EQ(Capsule.m_Radius, 8.0f);
	EXPECT_FLOAT_EQ(Capsule.m_SmoothUnion, 0.0f);
	EXPECT_FLOAT_EQ(Capsule.m_ContentAlpha, 1.0f);
}

TEST(QmHudMediaIslandBlob, RightCapsuleUsesTheSameBoundedVelocityStretch)
{
	SHudMediaIslandBlobSpring MovingSpring;
	StepBlobSpring(MovingSpring, true, 0.120f);
	const SHudMediaIslandBlobPose MovingPose = QmHudMediaIslandBlobPose(MovingSpring);
	const SHudMediaIslandLiquidCapsule Capsule = QmHudMediaIslandRightBlobCapsule(100.0f, 20.0f, 8.0f, 24.0f, 4.0f, MovingPose);
	EXPECT_GT(Capsule.m_Rect.w, Capsule.m_Rect.h);
	EXPECT_GT(Capsule.m_SmoothUnion, 0.0f);
	EXPECT_GT(Capsule.m_ContentAlpha, 0.0f);
}

TEST(QmHudMediaIslandSatellite, SwapCompletionKeepsIdentityAndDoesNotRestartProgressRing)
{
	constexpr int64_t StartTick = 100;
	constexpr int TickSpeed = 50;
	const SHudMediaIslandSwapLifecycle Countdown = QmHudMediaIslandSwapLifecycle(StartTick, StartTick + 30 * TickSpeed - 1, TickSpeed);
	const SHudMediaIslandSwapLifecycle Ready = QmHudMediaIslandSwapLifecycle(StartTick, StartTick + 30 * TickSpeed, TickSpeed);
	const SHudMediaIslandCountdownInput CountdownSwap = QmHudMediaIslandSwapCountdownInput(1, StartTick, Countdown, false);
	const SHudMediaIslandCountdownInput ReadySwap = QmHudMediaIslandSwapCountdownInput(1, StartTick, Ready, false);

	EXPECT_TRUE(Countdown.m_Visible);
	EXPECT_FALSE(Countdown.m_Completed);
	EXPECT_EQ(Countdown.m_SecondsLeft, 1);
	EXPECT_GT(CountdownSwap.m_Progress, 0.0f);
	EXPECT_TRUE(Ready.m_Visible);
	EXPECT_TRUE(Ready.m_Completed);
	EXPECT_FLOAT_EQ(ReadySwap.m_Progress, 0.0f);
	EXPECT_EQ(ReadySwap.m_Type, CountdownSwap.m_Type);
	EXPECT_EQ(ReadySwap.m_Id, CountdownSwap.m_Id);
	EXPECT_TRUE(ReadySwap.m_Completed);
}

TEST(QmHudMediaIslandSatellite, SwapReadyStateExpiresAtSixtySeconds)
{
	constexpr int64_t StartTick = 100;
	constexpr int TickSpeed = 50;
	const SHudMediaIslandSwapLifecycle LastReadyTick = QmHudMediaIslandSwapLifecycle(StartTick, StartTick + 60 * TickSpeed - 1, TickSpeed);
	const SHudMediaIslandSwapLifecycle Expired = QmHudMediaIslandSwapLifecycle(StartTick, StartTick + 60 * TickSpeed, TickSpeed);

	EXPECT_TRUE(LastReadyTick.m_Visible);
	EXPECT_TRUE(LastReadyTick.m_Completed);
	EXPECT_FALSE(Expired.m_Visible);
}

TEST(QmHudMediaIslandSatellite, SwapDirectionAndConnectionStayBoundToTheirInstance)
{
	const SHudMediaIslandSwapLifecycle Lifecycle = QmHudMediaIslandSwapLifecycle(100, 200, 50);
	const SHudMediaIslandCountdownInput Incoming = QmHudMediaIslandSwapCountdownInput(0, 100, Lifecycle, false);
	const SHudMediaIslandCountdownInput Outgoing = QmHudMediaIslandSwapCountdownInput(1, 100, Lifecycle, true);

	EXPECT_FALSE(Incoming.m_SwapOutgoing);
	EXPECT_TRUE(Outgoing.m_SwapOutgoing);
	EXPECT_TRUE(QmHudMediaIslandSwapVisibleForConnection(Incoming.m_Id, 0));
	EXPECT_FALSE(QmHudMediaIslandSwapVisibleForConnection(Incoming.m_Id, 1));
	EXPECT_TRUE(QmHudMediaIslandSwapVisibleForConnection(Outgoing.m_Id, 1));
	EXPECT_FALSE(QmHudMediaIslandSwapVisibleForConnection(Outgoing.m_Id, 0));
}

TEST(QmHudMediaIslandSatellite, SdfCircleUsesNegativeInsideAndPositiveOutside)
{
	EXPECT_LT(QmHudMediaIslandSdfCircle(vec2(0.0f, 0.0f), vec2(0.0f, 0.0f), 2.0f), 0.0f);
	EXPECT_NEAR(QmHudMediaIslandSdfCircle(vec2(2.0f, 0.0f), vec2(0.0f, 0.0f), 2.0f), 0.0f, 0.0001f);
	EXPECT_GT(QmHudMediaIslandSdfCircle(vec2(3.0f, 0.0f), vec2(0.0f, 0.0f), 2.0f), 0.0f);
}

TEST(QmHudMediaIslandSatellite, SdfSmoothUnionFallsBackToMinimumWhenBlendIsDisabled)
{
	EXPECT_FLOAT_EQ(QmHudMediaIslandSdfSmoothUnion(0.4f, -0.2f, 0.0f), -0.2f);
	EXPECT_LT(QmHudMediaIslandSdfSmoothUnion(0.4f, 0.4f, 1.0f), 0.4f);
}

TEST(QmHudMediaIslandSatellite, SdfRoundedRectKeepsMainIslandCornersRounded)
{
	const CUIRect MainIsland = {0.0f, 0.0f, 20.0f, 16.0f};

	EXPECT_LT(QmHudMediaIslandSdfRoundedRect(vec2(10.0f, 8.0f), MainIsland, 8.0f, IGraphics::CORNER_ALL), 0.0f);
	EXPECT_GT(QmHudMediaIslandSdfRoundedRect(vec2(0.0f, 0.0f), MainIsland, 8.0f, IGraphics::CORNER_ALL), 0.0f);
	EXPECT_NEAR(QmHudMediaIslandSdfRoundedRect(vec2(0.0f, 8.0f), MainIsland, 8.0f, IGraphics::CORNER_ALL), 0.0f, 0.0001f);
	EXPECT_NEAR(QmHudMediaIslandSdfRoundedRect(vec2(0.0f, 0.0f), MainIsland, 8.0f, IGraphics::CORNER_R), 0.0f, 0.0001f);
	EXPECT_GT(QmHudMediaIslandSdfRoundedRect(vec2(0.0f, 0.0f), MainIsland, 8.0f, IGraphics::CORNER_NONE, 8.0f), 0.0f);
}

TEST(QmHudMediaIslandSdfBounds, PaddingContainsSmoothUnionAndFeatherOverflow)
{
	constexpr float ScreenPixelSize = 0.5f;
	const float StrongSmoothUnion = QmHudMediaIslandBlobBlend(8.0f, 1.0f);
	const float RequiredOverflow = StrongSmoothUnion * 0.25f + ScreenPixelSize * 0.9f;

	SHudMediaIslandSdfRenderState LeftSatelliteState;
	LeftSatelliteState.m_ItemCount = 1;
	LeftSatelliteState.m_Items[0].m_SmoothUnion = StrongSmoothUnion;
	LeftSatelliteState.m_ScreenPixelSize = ScreenPixelSize;
	EXPECT_GE(QmHudMediaIslandSdfPadding(LeftSatelliteState), RequiredOverflow);

	SHudMediaIslandSdfRenderState RightSatelliteState;
	RightSatelliteState.m_HasRightCapsule = true;
	RightSatelliteState.m_RightCapsule.m_SmoothUnion = StrongSmoothUnion;
	RightSatelliteState.m_ScreenPixelSize = ScreenPixelSize;
	EXPECT_GE(QmHudMediaIslandSdfPadding(RightSatelliteState), RequiredOverflow);

	SHudMediaIslandSdfRenderState RestingState;
	RestingState.m_ScreenPixelSize = ScreenPixelSize;
	EXPECT_FLOAT_EQ(QmHudMediaIslandSdfPadding(RestingState), 1.5f);

	SHudMediaIslandSdfRenderState ShadowState;
	ShadowState.m_ScreenPixelSize = ScreenPixelSize;
	ShadowState.m_OuterShadowSize = 3.0f;
	EXPECT_GE(QmHudMediaIslandSdfPadding(ShadowState), 3.0f + ScreenPixelSize * 0.9f);
}

TEST(QmHudMediaIslandSdfBounds, OuterRectKeepsEveryLiquidEdgeInsideTheQuad)
{
	SHudMediaIslandSdfRenderState State;
	State.m_MainRect = {10.0f, 10.0f, 20.0f, 10.0f};
	State.m_ItemCount = 1;
	State.m_Items[0].m_Center = vec2(4.0f, 15.0f);
	State.m_Items[0].m_Radii = vec2(4.0f, 5.0f);
	State.m_Items[0].m_SmoothUnion = 8.0f;
	State.m_HasRightCapsule = true;
	State.m_RightCapsule.m_Rect = {32.0f, 10.0f, 8.0f, 10.0f};
	State.m_RightCapsule.m_SmoothUnion = 8.0f;
	State.m_ScreenPixelSize = 0.5f;

	const float Padding = QmHudMediaIslandSdfPadding(State);
	const CUIRect OuterRect = QmHudMediaIslandSdfOuterRect(State);
	EXPECT_FLOAT_EQ(OuterRect.x, -Padding);
	EXPECT_FLOAT_EQ(OuterRect.y, 10.0f - Padding);
	EXPECT_FLOAT_EQ(OuterRect.x + OuterRect.w, 40.0f + Padding);
	EXPECT_FLOAT_EQ(OuterRect.y + OuterRect.h, 20.0f + Padding);
}

TEST(QmHudMediaIslandBackdrop, TransparentOpacityIncludesPureBlurAndSkipsOpaqueBackground)
{
	EXPECT_TRUE(QmHudMediaIslandShouldPrepareBackdropBlur(0, true));
	EXPECT_TRUE(QmHudMediaIslandShouldPrepareBackdropBlur(1, true));
	EXPECT_TRUE(QmHudMediaIslandShouldPrepareBackdropBlur(99, true));
	EXPECT_FALSE(QmHudMediaIslandShouldPrepareBackdropBlur(100, true));
	EXPECT_FALSE(QmHudMediaIslandShouldPrepareBackdropBlur(0, false));
	EXPECT_FALSE(QmHudMediaIslandShouldPrepareBackdropBlur(99, false));
}

TEST(QmHudMediaIslandBackdrop, RefreshesBlurOnlyAfterTheShortFrameAttemptInterval)
{
	EXPECT_TRUE(QmHudMediaIslandShouldRefreshBackdropBlur(10, 0, false));
	// 失败尝试也要进入短暂冷却，避免后端持续失败时每帧重试。
	EXPECT_FALSE(QmHudMediaIslandShouldRefreshBackdropBlur(11, 10, true));
	EXPECT_FALSE(QmHudMediaIslandShouldRefreshBackdropBlur(10, 10, true));
	EXPECT_FALSE(QmHudMediaIslandShouldRefreshBackdropBlur(12, 10, true));
	EXPECT_TRUE(QmHudMediaIslandShouldRefreshBackdropBlur(13, 10, true));
	EXPECT_TRUE(QmHudMediaIslandShouldRefreshBackdropBlur(9, 10, true));
}

TEST(QmHudMediaIslandBackdrop, MapsTheAnimatedOuterRectToTheCapturedScreenTexture)
{
	const CUIRect OuterRect = {120.0f, 30.0f, 80.0f, 40.0f};
	const CUIRect ScreenRect = {0.0f, 0.0f, 400.0f, 200.0f};
	const vec4 BackdropUv = QmHudMediaIslandBackdropUv(OuterRect, ScreenRect);

	EXPECT_FLOAT_EQ(BackdropUv.x, 0.3f);
	EXPECT_FLOAT_EQ(BackdropUv.y, 0.85f);
	EXPECT_FLOAT_EQ(BackdropUv.z, 0.2f);
	EXPECT_FLOAT_EQ(BackdropUv.w, -0.2f);
	const vec4 InvalidBackdropUv = QmHudMediaIslandBackdropUv(OuterRect, CUIRect());
	EXPECT_FLOAT_EQ(InvalidBackdropUv.x, 0.0f);
	EXPECT_FLOAT_EQ(InvalidBackdropUv.y, 0.0f);
	EXPECT_FLOAT_EQ(InvalidBackdropUv.z, 0.0f);
	EXPECT_FLOAT_EQ(InvalidBackdropUv.w, 0.0f);
}

TEST(QmHudMediaIslandSdfGpuPacking, CopiesAllShapeAndAnimationInputs)
{
	SHudMediaIslandSdfRenderState State;
	State.m_Rect = {1.0f, 2.0f, 80.0f, 24.0f};
	State.m_MainRect = {10.0f, 2.0f, 50.0f, 20.0f};
	State.m_MainRadius = 10.0f;
	State.m_MainCorners = IGraphics::CORNER_T | IGraphics::CORNER_BR;
	State.m_MainDisabledCornerRadius = 2.0f;
	State.m_ItemCount = 1;
	State.m_Items[0].m_Center = vec2(5.0f, 12.0f);
	State.m_Items[0].m_Radii = vec2(8.0f, 9.0f);
	State.m_Items[0].m_SmoothUnion = 3.0f;
	State.m_Items[0].m_ContentAlpha = 0.8f;
	State.m_Items[0].m_ContentScale = 0.7f;
	State.m_Items[0].m_CountdownProgress = 0.6f;
	State.m_Items[0].m_RingColor = ColorRGBA(0.1f, 0.9f, 1.0f, 0.7f);
	State.m_HasRightCapsule = true;
	State.m_RightCapsule.m_Rect = {62.0f, 2.0f, 20.0f, 20.0f};
	State.m_RightCapsule.m_Radius = 10.0f;
	State.m_RightCapsule.m_SmoothUnion = 4.0f;
	State.m_RingRadius = 6.0f;
	State.m_RingThickness = 1.5f;
	State.m_BackgroundColor = ColorRGBA(0.02f, 0.03f, 0.05f, 0.9f);
	State.m_ScreenPixelSize = 0.5f;
	State.m_OuterShadowSize = 1.0f;
	State.m_OuterShadowOpacity = 0.14f;
	State.m_BackdropUv = vec4(0.1f, 0.9f, 0.2f, -0.3f);

	IGraphics::SMediaIslandSdfParams Params;
	ASSERT_TRUE(QmHudMediaIslandBuildGpuSdfParams(State, Params));
	EXPECT_EQ(Params.ItemCount(), 1);
	EXPECT_TRUE(Params.HasRightCapsule());
	EXPECT_EQ(Params.MainCorners(), State.m_MainCorners);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_RECT].z, 80.0f);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_RESERVED].x, 1.0f);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_RESERVED].y, 0.14f);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_RESERVED].z, 0.0f);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_RESERVED].w, 0.0f);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_BACKDROP_UV].x, State.m_BackdropUv.x);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_BACKDROP_UV].y, State.m_BackdropUv.y);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_BACKDROP_UV].z, State.m_BackdropUv.z);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_BACKDROP_UV].w, State.m_BackdropUv.w);
	EXPECT_FLOAT_EQ(Params.Item(0, 0).z, 8.0f);
	EXPECT_FLOAT_EQ(Params.Item(0, 1).w, 0.6f);
	EXPECT_FLOAT_EQ(Params.Item(0, 2).g, 0.9f);
	EXPECT_FLOAT_EQ(Params.Item(0, 2).a, 0.7f);
}

TEST(QmHudMediaIslandSatellite, ParsesOwnSpamProtectionMuteOnly)
{
	int Seconds = 0;
	EXPECT_EQ(QmHudParseSpamProtectionMute("'Main' has been muted for 60 seconds (Spam protection)", "Main", "Dummy", Seconds), EHudMediaIslandMuteMessage::SPAM_BROADCAST);
	EXPECT_EQ(Seconds, 60);
	EXPECT_EQ(QmHudParseSpamProtectionMute("'Other' has been muted for 60 seconds (Spam protection)", "Main", "Dummy", Seconds), EHudMediaIslandMuteMessage::NONE);
	EXPECT_EQ(QmHudParseSpamProtectionMute("'Main' has been muted for 60 seconds (manual)", "Main", "Dummy", Seconds), EHudMediaIslandMuteMessage::NONE);
	EXPECT_EQ(QmHudParseSpamProtectionMute("'O'Brien' has been muted for 45 seconds (Spam protection)", "O'Brien", "Dummy", Seconds), EHudMediaIslandMuteMessage::SPAM_BROADCAST);
	EXPECT_EQ(Seconds, 45);
}

TEST(QmHudMediaIslandSatellite, ParsesActiveMuteRemainingMessageSeparately)
{
	int Seconds = 0;
	EXPECT_EQ(QmHudParseSpamProtectionMute("You are not permitted to talk for the next 17 seconds.", "Main", "Dummy", Seconds), EHudMediaIslandMuteMessage::REMAINING);
	EXPECT_EQ(Seconds, 17);
	EXPECT_EQ(QmHudParseSpamProtectionMute("This server has an initial chat delay, you will be able to talk in 17 seconds.", "Main", "Dummy", Seconds), EHudMediaIslandMuteMessage::NONE);
}

TEST(QmHudMediaIslandSatellite, RenderPathUsesBlobSatellitesInsteadOfCountdownText)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string GameClientSource = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const std::string TClientSource = ReadTestSourceFile("src/game/client/components/tclient/tclient.cpp");
	const size_t RenderBegin = Source.find("void CHud::RenderMediaIsland()");
	ASSERT_NE(RenderBegin, std::string::npos);
	const size_t RenderEnd = Source.find("void CHud::RenderPlayerState", RenderBegin);
	ASSERT_NE(RenderEnd, std::string::npos);
	const std::string RenderBody = Source.substr(RenderBegin, RenderEnd - RenderBegin);

	EXPECT_NE(RenderBody.find("RenderMediaIslandSdf"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudAdvanceMediaIslandLiquidProgress"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandBlobPose"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandBlobBlend"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandBlobConnectionStrength(BlobPose.m_Travel)"), std::string::npos);
	EXPECT_NE(RenderBody.find("mix(SpawnCenterX, FinalCenterX, BlobPose.m_Travel)"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandSdfOuterRect(CurrentSdfState)"), std::string::npos);
	EXPECT_NE(RenderBody.find("TransformedScreenX1 - TransformedScreenX0"), std::string::npos);
	EXPECT_NE(RenderBody.find("TransformedScreenY1 - TransformedScreenY0"), std::string::npos);
	EXPECT_EQ(RenderBody.find("DrawMediaIslandRipple"), std::string::npos);
	EXPECT_EQ(Source.find("m_MediaIslandRippleTexture"), std::string::npos);
	EXPECT_NE(RenderBody.find("const float SatelliteRadius = Radius;"), std::string::npos);
	EXPECT_NE(RenderBody.find("constexpr float SatelliteItemGap = QmHudMediaIslandScaled(2.0f);"), std::string::npos);
	EXPECT_NE(RenderBody.find("aActiveSatelliteTargetCenters[i] = SatelliteCursorX + aActiveSatelliteTargetWidths[i] * 0.5f"), std::string::npos);
	EXPECT_NE(RenderBody.find("const CUIRect MainIslandSdfRect = EntrancePose.m_Rect;"), std::string::npos);
	EXPECT_EQ(RenderBody.find("QmHudMediaIslandMainCapRect"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandRightBlobCapsule"), std::string::npos);
	EXPECT_NE(RenderBody.find("EQmIcon::SATELLITE_SPECTATOR_EYE"), std::string::npos);
	EXPECT_NE(RenderBody.find("EQmIcon::SATELLITE_SPECTATOR_EYE_CLOSED"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandSpectatorCountAlpha(ShowSpectator, SpectatorIconPose)"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandSpectatorIconProgressDuringExit(AnimState.m_SpectatorExitIconStart, AnimState.m_SpectatorExitLiquidStart, QmHudMediaIslandBlobProgress(AnimState.m_SpectatorLiquidSpring))"), std::string::npos);
	EXPECT_NE(Source.find("EQmIcon::SATELLITE_CHECK"), std::string::npos);
	EXPECT_NE(RenderBody.find("if(IsOpenGlBackend())"), std::string::npos);
	EXPECT_NE(RenderBody.find("FontIcons::FONT_ICON_EYE"), std::string::npos);
	EXPECT_EQ(RenderBody.find("if(SatelliteRenderItemCount > 0)"), std::string::npos);
	EXPECT_EQ(RenderBody.find("DrawMediaIslandLiquidBridge"), std::string::npos);
	EXPECT_EQ(RenderBody.find("DrawMediaIslandProgressRing"), std::string::npos);
	EXPECT_EQ(RenderBody.find("Graphics()->DrawRect(IslandX, IslandY"), std::string::npos);
	EXPECT_NE(RenderBody.find("MediaIslandCountdownIcon"), std::string::npos);
	EXPECT_NE(RenderBody.find("const ColorRGBA IconColor = Item.m_Completed ? ColorRGBA(0.20f, 1.0f, 0.42f"), std::string::npos);
	EXPECT_NE(RenderBody.find("MediaIslandCountdownIcon(Item.m_Type, Item.m_Completed, Item.m_SwapOutgoing)"), std::string::npos);
	EXPECT_NE(Source.find("QmHudMediaIslandSwapVisibleForConnection(Dummy, g_Config.m_ClDummy)"), std::string::npos);
	EXPECT_NE(Source.find("Out.m_Outgoing = State.m_Outgoing"), std::string::npos);
	EXPECT_NE(Source.find("SwapOutgoing ? EQmIcon::SATELLITE_SWAP_OUTGOING : EQmIcon::SATELLITE_SWAP_INCOMING"), std::string::npos);
	EXPECT_NE(RenderBody.find("SdfItem.m_RingColor = MediaIslandCountdownColor(Item.m_Type);"), std::string::npos);
	EXPECT_EQ(RenderBody.find("MediaIslandCountdownColor(Item.m_Type, Item.m_Completed)"), std::string::npos);
	EXPECT_NE(RenderBody.find("SatelliteVisibleLeft"), std::string::npos);
	EXPECT_EQ(RenderBody.find("BuildSwitchCountdownSummary"), std::string::npos);
	EXPECT_NE(GameClientSource.find("m_Hud.HandleSpamProtectionMessage(pMsg->m_pMessage);"), std::string::npos);
	EXPECT_NE(GameClientSource.find("m_TClient.HandleSwapCountdownMessage(pMsg->m_pMessage, Conn);"), std::string::npos);
	EXPECT_NE(TClientSource.find("m_aSwapCountdownTrackers[Dummy].Cancel"), std::string::npos);
}

TEST(QmHudMediaIslandSwapText, OnlyIncomingRequestsReplaceCheckpointAndExpandBeforeLyrics)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string RenderBody = FunctionBody(Source, "void CHud::RenderMediaIsland()");
	const std::string BuildInfoBody = FunctionBody(Source, "bool BuildSwapCountdownInfo(");

	EXPECT_NE(BuildInfoBody.find("if(!Out.m_Outgoing)"), std::string::npos);
	EXPECT_NE(BuildInfoBody.find("Localize(\"%s has requested to swap with %s\")"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandSwapRows(IncomingSwapCount, TimerCapsule.m_Visible, ShowLyricsIslandLine)"), std::string::npos);
	EXPECT_NE(RenderBody.find("TimerCapsule.m_BoxW = std::min(MaxTimerWidth"), std::string::npos);
	EXPECT_NE(RenderBody.find("if(SwapRows.m_InlineSwapCount > 0)"), std::string::npos);
	EXPECT_NE(RenderBody.find("else if(Checkpoint > 0)"), std::string::npos);
	EXPECT_NE(RenderBody.find("LyricsTextY = BottomTextY + BottomRowLineHeight * SwapRows.m_LyricsLineIndex"), std::string::npos);
}

TEST(QmHudMediaIslandSatellite, CompletedSwapUsesCheckIconWithoutBottomReadyText)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const size_t RenderBegin = Source.find("void CHud::RenderMediaIsland()");
	ASSERT_NE(RenderBegin, std::string::npos);
	const size_t RenderEnd = Source.find("void CHud::RenderPlayerState", RenderBegin);
	ASSERT_NE(RenderEnd, std::string::npos);
	const std::string RenderBody = Source.substr(RenderBegin, RenderEnd - RenderBegin);

	EXPECT_NE(RenderBody.find("MediaIslandCountdownIcon(Item.m_Type, Item.m_Completed, Item.m_SwapOutgoing)"), std::string::npos);
	EXPECT_EQ(RenderBody.find("ShowSwapReady"), std::string::npos);
	EXPECT_EQ(RenderBody.find("SwapReadyCount"), std::string::npos);
	EXPECT_EQ(RenderBody.find("SwapBottomContentWidth"), std::string::npos);
	EXPECT_EQ(RenderBody.find("RenderBottomTextCentered(BottomTextY, SwapList"), std::string::npos);
}

TEST(QmHudMediaIslandTimerLayout, SecondaryLinePreservesTenPercentTopMargin)
{
	const SHudMediaIslandTimerRowLayout Layout = QmHudMediaIslandTimerRows(1.0f, 16.0f, true);

	EXPECT_FLOAT_EQ(Layout.m_RaceY, 2.6f);
	EXPECT_FLOAT_EQ(Layout.m_RaceH, 9.6f);
	EXPECT_FLOAT_EQ(Layout.m_CheckpointY, 12.2f);
	EXPECT_FLOAT_EQ(Layout.m_CheckpointH, 4.8f);
}

TEST(QmHudMediaIslandTimerLayout, RaceUsesTheWholeSlotWithoutSecondaryLine)
{
	const SHudMediaIslandTimerRowLayout Layout = QmHudMediaIslandTimerRows(1.0f, 16.0f, false);

	EXPECT_FLOAT_EQ(Layout.m_RaceY, 1.0f);
	EXPECT_FLOAT_EQ(Layout.m_RaceH, 16.0f);
	EXPECT_FLOAT_EQ(Layout.m_CheckpointH, 0.0f);
}

TEST(QmHudMediaIslandTimerLayout, CheckpointOrSwapRaceTextDoesNotIntrudeIntoTopMargin)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string RenderBody = FunctionBody(Source, "void CHud::RenderMediaIsland()");

	EXPECT_NE(RenderBody.find("const bool ShowTimerSecondaryLine = SwapRows.m_InlineSwapCount > 0 || Checkpoint > 0;"), std::string::npos);
	EXPECT_NE(RenderBody.find("ShowTimerSecondaryLine ? TimerRows.m_RaceY + (TimerRows.m_RaceH - TimerRaceFontSize) * 0.5f : TimerCapsule.m_TextY"), std::string::npos);
}

TEST(QmHudMediaIslandWaveform, PlayingBarsVaryIndependentlyAndPausedBarsSettle)
{
	bool AnyChanged = false;
	for(int Bar = 0; Bar < 7; ++Bar)
	{
		const float First = QmHudMediaIslandWaveBarHeight(Bar, 1.0f, 1.0f);
		const float Next = QmHudMediaIslandWaveBarHeight(Bar, 1.1f, 1.0f);
		EXPECT_GE(First, 0.20f);
		EXPECT_LE(First, 1.0f);
		EXPECT_FLOAT_EQ(QmHudMediaIslandWaveBarHeight(Bar, 1.0f, 0.0f), 0.20f);
		EXPECT_FLOAT_EQ(QmHudMediaIslandWaveBarHeight(Bar, 1.0f, 0.5f), 0.20f + (First - 0.20f) * 0.5f);
		AnyChanged |= std::abs(First - Next) > 0.001f;
	}
	EXPECT_TRUE(AnyChanged);

	constexpr int SampleCount = 80;
	constexpr float SampleStep = 0.125f;
	for(int FirstBar = 0; FirstBar < 7; ++FirstBar)
	{
		for(int SecondBar = FirstBar + 1; SecondBar < 7; ++SecondBar)
		{
			double FirstSum = 0.0;
			double SecondSum = 0.0;
			double FirstSquaredSum = 0.0;
			double SecondSquaredSum = 0.0;
			double ProductSum = 0.0;
			for(int Sample = 0; Sample < SampleCount; ++Sample)
			{
				const float Time = Sample * SampleStep;
				const double First = QmHudMediaIslandWaveBarHeight(FirstBar, Time, 1.0f);
				const double Second = QmHudMediaIslandWaveBarHeight(SecondBar, Time, 1.0f);
				FirstSum += First;
				SecondSum += Second;
				FirstSquaredSum += First * First;
				SecondSquaredSum += Second * Second;
				ProductSum += First * Second;
			}

			const double Numerator = SampleCount * ProductSum - FirstSum * SecondSum;
			const double Denominator = std::sqrt(
				(SampleCount * FirstSquaredSum - FirstSum * FirstSum) *
				(SampleCount * SecondSquaredSum - SecondSum * SecondSum));
			ASSERT_GT(Denominator, 0.0);
			EXPECT_LT(std::abs(Numerator / Denominator), 0.20) << "bars " << FirstBar << " and " << SecondBar;
		}
	}
}

TEST(QmHudMediaIslandWaveform, StoppedBarsSettleFromOutsideIn)
{
	constexpr int BarCount = 7;
	constexpr float MidSettleTime = 0.40f;
	const float Outer = QmHudMediaIslandWaveBarSettleProgress(0, BarCount, MidSettleTime);
	const float NextOuter = QmHudMediaIslandWaveBarSettleProgress(1, BarCount, MidSettleTime);
	const float NextInner = QmHudMediaIslandWaveBarSettleProgress(2, BarCount, MidSettleTime);
	const float Center = QmHudMediaIslandWaveBarSettleProgress(3, BarCount, MidSettleTime);

	EXPECT_GT(Outer, NextOuter);
	EXPECT_GT(NextOuter, NextInner);
	EXPECT_GT(NextInner, Center);
	EXPECT_FLOAT_EQ(Outer, QmHudMediaIslandWaveBarSettleProgress(6, BarCount, MidSettleTime));
	EXPECT_FLOAT_EQ(NextOuter, QmHudMediaIslandWaveBarSettleProgress(5, BarCount, MidSettleTime));
	EXPECT_FLOAT_EQ(NextInner, QmHudMediaIslandWaveBarSettleProgress(4, BarCount, MidSettleTime));
	for(int Bar = 0; Bar < BarCount; ++Bar)
	{
		EXPECT_FLOAT_EQ(QmHudMediaIslandWaveBarSettleProgress(Bar, BarCount, -0.1f), 0.0f);
		EXPECT_FLOAT_EQ(QmHudMediaIslandWaveBarSettleProgress(Bar, BarCount, 0.9f), 1.0f);
	}
}

TEST(QmHudMediaIslandSource, MovesClockAndFrozenCountIntoStackAndReplacesClockSlotWithWaveform)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string RenderBody = FunctionBody(Source, "void CHud::RenderMediaIsland()");
	const std::string VisibleBody = FunctionBody(Source, "void CHud::EnsureMediaIslandFrameCache() const");

	EXPECT_NE(RenderBody.find("const bool ShowInfoStack = ShowLocalTime || ShowFrozenSummary;"), std::string::npos);
	EXPECT_NE(RenderBody.find("constexpr float InfoStackGap = QmHudMediaIslandScaled(0.8f);"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandMirroredInfoStack"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandWaveBarHeight"), std::string::npos);
	EXPECT_NE(RenderBody.find("constexpr int WaveBarCount = 7;"), std::string::npos);
	EXPECT_NE(RenderBody.find("constexpr int WaveTargetBarCount = 6;"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandWaveBarSettleProgress"), std::string::npos);
	EXPECT_NE(RenderBody.find("constexpr float WaveMaxHeight = QmHudMediaIslandScaled(7.2f);"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandTimerRows"), std::string::npos);
	EXPECT_NE(VisibleBody.find("BuildHudFrozenSummaryText"), std::string::npos);
	EXPECT_EQ(RenderBody.find("ShowFrozenSummaryInBottomRow"), std::string::npos);
	EXPECT_EQ(RenderBody.find("%s CP%d"), std::string::npos);
}

TEST(QmHudMediaIslandSource, DisablesNeteaseOnlyWorkWhenTheHookIsOff)
{
	const std::string HudSource = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string HudCacheBody = FunctionBody(HudSource, "void CHud::EnsureMediaIslandFrameCache() const");
	const std::string VisibleBody = FunctionBody(HudSource, "bool CHud::HasVisibleMediaIsland() const");
	const std::string AvoidanceBody = FunctionBody(HudSource, "float CHud::GetTopIslandAvoidanceRight() const");
	const std::string IntegrationSource = ReadTestSourceFile("src/game/client/components/qmclient/netease/netease_integration.cpp");
	const std::string IntegrationBody = FunctionBody(IntegrationSource, "void CNeteaseIntegration::OnUpdate()");

	EXPECT_NE(HudCacheBody.find("if(g_Config.m_QmNeteaseHookEnable != 0)"), std::string::npos);
	EXPECT_NE(VisibleBody.find("if(g_Config.m_QmHudIslandUseOriginalStyle)"), std::string::npos);
	EXPECT_NE(AvoidanceBody.find("if(g_Config.m_QmHudIslandUseOriginalStyle)"), std::string::npos);
	EXPECT_NE(IntegrationBody.find("if(!g_Config.m_QmNeteaseHookEnable)"), std::string::npos);
	EXPECT_NE(IntegrationBody.find("ClearForStaleMedia();"), std::string::npos);
	EXPECT_NE(FunctionBody(HudSource, "float CHud::RenderLegacyMediaInfoAt(float AnchorX, float CenterY)").find("EnsureMediaIslandFrameCache();"), std::string::npos);
}

TEST(QmHudMediaIslandSource, RenderPathKeepsStableNodesAndEditorRect)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string AnimResolveSource = ReadTestSourceFile("src/game/client/QmUi/QmAnimResolve.cpp");
	const size_t RenderBegin = Source.find("void CHud::RenderMediaIsland()");
	ASSERT_NE(RenderBegin, std::string::npos);
	const size_t RenderEnd = Source.find("float CHud::RenderLegacyMediaInfoAt", RenderBegin);
	ASSERT_NE(RenderEnd, std::string::npos);
	const std::string RenderBody = Source.substr(RenderBegin, RenderEnd - RenderBegin);

	EXPECT_NE(RenderBody.find("HudMediaIslandNodeKey(\"cover_in\")"), std::string::npos);
	EXPECT_NE(RenderBody.find("HudMediaIslandNodeKey(\"cover_out\")"), std::string::npos);
	EXPECT_NE(RenderBody.find("HudMediaIslandNodeKey(\"track_title_in\")"), std::string::npos);
	EXPECT_NE(RenderBody.find("HudMediaIslandNodeKey(\"track_title_out\")"), std::string::npos);
	EXPECT_NE(RenderBody.find("HudMediaIslandNodeKey(\"track_meta_in\")"), std::string::npos);
	EXPECT_NE(RenderBody.find("HudMediaIslandNodeKey(\"track_meta_out\")"), std::string::npos);
	EXPECT_NE(RenderBody.find("StartCapsuleMorph()"), std::string::npos);
	EXPECT_NE(RenderBody.find("m_CapsuleMorphNeedsCapture"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandApplyCapsuleSqueeze"), std::string::npos);
	EXPECT_NE(RenderBody.find("HudMediaIslandNodeKey(\"capsule_morph\")"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandResolveEntranceSprings"), std::string::npos);
	EXPECT_NE(RenderBody.find("HudMediaIslandNodeKey(\"entrance_drop\")"), std::string::npos);
	EXPECT_NE(RenderBody.find("EntranceSprings.m_ExpandProgress"), std::string::npos);
	EXPECT_NE(RenderBody.find("RelaxMediaIslandEntranceSprings"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandEntrancePose"), std::string::npos);
	EXPECT_NE(RenderBody.find("EntrancePose.m_BackgroundColor"), std::string::npos);
	EXPECT_NE(RenderBody.find("EntrancePose.m_ContentAlpha"), std::string::npos);
	EXPECT_NE(RenderBody.find("EntrancePose.m_DisabledCornerRadius"), std::string::npos);
	EXPECT_NE(RenderBody.find("CoverInAlpha * EntranceContentAlpha"), std::string::npos);
	EXPECT_NE(RenderBody.find("TrackTitleInAlpha * EntranceContentAlpha"), std::string::npos);
	EXPECT_NE(RenderBody.find("TimerCapsule.m_Alpha * EntranceContentAlpha"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandDesiredBottomWidth("), std::string::npos);
	EXPECT_EQ(RenderBody.find("TextBoundingBox(BottomFontSize, aLyricsIslandBuf)"), std::string::npos);
	EXPECT_NE(RenderBody.find("QmHudMediaIslandMarqueeOffset("), std::string::npos);
	EXPECT_NE(RenderBody.find("EnableMappedClip("), std::string::npos);
	EXPECT_NE(RenderBody.find("0.42f * EntranceContentAlpha"), std::string::npos);
	EXPECT_NE(RenderBody.find("SdfItem.m_ContentScale = Item.m_ContentScale * EntranceContentAlpha"), std::string::npos);
	EXPECT_NE(RenderBody.find("SatelliteIconSize * Item.m_ContentScale * EntranceContentAlpha"), std::string::npos);
	EXPECT_NE(AnimResolveSource.find("Request.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;"), std::string::npos);
	EXPECT_EQ(RenderBody.find("EUiAnimInterruptPolicy::QUEUE"), std::string::npos);
	EXPECT_EQ(RenderBody.find("m_CoverRotation"), std::string::npos);
	EXPECT_NE(RenderBody.find("m_MediaIslandLastVisibleRect = HudEditorScope.m_VisibleRect;"), std::string::npos);
	EXPECT_NE(RenderBody.find("BeginTransform(EHudEditorElement::MediaIsland, EditorTransformRect, EditorVisibleRect);"), std::string::npos);
	EXPECT_EQ(RenderBody.find("QmHudIslandEdgeMargin"), std::string::npos);
	const std::string HudEditorSource = ReadTestSourceFile("src/game/client/components/hud_editor.cpp");
	EXPECT_NE(HudEditorSource.find("Element == EHudEditorElement::MediaIsland ? QmHudEditor::MEDIA_ISLAND_EDGE_SNAP_DISTANCE : HUD_EDITOR_EDGE_ANCHOR_DISTANCE"), std::string::npos);
	EXPECT_NE(HudEditorSource.find("const float ScreenEdgeSnapDistance = HudEditorEdgeSnapDistance(Visible.m_Element);"), std::string::npos);
}

TEST(QmHudMediaIslandSource, IslandRendersBeforeCheckpointAndFinishEffects)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const size_t OnRenderBegin = Source.find("void CHud::OnRender()");
	ASSERT_NE(OnRenderBegin, std::string::npos);
	const size_t OnRenderEnd = Source.find("void CHud::OnMessage", OnRenderBegin);
	ASSERT_NE(OnRenderEnd, std::string::npos);
	const std::string OnRenderBody = Source.substr(OnRenderBegin, OnRenderEnd - OnRenderBegin);

	const size_t IslandRender = OnRenderBody.find("RenderMediaIsland();");
	const size_t EffectsRender = OnRenderBody.find("RenderDDRaceEffects();");
	ASSERT_NE(IslandRender, std::string::npos);
	ASSERT_NE(EffectsRender, std::string::npos);
	EXPECT_LT(IslandRender, EffectsRender);
}

TEST(QmMediaIslandGpuSdfContract, UsesFixedStd140FriendlyParameterBlock)
{
	static_assert(IGraphics::MEDIA_ISLAND_SDF_MAX_ITEMS == 12);
	static_assert(IGraphics::SMediaIslandSdfParams::DATA_RESERVED == 7);
	static_assert(IGraphics::SMediaIslandSdfParams::DATA_ITEM_STRIDE == 3);
	static_assert(IGraphics::SMediaIslandSdfParams::DATA_BACKDROP_UV == 44);
	static_assert(IGraphics::SMediaIslandSdfParams::DATA_COUNT == 45);
	static_assert(sizeof(vec4) == sizeof(float) * 4);
	static_assert(sizeof(IGraphics::SMediaIslandSdfParams) == IGraphics::SMediaIslandSdfParams::DATA_COUNT * sizeof(vec4));

	IGraphics::SMediaIslandSdfParams Params;
	Params.Clear();
	Params.SetItemCount(12);
	Params.SetHasRightCapsule(true);
	Params.SetMainCorners(IGraphics::CORNER_ALL);
	EXPECT_EQ(Params.ItemCount(), 12);
	EXPECT_TRUE(Params.HasRightCapsule());
	EXPECT_EQ(Params.MainCorners(), IGraphics::CORNER_ALL);

	Params.SetItemCount(13);
	EXPECT_EQ(Params.ItemCount(), IGraphics::MEDIA_ISLAND_SDF_MAX_ITEMS);
}

TEST(QmMediaIslandGpuSdfContract, BackendsPublishActualShaderCapability)
{
	const std::string OpenGlSource = ReadTestSourceFile("src/engine/client/backend/opengl/backend_opengl3.cpp");
	const std::string VulkanSource = ReadTestSourceFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	EXPECT_NE(OpenGlSource.find("m_MediaIslandSdf = m_MediaIslandSdfProgramValid"), std::string::npos);
	EXPECT_NE(VulkanSource.find("m_MediaIslandSdf = true"), std::string::npos);
}

TEST(QmMediaIslandGpuSdfContract, ShapePassAvoidsPerFragmentDistanceArrayAndInactiveItemIterations)
{
	const std::array<const char *, 2> apShaderPaths = {
		"data/shader/media_island_sdf.frag",
		"data/shader/vulkan/media_island_sdf.frag",
	};
	for(const char *pShaderPath : apShaderPaths)
	{
		const std::string ShaderSource = ReadTestSourceFile(pShaderPath);
		EXPECT_NE(ShaderSource.find("gMediaIslandSdfData[45]"), std::string::npos) << pShaderPath;
		EXPECT_NE(ShaderSource.find("ITEM_STRIDE = 3"), std::string::npos) << pShaderPath;
		EXPECT_NE(ShaderSource.find("BlobSdf"), std::string::npos) << pShaderPath;
		EXPECT_NE(ShaderSource.find("BlobExponent"), std::string::npos) << pShaderPath;
		EXPECT_EQ(ShaderSource.find("RadialRipple"), std::string::npos) << pShaderPath;
		EXPECT_EQ(ShaderSource.find("StrongestRipple"), std::string::npos) << pShaderPath;
		EXPECT_EQ(ShaderSource.find("EllipseSdf"), std::string::npos) << pShaderPath;
		EXPECT_NE(ShaderSource.find("float ItemDistance = BlobSdf(Point, ItemShape.xy, ItemShape.zw);"), std::string::npos) << pShaderPath;
		EXPECT_EQ(ShaderSource.find("float ItemDistances[MAX_ITEMS]"), std::string::npos) << pShaderPath;
		EXPECT_EQ(ShaderSource.find("i < MAX_ITEMS"), std::string::npos) << pShaderPath;
		EXPECT_NE(ShaderSource.find("float ShapeDistance = MainDistance;"), std::string::npos) << pShaderPath;
		EXPECT_NE(ShaderSource.find("vec4 ItemShape = Data(ITEM_BASE + i * ITEM_STRIDE);"), std::string::npos) << pShaderPath;

		const std::string ItemLoop = "for(int i = 0; i < ItemCount; ++i)";
		const size_t ShapeLoop = ShaderSource.find(ItemLoop);
		ASSERT_NE(ShapeLoop, std::string::npos) << pShaderPath;
		const size_t RingLoop = ShaderSource.find(ItemLoop, ShapeLoop + ItemLoop.size());
		ASSERT_NE(RingLoop, std::string::npos) << pShaderPath;
		EXPECT_EQ(ShaderSource.find(ItemLoop, RingLoop + ItemLoop.size()), std::string::npos) << pShaderPath;
	}
}

TEST(QmMediaIslandGpuSdfContract, SinglePassShapeReductionMatchesPreviousTwoPassResult)
{
	constexpr float FarDistance = 1000000.0f;
	constexpr float MainDistance = 1.75f;
	constexpr float MainRadius = 8.0f;
	const std::array<float, 12> aItemDistances = {4.0f, 1.2f, -0.5f, 8.0f, 0.0f, 3.0f, -1.0f, 2.5f, 7.0f, 0.5f, 9.0f, -0.2f};
	const std::array<float, 12> aSmoothUnions = {0.0f, 2.0f, 3.5f, 0.0f, 1.0f, 4.0f, 0.0f, 0.5f, 2.5f, 0.0f, 1.5f, 3.0f};
	const std::array<int, 3> aItemCounts = {0, 1, 12};

	for(const int ItemCount : aItemCounts)
	{
		float PreviousSatelliteDistance = FarDistance;
		for(int i = 0; i < ItemCount; ++i)
		{
			PreviousSatelliteDistance = i == 0 ? aItemDistances[i] : QmHudMediaIslandSdfSmoothUnion(PreviousSatelliteDistance, aItemDistances[i], MainRadius * 0.28f);
		}
		float PreviousShapeDistance = std::min(MainDistance, PreviousSatelliteDistance);
		for(int i = 0; i < ItemCount; ++i)
		{
			if(aSmoothUnions[i] > 0.0f)
				PreviousShapeDistance = std::min(PreviousShapeDistance, QmHudMediaIslandSdfSmoothUnion(MainDistance, aItemDistances[i], aSmoothUnions[i]));
		}

		float SinglePassSatelliteDistance = FarDistance;
		float SinglePassShapeDistance = MainDistance;
		for(int i = 0; i < ItemCount; ++i)
		{
			SinglePassSatelliteDistance = i == 0 ? aItemDistances[i] : QmHudMediaIslandSdfSmoothUnion(SinglePassSatelliteDistance, aItemDistances[i], MainRadius * 0.28f);
			if(aSmoothUnions[i] > 0.0f)
				SinglePassShapeDistance = std::min(SinglePassShapeDistance, QmHudMediaIslandSdfSmoothUnion(MainDistance, aItemDistances[i], aSmoothUnions[i]));
		}
		SinglePassShapeDistance = std::min(SinglePassShapeDistance, SinglePassSatelliteDistance);

		EXPECT_FLOAT_EQ(SinglePassShapeDistance, PreviousShapeDistance) << "item count " << ItemCount;
	}
}

TEST(QmMediaIslandGpuSdfContract, OpenGlAndVulkanUseTheSameFragmentMainPath)
{
	const std::string OpenGlSource = ReadTestSourceFile("data/shader/media_island_sdf.frag");
	const std::string VulkanSource = ReadTestSourceFile("data/shader/vulkan/media_island_sdf.frag");
	const size_t OpenGlMain = OpenGlSource.find("void main()");
	const size_t VulkanMain = VulkanSource.find("void main()");
	ASSERT_NE(OpenGlMain, std::string::npos);
	ASSERT_NE(VulkanMain, std::string::npos);

	const auto CompactWhitespace = [](std::string Source) {
		Source.erase(std::remove_if(Source.begin(), Source.end(), [](char Character) {
			return Character == ' ' || Character == '\t' || Character == '\r' || Character == '\n';
		}),
			Source.end());
		return Source;
	};
	EXPECT_EQ(CompactWhitespace(OpenGlSource.substr(OpenGlMain)), CompactWhitespace(VulkanSource.substr(VulkanMain)));
}

TEST(QmHudMediaIslandSource, MediaIslandUsesGpuSdfCommandWithoutCpuRasterization)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const size_t IslandBegin = Source.find("void CHud::RenderMediaIsland()");
	ASSERT_NE(IslandBegin, std::string::npos);
	const size_t IslandEnd = Source.find("float CHud::RenderLegacyMediaInfoAt", IslandBegin);
	ASSERT_NE(IslandEnd, std::string::npos);
	const std::string IslandBody = Source.substr(IslandBegin, IslandEnd - IslandBegin);

	const size_t SdfDraw = IslandBody.find("Graphics()->RenderMediaIslandSdf");
	ASSERT_NE(SdfDraw, std::string::npos);
	EXPECT_EQ(IslandBody.find("Graphics()->RenderMediaIslandSdf", SdfDraw + 1), std::string::npos);
	EXPECT_NE(IslandBody.find("HasMediaIslandSdf"), std::string::npos);
	EXPECT_NE(Source.find("DrawMediaIslandArcGeometry"), std::string::npos);
	EXPECT_EQ(IslandBody.find("UpdateTexture"), std::string::npos);
	EXPECT_EQ(IslandBody.find("PixelX"), std::string::npos);
	EXPECT_EQ(IslandBody.find("PixelY"), std::string::npos);
	EXPECT_EQ(Source.find("m_vMediaIslandSdfPixels"), std::string::npos);
	EXPECT_EQ(Source.find("m_MediaIslandSdfTexture"), std::string::npos);
	EXPECT_EQ(IslandBody.find("BeginRenderTarget"), std::string::npos);
}

TEST(QmHudMediaIslandSource, BackgroundBlurUsesTheAnimatedCombinedSdfIncludingAtZeroOpacity)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string Header = ReadTestSourceFile("src/game/client/components/hud.h");
	const std::string GraphicsHeader = ReadTestSourceFile("src/engine/graphics.h");
	const std::string ThreadedHeader = ReadTestSourceFile("src/engine/client/graphics_threaded.h");
	const std::string ThreadedSource = ReadTestSourceFile("src/engine/client/graphics_threaded.cpp");
	const std::string OpenGlBackend = ReadTestSourceFile("src/engine/client/backend/opengl/backend_opengl3.cpp");
	const std::string VulkanBackend = ReadTestSourceFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const std::string PrepareBlur = FunctionBody(Source, "bool CHud::PrepareMediaIslandBlur()");
	const std::string OnRelease = FunctionBody(Source, "void CHud::OnRelease()");
	const std::string OnRender = FunctionBody(Source, "void CHud::OnRender()");
	const std::string ResetContainers = FunctionBody(Source, "void CHud::ResetHudContainers()");
	const size_t IslandBegin = Source.find("void CHud::RenderMediaIsland()");
	ASSERT_NE(IslandBegin, std::string::npos);
	const size_t IslandEnd = Source.find("float CHud::RenderLegacyMediaInfoAt", IslandBegin);
	ASSERT_NE(IslandEnd, std::string::npos);
	const std::string IslandBody = Source.substr(IslandBegin, IslandEnd - IslandBegin);

	EXPECT_NE(Header.find("m_MediaIslandBlurSource"), std::string::npos);
	EXPECT_NE(Header.find("void OnRelease() override;"), std::string::npos);
	EXPECT_NE(PrepareBlur.find("QmHudMediaIslandShouldPrepareBackdropBlur"), std::string::npos);
	EXPECT_NE(PrepareBlur.find("HasMediaIslandSdf"), std::string::npos);
	EXPECT_NE(PrepareBlur.find("IsBackbufferCaptureSupported"), std::string::npos);
	EXPECT_NE(PrepareBlur.find("IsRenderTargetGaussianBlurSupported"), std::string::npos);
	EXPECT_NE(PrepareBlur.find("CaptureBackbufferToRenderTarget"), std::string::npos);
	EXPECT_NE(PrepareBlur.find("GaussianBlurRenderTarget"), std::string::npos);
	EXPECT_NE(PrepareBlur.find("m_QmGaussianBlur"), std::string::npos);
	EXPECT_NE(PrepareBlur.find("QmHudMediaIslandShouldRefreshBackdropBlur"), std::string::npos);
	EXPECT_EQ(PrepareBlur.find("m_QmBetterScoreboard"), std::string::npos);
	EXPECT_NE(OnRelease.find("DestroyMediaIslandBlurTargets"), std::string::npos);
	EXPECT_NE(OnRender.find("g_Config.m_QmHudIslandUseOriginalStyle"), std::string::npos);
	EXPECT_NE(OnRender.find("DestroyMediaIslandBlurTargets"), std::string::npos);
	EXPECT_NE(ResetContainers.find("m_MediaIslandBlurReady = false"), std::string::npos);
	EXPECT_NE(IslandBody.find("DrawMediaIslandGeometryFallback"), std::string::npos);

	EXPECT_NE(IslandBody.find("PrepareMediaIslandBlur()"), std::string::npos);
	EXPECT_NE(IslandBody.find("QmHudMediaIslandBackdropUv(CurrentSdfState.m_Rect"), std::string::npos);
	EXPECT_NE(IslandBody.find("Graphics()->RenderMediaIslandSdf(GpuSdfParams, Backdrop)"), std::string::npos);
	EXPECT_EQ(Source.find("void CHud::RenderMediaIslandBlur"), std::string::npos);

	EXPECT_NE(GraphicsHeader.find("DATA_BACKDROP_UV"), std::string::npos);
	EXPECT_NE(GraphicsHeader.find("CRenderTargetHandle Backdrop"), std::string::npos);
	EXPECT_NE(ThreadedHeader.find("m_BackdropTargetId"), std::string::npos);
	EXPECT_NE(ThreadedSource.find("Cmd.m_BackdropTargetId"), std::string::npos);
	EXPECT_NE(OpenGlBackend.find("gBackdropSampler"), std::string::npos);
	EXPECT_NE(OpenGlBackend.find("Target.m_Texture"), std::string::npos);
	EXPECT_NE(VulkanBackend.find("m_StandardTexturedDescriptorSetLayout, m_QuadUniformDescriptorSetLayout"), std::string::npos);
	EXPECT_NE(VulkanBackend.find("pCommand->m_BackdropTargetId"), std::string::npos);
}

TEST(QmHudMediaIslandSource, SharedScaleCoversLayoutTimerAndEntranceWithoutMovingTheTopAnchor)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string Logic = ReadTestSourceFile("src/game/client/components/hud_media_island_logic.h");
	const std::string AvoidanceBody = FunctionBody(Source, "float CHud::GetTopIslandAvoidanceRight() const");
	const std::string IslandBody = FunctionBody(Source, "void CHud::RenderMediaIsland()");
	const std::string TimerBody = FunctionBody(Source, "SHudTopTimerCapsuleInfo BuildHudTopTimerCapsuleInfo(const SHudGameTimerInfo &TimerInfo)");

	EXPECT_NE(Logic.find("QmHudMediaIslandDesignScale = 0.8f"), std::string::npos);
	EXPECT_NE(Logic.find("QmHudMediaIslandScaled(16.0f)"), std::string::npos);
	EXPECT_NE(AvoidanceBody.find("QmHudMediaIslandScaled(16.0f)"), std::string::npos);
	EXPECT_NE(AvoidanceBody.find("QmHudMediaIslandScaled(5.8f)"), std::string::npos);
	EXPECT_NE(IslandBody.find("QmHudMediaIslandScaled(16.0f)"), std::string::npos);
	EXPECT_NE(IslandBody.find("QmHudMediaIslandScaled(12.0f)"), std::string::npos);
	EXPECT_NE(IslandBody.find("QmHudMediaIslandScaled(5.8f)"), std::string::npos);
	EXPECT_NE(IslandBody.find("const float IslandY = 1.0f;"), std::string::npos);
	EXPECT_NE(TimerBody.find("QmHudMediaIslandScaled(TimerInfo.m_FontSize)"), std::string::npos);
}

// 意图：主胶囊保留宽度只由真实内容与左侧倒计时副岛决定，渲染路径与避让路径必须同源，
// 且不得再拿状态区（时钟/冰冻统计）或观战卫星当保留依据。
TEST(QmHudMediaIslandSource, BothLayoutPathsShareTheSameMainCapsuleReservation)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string Header = ReadTestSourceFile("src/game/client/components/hud.h");
	const std::string AvoidanceBody = FunctionBody(Source, "float CHud::GetTopIslandAvoidanceRight() const");
	const std::string IslandBody = FunctionBody(Source, "void CHud::RenderMediaIsland()");

	EXPECT_NE(Header.find("bool HasVisibleCountdownSatellite() const"), std::string::npos);
	EXPECT_NE(AvoidanceBody.find("QmHudMediaIslandShouldReserveMainCapsule(HasMediaState, ShowTeam, m_MediaIslandAnimState.HasVisibleCountdownSatellite())"), std::string::npos);
	EXPECT_NE(IslandBody.find("QmHudMediaIslandShouldReserveMainCapsule(HasMediaState, ShowTeam, AnimState.HasVisibleCountdownSatellite())"), std::string::npos);
	// 回归护栏：状态区不再算主胶囊内容，观战卫星也不再作为保留依据。
	EXPECT_EQ(IslandBody.find("ShowTeam || ShowInfoStack"), std::string::npos);
	EXPECT_EQ(IslandBody.find("QmHudMediaIslandShouldReserveMainCapsule(HasMediaState, HasSpectatorSatellitePresentation"), std::string::npos);
}

TEST(QmHudMediaIslandSource, BackdropAndOuterShadowFollowTheSameCombinedSdf)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string OpenGlShader = ReadTestSourceFile("data/shader/media_island_sdf.frag");
	const std::string VulkanShader = ReadTestSourceFile("data/shader/vulkan/media_island_sdf.frag");
	const std::string IslandBody = FunctionBody(Source, "void CHud::RenderMediaIsland()");
	const std::string ShadowBody = FunctionBody(Source, "void DrawMediaIslandOuterShadowFallback(IGraphics *pGraphics, const SHudMediaIslandSdfRenderState &State)");
	const std::string FallbackBody = FunctionBody(Source, "void DrawMediaIslandGeometryFallback(IGraphics *pGraphics, const SHudMediaIslandSdfRenderState &State)");

	EXPECT_NE(Source.find("MEDIA_ISLAND_OUTER_SHADOW_PIXELS = 5.0f"), std::string::npos);
	EXPECT_NE(Source.find("MEDIA_ISLAND_OUTER_SHADOW_OPACITY = 0.35f"), std::string::npos);
	EXPECT_NE(IslandBody.find("m_OuterShadowSize = ScreenPixelSize * MEDIA_ISLAND_OUTER_SHADOW_PIXELS"), std::string::npos);
	EXPECT_NE(IslandBody.find("m_OuterShadowOpacity = MEDIA_ISLAND_OUTER_SHADOW_OPACITY * EntrancePose.m_BackgroundColor.a"), std::string::npos);
	EXPECT_NE(IslandBody.find("QmHudMediaIslandBackdropUv(CurrentSdfState.m_Rect"), std::string::npos);
	EXPECT_NE(ShadowBody.find("State.m_Items"), std::string::npos);
	EXPECT_NE(ShadowBody.find("State.m_HasRightCapsule"), std::string::npos);
	EXPECT_NE(FallbackBody.find("DrawMediaIslandOuterShadowFallback"), std::string::npos);

	for(const std::string *pShader : {&OpenGlShader, &VulkanShader})
	{
		const size_t ShadowParams = pShader->find("vec4 ShadowParams = Data(7);");
		const size_t ShadowComposite = pShader->find("Composite(PremulColor, Alpha, vec4(0.0, 0.0, 0.0, ShadowParams.y * PanelAlpha)");
		const size_t BackgroundComposite = pShader->find("Composite(PremulColor, Alpha, vec4(ShapeColor, PanelAlpha), ShapeCoverage)");
		ASSERT_NE(ShadowParams, std::string::npos);
		ASSERT_NE(ShadowComposite, std::string::npos);
		ASSERT_NE(BackgroundComposite, std::string::npos);
		EXPECT_NE(pShader->find("ShapeDistance = min(ShapeDistance, SatelliteDistance);"), std::string::npos);
		EXPECT_NE(pShader->find("ShapeDistance = min(ShapeDistance, CapsuleDistance);"), std::string::npos);
		EXPECT_NE(pShader->find("texture(gBackdropSampler"), std::string::npos);
		// 亚克力板语义：背景色与模糊底图先按面板透明度混合，再整体按它合成，
		// 因此透明度 0 时整块板（含外圈阴影）消失，而不是"只去掉背景色、留下不透明底图"。
		EXPECT_NE(pShader->find("float PanelAlpha = clamp(Background.a, 0.0, 1.0);"), std::string::npos);
		// const 非常量初始化在 GLSL 330 下不合法，OpenGL 与 Vulkan 两份都必须保持可编译的写法。
		EXPECT_EQ(pShader->find("const float PanelAlpha"), std::string::npos);
		EXPECT_NE(pShader->find("vec3 ShapeColor = mix(Backdrop, Background.rgb, PanelAlpha);"), std::string::npos);
		EXPECT_NE(pShader->find("Composite(PremulColor, Alpha, Background, ShapeCoverage);"), std::string::npos);
		EXPECT_LT(ShadowParams, ShadowComposite);
		EXPECT_LT(ShadowComposite, BackgroundComposite);
	}
}

TEST(QmHudPresentationSource, MediaIslandAndWeaponHudUseContinuousPresentationState)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/hud.cpp");
	const std::string Header = ReadTestSourceFile("src/game/client/components/hud.h");

	EXPECT_NE(Source.find("#include <game/client/QmUi/QmAnimResolve.h>"), std::string::npos);
	EXPECT_EQ(Source.find("float ResolvePresentationStateValue("), std::string::npos);

	const size_t IslandBegin = Source.find("void CHud::RenderMediaIsland()");
	ASSERT_NE(IslandBegin, std::string::npos);
	const size_t IslandEnd = Source.find("float CHud::RenderLegacyMediaInfoAt", IslandBegin);
	ASSERT_NE(IslandEnd, std::string::npos);
	const std::string IslandBody = Source.substr(IslandBegin, IslandEnd - IslandBegin);
	EXPECT_NE(IslandBody.find("ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode"), std::string::npos);
	EXPECT_NE(IslandBody.find("ResolveUiPresentationStateValue(AnimRuntime, CoverInNode"), std::string::npos);
	EXPECT_NE(IslandBody.find("BuildContentSpring(false)"), std::string::npos);
	EXPECT_NE(IslandBody.find("TrackExitSpring"), std::string::npos);
	EXPECT_NE(IslandBody.find("ExitTimeScale"), std::string::npos);
	EXPECT_EQ(IslandBody.find("EUiAnimInterruptPolicy::QUEUE"), std::string::npos);

	const size_t PlayerStateBegin = Source.find("void CHud::RenderPlayerState");
	ASSERT_NE(PlayerStateBegin, std::string::npos);
	const size_t PlayerStateEnd = Source.find("void CHud::RenderNinjaBarPos", PlayerStateBegin);
	ASSERT_NE(PlayerStateEnd, std::string::npos);
	const std::string PlayerStateBody = Source.substr(PlayerStateBegin, PlayerStateEnd - PlayerStateBegin);
	EXPECT_NE(Source.find("HudWeaponPresentationNodeKey"), std::string::npos);
	EXPECT_NE(Header.find("SHudWeaponPresentationState"), std::string::npos);
	EXPECT_NE(PlayerStateBody.find("ResolveUiPresentationStateValue(AnimRuntime, WeaponNode"), std::string::npos);
	EXPECT_EQ(Source.find("m_aHudWeaponSwitchStartTimes"), std::string::npos);
	EXPECT_EQ(Source.find("HudActiveWeaponSwitchScale"), std::string::npos);
}

// ==== 灵动岛式通知（启动赞助提醒 / 关闭挽留）====

TEST(QmIslandNotice, TargetsDropThenExpandAndCollapseThenRise)
{
	qm_island::SNoticeState State;

	// 从未出现 → 显示：掉落立刻启动；展开置位，真正开始还要等掉落落定（见 ResolveSprings）。
	const qm_island::SNoticeTargets ShowTargets = qm_island::ResolveTargets(State, true);
	EXPECT_FLOAT_EQ(ShowTargets.m_Drop, 1.0f);
	EXPECT_TRUE(ShowTargets.m_Expand);

	// 收起时只要展开通道还没收拢，掉落目标必须保持 1：先收成黑球，不许同时往上飞。
	State.m_DropProgress = 1.0f;
	State.m_ExpandProgress = 1.0f;
	const qm_island::SNoticeTargets Collapsing = qm_island::ResolveTargets(State, false);
	EXPECT_FLOAT_EQ(Collapsing.m_Drop, 1.0f);
	EXPECT_FALSE(Collapsing.m_Expand);

	// 已经收成球（展开通道归一）之后，掉落目标才归零 → 黑球上滑离场。
	State.m_ExpandProgress = 0.0f;
	const qm_island::SNoticeTargets Rising = qm_island::ResolveTargets(State, false);
	EXPECT_FLOAT_EQ(Rising.m_Drop, 0.0f);
	EXPECT_FALSE(Rising.m_Expand);

	// 出场途中重新显示：掉落保持 1，展开重新置位，弹簧速度由运行时继承。
	State.m_DropProgress = 1.0f;
	const qm_island::SNoticeTargets Reshow = qm_island::ResolveTargets(State, true);
	EXPECT_FLOAT_EQ(Reshow.m_Drop, 1.0f);
	EXPECT_TRUE(Reshow.m_Expand);
}

TEST(QmIslandNotice, CountdownOnlyRunsAfterFullyExpanded)
{
	qm_island::SNoticeState State;
	State.m_DurationSeconds = 5.0f;
	State.m_DropProgress = 1.0f;
	State.m_ExpandProgress = 0.5f;

	// 形变阶段不吞时间：没完全展开就不该开始倒计时。
	EXPECT_FALSE(qm_island::AdvanceCountdown(State, true, 2.0f));
	EXPECT_FLOAT_EQ(State.m_ElapsedSeconds, 0.0f);
	EXPECT_FLOAT_EQ(qm_island::RemainingFraction(State), 1.0f);

	// 完全展开后才推进。
	State.m_ExpandProgress = 1.0f;
	EXPECT_FALSE(qm_island::AdvanceCountdown(State, true, 4.0f));
	EXPECT_FLOAT_EQ(State.m_ElapsedSeconds, 4.0f);
	EXPECT_FLOAT_EQ(qm_island::RemainingFraction(State), 0.2f);
	EXPECT_TRUE(qm_island::AdvanceCountdown(State, true, 1.0f));
	EXPECT_FLOAT_EQ(qm_island::RemainingFraction(State), 0.0f);

	// 到时后业务已收起：出场动画期间不再累加，环也停在「空」，不会闪回满圈。
	EXPECT_FALSE(qm_island::AdvanceCountdown(State, false, 1.0f));
	EXPECT_FLOAT_EQ(qm_island::RemainingFraction(State), 0.0f);
}

TEST(QmIslandNotice, NeedsRenderKeepsDrawingUntilTheExitAnimationSettles)
{
	qm_island::SNoticeState State;
	EXPECT_FALSE(qm_island::NeedsRender(State, false));
	EXPECT_TRUE(qm_island::NeedsRender(State, true));

	// 业务已经收起，但出场动画还在跑：必须继续画，否则退化成瞬间消失。
	State.m_ExpandProgress = 0.5f;
	EXPECT_TRUE(qm_island::NeedsRender(State, false));
	State.m_ExpandProgress = 0.0f;
	State.m_DropProgress = 0.4f;
	EXPECT_TRUE(qm_island::NeedsRender(State, false));

	// 两通道都收完才停手，并允许复位。
	State.m_DropProgress = 0.0f;
	EXPECT_FALSE(qm_island::NeedsRender(State, false));
	State.m_ElapsedSeconds = 3.0f;
	qm_island::Reset(State);
	EXPECT_FLOAT_EQ(State.m_ElapsedSeconds, 0.0f);
	EXPECT_FLOAT_EQ(State.m_DropProgress, 0.0f);
	EXPECT_FLOAT_EQ(State.m_ExpandProgress, 0.0f);
	EXPECT_FLOAT_EQ(State.m_DurationSeconds, 5.0f);
}

TEST(QmIslandNotice, RingColorTurnsFromGreenToRedAsTimeRunsOut)
{
	const ColorRGBA Full = qm_island::CountdownRingColor(1.0f);
	const ColorRGBA Half = qm_island::CountdownRingColor(0.5f);
	const ColorRGBA Empty = qm_island::CountdownRingColor(0.0f);
	EXPECT_GT(Full.g, Full.r);
	EXPECT_GT(Empty.r, Empty.g);
	EXPECT_GT(Half.r, Full.r);
	EXPECT_LT(Half.r, Empty.r);
}

TEST(QmIslandNotice, LayoutCentersTheBodyAndKeepsTheRingOutsideIt)
{
	const CUIRect Screen = {0.0f, 0.0f, 1280.0f, 720.0f};
	const qm_island::SNoticeLayout Layout = qm_island::ResolveLayout(Screen, 300.0f, 26.0f, 10.0f, 2.0f, 1.5f);
	EXPECT_FLOAT_EQ(Layout.m_Body.w, 300.0f);
	EXPECT_FLOAT_EQ(Layout.m_Body.h, 26.0f);
	EXPECT_FLOAT_EQ(Layout.m_Body.x, 490.0f);
	EXPECT_FLOAT_EQ(Layout.m_Body.y, 10.0f);
	EXPECT_FLOAT_EQ(Layout.m_RingThickness, 2.0f);
	// 环中心线 = 半厚 + 间隙：环的内沿离主体正好一个间隙，整条环都在主体外侧。
	EXPECT_FLOAT_EQ(Layout.m_RingOffset, 2.5f);
	EXPECT_FLOAT_EQ(Layout.m_RingOffset - Layout.m_RingThickness * 0.5f, 1.5f);

	// 超窄屏时主体被夹住，仍然居中。
	const CUIRect Narrow = {0.0f, 0.0f, 100.0f, 240.0f};
	const qm_island::SNoticeLayout NarrowLayout = qm_island::ResolveLayout(Narrow, 300.0f, 26.0f, 10.0f, 2.0f, 1.5f);
	EXPECT_FLOAT_EQ(NarrowLayout.m_Body.w, 80.0f);
	EXPECT_FLOAT_EQ(NarrowLayout.m_Body.x, 10.0f);

	// 环厚有下限，避免高 DPI 下细到看不见。
	const qm_island::SNoticeLayout Thin = qm_island::ResolveLayout(Screen, 300.0f, 26.0f, 10.0f, 0.2f, 0.0f);
	EXPECT_FLOAT_EQ(Thin.m_RingThickness, 1.0f);
	EXPECT_FLOAT_EQ(Thin.m_RingOffset, 0.5f);
}

TEST(QmIslandNotice, PerimeterPointWalksTheCapsuleClockwiseFromTheTop)
{
	constexpr float Pi = 3.14159265359f;
	const CUIRect Capsule = {100.0f, 50.0f, 300.0f, 26.0f};
	const float Radius = 13.0f;
	const float Perimeter = qm_island::RoundedRectPerimeterLength(Capsule, Radius);
	// 胶囊：两条直边各 2*(150-13)，两端各半圆。
	EXPECT_FLOAT_EQ(Perimeter, 4.0f * 137.0f + 2.0f * Pi * 13.0f);

	// 起点是上边中点；半圈落在下边中点；走满一圈回到起点。
	const vec2 Top = qm_island::RoundedRectPerimeterPoint(Capsule, Radius, 0.0f);
	EXPECT_FLOAT_EQ(Top.x, 250.0f);
	EXPECT_FLOAT_EQ(Top.y, 50.0f);
	const vec2 Bottom = qm_island::RoundedRectPerimeterPoint(Capsule, Radius, 0.5f);
	EXPECT_FLOAT_EQ(Bottom.x, 250.0f);
	EXPECT_FLOAT_EQ(Bottom.y, 76.0f);
	const vec2 Wrapped = qm_island::RoundedRectPerimeterPoint(Capsule, Radius, 1.0f);
	EXPECT_FLOAT_EQ(Wrapped.x, Top.x);
	EXPECT_FLOAT_EQ(Wrapped.y, Top.y);

	// 端部圆弧中点：上边右半段走完 137 后进入右端圆弧，再走半段弧长即最右点。
	const float CapHalf = (Pi * Radius) / Perimeter;
	const vec2 Rightmost = qm_island::RoundedRectPerimeterPoint(Capsule, Radius, 137.0f / Perimeter + CapHalf);
	EXPECT_NEAR(Rightmost.x, 400.0f, 0.001f);
	EXPECT_NEAR(Rightmost.y, 63.0f, 0.001f);

	// 宽高相等时退化成圆周：0.25 / 0.75 分别是最右 / 最左。
	const CUIRect Circle = {0.0f, 0.0f, 40.0f, 40.0f};
	EXPECT_FLOAT_EQ(qm_island::RoundedRectPerimeterLength(Circle, 20.0f), 2.0f * Pi * 20.0f);
	const vec2 CircleRight = qm_island::RoundedRectPerimeterPoint(Circle, 20.0f, 0.25f);
	EXPECT_NEAR(CircleRight.x, 40.0f, 0.001f);
	EXPECT_NEAR(CircleRight.y, 20.0f, 0.001f);
	const vec2 CircleLeft = qm_island::RoundedRectPerimeterPoint(Circle, 20.0f, 0.75f);
	EXPECT_NEAR(CircleLeft.x, 0.0f, 0.001f);
	EXPECT_NEAR(CircleLeft.y, 20.0f, 0.001f);
}

TEST(QmIslandNotice, OutlineRingFieldsReachTheGpuParamsAndGrowTheQuad)
{
	SHudMediaIslandSdfRenderState State;
	State.m_MainRect = {490.0f, 10.0f, 300.0f, 26.0f};
	State.m_MainRadius = 13.0f;
	State.m_MainCorners = IGraphics::CORNER_ALL;
	State.m_BackgroundColor = ColorRGBA(0.10f, 0.11f, 0.14f, 0.82f);
	State.m_ScreenPixelSize = 1.0f;
	State.m_ItemCount = 1;
	State.m_Items[0].m_Center = vec2(640.0f, 23.0f);
	State.m_Items[0].m_Radii = vec2(150.0f, 13.0f);
	State.m_Items[0].m_ContentAlpha = 1.0f;
	State.m_Items[0].m_CountdownProgress = 0.5f;
	State.m_Items[0].m_RingColor = ColorRGBA(0.5f, 0.8f, 0.4f, 1.0f);

	const CUIRect WithoutRing = QmHudMediaIslandSdfOuterRect(State);
	State.m_OutlineRingThickness = 2.0f;
	State.m_OutlineRingOffset = 2.5f;
	const CUIRect WithRing = QmHudMediaIslandSdfOuterRect(State);

	// 环外沿 = 中心线外扩 + 半厚，外接矩形必须为此留出余量（再加一圈羽化），否则环被裁掉。
	EXPECT_FLOAT_EQ(State.m_MainRect.y - WithoutRing.y, 1.5f);
	EXPECT_FLOAT_EQ(State.m_MainRect.y - WithRing.y, 2.5f + 1.0f + 0.9f);
	EXPECT_GT(WithRing.w, WithoutRing.w);

	IGraphics::SMediaIslandSdfParams Params;
	ASSERT_TRUE(QmHudMediaIslandBuildGpuSdfParams(State, Params));
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_RESERVED].z, 2.0f);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_RESERVED].w, 2.5f);

	// 关闭轮廓环时回到原来的语义（HUD 卫星环那条路径读到 0 就照旧画圆环）。
	State.m_OutlineRingThickness = 0.0f;
	ASSERT_TRUE(QmHudMediaIslandBuildGpuSdfParams(State, Params));
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_RESERVED].z, 0.0f);
}

TEST(QmIslandNoticeShader, OutlineRingModeIsOptInAndSharedByBothBackends)
{
	for(const char *pShaderPath : {"data/shader/media_island_sdf.frag", "data/shader/vulkan/media_island_sdf.frag"})
	{
		const std::string Source = ReadTestSourceFile(pShaderPath);
		EXPECT_NE(Source.find("float OutlineThickness = max(ShadowParams.z, 0.0);"), std::string::npos) << pShaderPath;
		EXPECT_NE(Source.find("abs(MainDistance + max(ShadowParams.w, 0.0)) - OutlineThickness * 0.5"), std::string::npos) << pShaderPath;
		EXPECT_NE(Source.find("float RoundedRectPerimeterLength(vec4 Rect, float Radius)"), std::string::npos) << pShaderPath;
		EXPECT_NE(Source.find("float RoundedRectPerimeterFraction(vec2 Point, vec4 Rect, float Radius)"), std::string::npos) << pShaderPath;
		EXPECT_NE(Source.find("RoundedRectPerimeterFraction(Point, Data(1), MainRadius)"), std::string::npos) << pShaderPath;
		EXPECT_NE(Source.find("1.0 - smoothstep(OutlineProgress - ArcEdge, OutlineProgress + ArcEdge, Along)"), std::string::npos) << pShaderPath;
		// 整圈底轨与进度弧同一层次：底轨 18%，进度弧吃 ContentAlpha。
		EXPECT_NE(Source.find("OutlineTrack.a *= 0.18 * OutlineAlpha;"), std::string::npos) << pShaderPath;
		// 关闭时退回「每个 item 一个圆环」，HUD 卫星倒计时的语义不能被改掉。
		EXPECT_NE(Source.find("float RingRadius = MainParams.z * ItemParams.z;"), std::string::npos) << pShaderPath;
	}
}

TEST(QmIslandNoticeSource, SponsorNudgeSequencesDropExpandCountdownAndRise)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderSponsorNudge(CUIRect Screen)");
	ASSERT_FALSE(Body.empty());

	const size_t NeedsRender = Body.find("qm_island::NeedsRender(m_QmSponsorNudgeNotice, Visible)");
	const size_t ResolveSprings = Body.find("qm_island::ResolveSprings(");
	const size_t Advance = Body.find("qm_island::AdvanceCountdown(");
	const size_t Dismiss = Body.find("DismissSponsorNudge(false)");
	const size_t Ring = Body.find("m_OutlineRingThickness");
	ASSERT_NE(NeedsRender, std::string::npos);
	ASSERT_NE(ResolveSprings, std::string::npos);
	ASSERT_NE(Advance, std::string::npos);
	ASSERT_NE(Dismiss, std::string::npos);
	ASSERT_NE(Ring, std::string::npos);
	// 顺序：先判断还要不要继续画 → 推两条弹簧 → 倒计时到时收起（收起后由同一段动画走完出场）。
	EXPECT_LT(NeedsRender, ResolveSprings);
	EXPECT_LT(ResolveSprings, Advance);
	EXPECT_LT(Advance, Dismiss);
	// 倒计时读数由状态机给出，不再各处硬编码 5 秒。
	EXPECT_NE(Body.find("qm_island::RemainingFraction(m_QmSponsorNudgeNotice)"), std::string::npos);
	EXPECT_EQ(Body.find("5.0f"), std::string::npos);

	// 光辉（外圈阴影）从这条路径上彻底移除。
	EXPECT_EQ(Body.find("ApplyOuterShadow"), std::string::npos);
	EXPECT_EQ(Source.find("qm_island::ApplyOuterShadow"), std::string::npos);
	EXPECT_EQ(Source.find("m_QmSponsorNudgeElapsed"), std::string::npos);
}

TEST(QmIslandNoticeSource, GeometryFallbackDrawsTheOutlineRingWhenSdfIsUnavailable)
{
	const std::string Source = ReadTestSourceFile("src/game/client/QmUi/QmIslandSurface.cpp");
	const std::string Fallback = FunctionBody(Source, "void RenderGeometryFallback(IGraphics *pGraphics, const SHudMediaIslandSdfRenderState &State)");
	ASSERT_FALSE(Fallback.empty());
	EXPECT_NE(Fallback.find("DrawOutlineRingFallback"), std::string::npos);
	// 轮廓环模式下不再画「每个 item 自己的圆环」，否则宽岛中间会多出一个与主体无关的小圆圈。
	EXPECT_NE(Fallback.find("State.m_OutlineRingThickness <= 0.0f"), std::string::npos);

	const std::string RingBody = FunctionBody(Source, "void DrawOutlineRingFallback(IGraphics *pGraphics, const SHudMediaIslandSdfRenderState &State)");
	ASSERT_FALSE(RingBody.empty());
	EXPECT_NE(RingBody.find("RoundedRectPerimeterPoint"), std::string::npos);
	EXPECT_NE(RingBody.find("0.18f * Item.m_ContentAlpha"), std::string::npos);
	EXPECT_NE(RingBody.find("Item.m_CountdownProgress"), std::string::npos);
}
