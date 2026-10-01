// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/graphics.h>
#include <engine/shared/config.h>

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

TEST(QmHudMediaIslandSatellite, ParsesChineseServerMuteMessages)
{
	int Seconds = 0;
	EXPECT_EQ(QmHudParseSpamProtectionMute("你在接下来的 17 秒内不能发言。", "Main", "Dummy", Seconds), EHudMediaIslandMuteMessage::REMAINING);
	EXPECT_EQ(Seconds, 17);

	EXPECT_EQ(QmHudParseSpamProtectionMute("'Main' 已被禁言 60 秒（Spam protection）", "Main", "Dummy", Seconds), EHudMediaIslandMuteMessage::SPAM_BROADCAST);
	EXPECT_EQ(Seconds, 60);
	EXPECT_EQ(QmHudParseSpamProtectionMute("'O'Brien' 已被禁言 45 秒（Spam protection）", "O'Brien", "Dummy", Seconds), EHudMediaIslandMuteMessage::SPAM_BROADCAST);
	EXPECT_EQ(Seconds, 45);

	// 分身名字同样要能命中。
	EXPECT_EQ(QmHudParseSpamProtectionMute("'Dummy' 已被禁言 30 秒（Spam protection）", "Main", "Dummy", Seconds), EHudMediaIslandMuteMessage::SPAM_BROADCAST);
	EXPECT_EQ(Seconds, 30);
}

TEST(QmHudMediaIslandSatellite, IgnoresChineseServerMuteMessagesForOtherCauses)
{
	int Seconds = 0;
	EXPECT_EQ(QmHudParseSpamProtectionMute("'Other' 已被禁言 60 秒（Spam protection）", "Main", "Dummy", Seconds), EHudMediaIslandMuteMessage::NONE);
	EXPECT_EQ(QmHudParseSpamProtectionMute("'Main' 已被禁言 60 秒（manual）", "Main", "Dummy", Seconds), EHudMediaIslandMuteMessage::NONE);
	// 无原因后缀的广播（服务端不带 pReason 时的分支）不视为刷屏禁言。
	EXPECT_EQ(QmHudParseSpamProtectionMute("'Main' 已被禁言 60 秒", "Main", "Dummy", Seconds), EHudMediaIslandMuteMessage::NONE);
	// 初始聊天延迟提示不算禁言，与英文侧一致。
	EXPECT_EQ(QmHudParseSpamProtectionMute("本服务器有初始聊天延迟，你将在 17 秒后可以发言。", "Main", "Dummy", Seconds), EHudMediaIslandMuteMessage::NONE);
	// 前缀命中但秒数缺失，必须返回 NONE 而不是落进刷屏禁言分支。
	EXPECT_EQ(QmHudParseSpamProtectionMute("你在接下来的 秒内不能发言。", "Main", "Dummy", Seconds), EHudMediaIslandMuteMessage::NONE);
	EXPECT_EQ(Seconds, 0);
}
