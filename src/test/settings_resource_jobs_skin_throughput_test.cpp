// 设置资源任务测试：按资源任务生命周期独立组织。
#include <game/client/components/assets_preview_scale.h>
#include <game/client/components/menus.h>
#include <game/client/components/settings_resource_jobs.h>
#include <game/client/components/settings_warmup.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

TEST(SettingsResourceJobsSkinThroughput, SkinListBackgroundWarmupWaitsForIdleVisibleBacklog)
{
	EXPECT_TRUE(SettingsSkinBackgroundWarmupShouldRun(true, false, false));
	EXPECT_FALSE(SettingsSkinBackgroundWarmupShouldRun(true, true, false));
	EXPECT_FALSE(SettingsSkinBackgroundWarmupShouldRun(true, false, true));
	EXPECT_FALSE(SettingsSkinBackgroundWarmupShouldRun(false, false, false));
	EXPECT_FALSE(SettingsSkinBackgroundWarmupWindowFull(0, 20, 10, 64));
	EXPECT_TRUE(SettingsSkinBackgroundWarmupWindowFull(0, 40, 24, 64));
}

TEST(SettingsResourceJobsSkinThroughput, TeeSkinSourceLoadWindowCapsActiveDecodeConcurrency)
{
	const SSettingsResourceFrameContext Idle = SettingsBuildFrameContext(false, false, 0);
	const SSettingsResourceFrameContext RecoveryStart = SettingsBuildFrameContext(false, false, 2);
	const SSettingsResourceFrameContext RecoveryEnd = SettingsBuildFrameContext(false, false, 1);
	const SSettingsResourceFrameContext Scroll = SettingsBuildFrameContext(true, false, 0);
	EXPECT_EQ(SettingsSkinSourceLoadNormalWindow(Idle, true, 1600), 256);
	EXPECT_EQ(SettingsSkinSourceLoadVisibleWindow(Idle, true, 1600), 256);
	EXPECT_LT(SettingsSkinSourceLoadNormalWindow(Scroll, true, 1600), SettingsSkinSourceLoadNormalWindow(RecoveryStart, true, 1600));
	EXPECT_LT(SettingsSkinSourceLoadNormalWindow(RecoveryStart, true, 1600), SettingsSkinSourceLoadNormalWindow(RecoveryEnd, true, 1600));
	EXPECT_LT(SettingsSkinSourceLoadNormalWindow(RecoveryEnd, true, 1600), SettingsSkinSourceLoadNormalWindow(Idle, true, 1600));
	EXPECT_LT(SettingsSkinSourceLoadVisibleWindow(Scroll, true, 1600), SettingsSkinSourceLoadVisibleWindow(RecoveryStart, true, 1600));
	EXPECT_LT(SettingsSkinSourceLoadVisibleWindow(RecoveryStart, true, 1600), SettingsSkinSourceLoadVisibleWindow(RecoveryEnd, true, 1600));
	EXPECT_LT(SettingsSkinSourceLoadVisibleWindow(RecoveryEnd, true, 1600), SettingsSkinSourceLoadVisibleWindow(Idle, true, 1600));
	EXPECT_EQ(SettingsSkinSourceLoadNormalWindow(Scroll, true, 1600), 48);
	EXPECT_EQ(SettingsSkinSourceLoadVisibleWindow(Scroll, true, 1600), 128);
	EXPECT_EQ(SettingsSkinSourceLoadVisibleWindow(Idle, true, 32), 32);
}

TEST(SettingsResourceJobsSkinThroughput, VisibleSkinFinalizeDefersBackgroundSweepsAfterPriorityWork)
{
	EXPECT_TRUE(SettingsSkinFinalizeShouldDeferBackgroundSweep(true, 1, 2));
	EXPECT_FALSE(SettingsSkinFinalizeShouldDeferBackgroundSweep(false, 1, 2));
	EXPECT_FALSE(SettingsSkinFinalizeShouldDeferBackgroundSweep(true, 0, 2));
	EXPECT_FALSE(SettingsSkinFinalizeShouldDeferBackgroundSweep(true, 2, 2));
}

TEST(SettingsResourceJobsSkinThroughput, VisibleSkinFinalizeAllowsBackgroundSweepOnNextFrame)
{
	EXPECT_TRUE(SettingsSkinFinalizeShouldDeferBackgroundSweep(true, 1, 12));
	EXPECT_FALSE(SettingsSkinFinalizeShouldDeferBackgroundSweep(false, 0, 12));
}

TEST(SettingsResourceJobsSkinThroughput, TeeSkinFinalizeBudgetDefersDuringScrollAndRecovery)
{
	const SSettingsResourceFrameContext Idle = {false, false, 0};
	const SSettingsResourceFrameContext Scrolling = {true, false, 0};
	const SSettingsResourceFrameContext RecoveringStart = SettingsBuildFrameContext(false, false, 2);
	const SSettingsResourceFrameContext RecoveringEnd = SettingsBuildFrameContext(false, false, 1);

	EXPECT_EQ(SettingsSkinFinalizeMaxPerFrame(true), 64);
	EXPECT_EQ(SettingsSkinGpuUploadUnits(true), 8);
	EXPECT_EQ(SettingsSkinFinalizeFrameBudget(Idle, true), SettingsSkinFinalizeMaxPerFrame(true));
	EXPECT_EQ(SettingsSkinGpuUploadFrameUnits(Idle, true), SettingsSkinGpuUploadUnits(true));
	EXPECT_EQ(SettingsSkinFinalizeFrameBudget(Scrolling, true), 16);
	EXPECT_EQ(SettingsSkinGpuUploadFrameUnits(Scrolling, true), 4);
	EXPECT_LT(SettingsSkinFinalizeFrameBudget(Scrolling, true), SettingsSkinFinalizeFrameBudget(RecoveringStart, true));
	EXPECT_LT(SettingsSkinFinalizeFrameBudget(RecoveringStart, true), SettingsSkinFinalizeFrameBudget(RecoveringEnd, true));
	EXPECT_LT(SettingsSkinFinalizeFrameBudget(RecoveringEnd, true), SettingsSkinFinalizeFrameBudget(Idle, true));
	EXPECT_LT(SettingsSkinGpuUploadFrameUnits(Scrolling, true), SettingsSkinGpuUploadFrameUnits(RecoveringStart, true));
	EXPECT_LT(SettingsSkinGpuUploadFrameUnits(RecoveringStart, true), SettingsSkinGpuUploadFrameUnits(RecoveringEnd, true));
	EXPECT_LT(SettingsSkinGpuUploadFrameUnits(RecoveringEnd, true), SettingsSkinGpuUploadFrameUnits(Idle, true));
}

TEST(SettingsResourceJobsSkinThroughput, TeeSkinFinalizeIdleDrainUsesBoundedMergeBudget)
{
	SSettingsSkinThroughputControllerState State;
	const auto Settled = SettingsSkinThroughputControllerStep({
									  {false, false, 0, true},
									  true,
									  6.5f,
									  6.0f,
									  512,
									  28,
									  28,
									  0,
									  0,
									  0,
									  0,
									  0,
									  20,
									  40,
									  20,
									  12,
									  12,
									  0,
									  0,
									  288,
									  false,
									  "none",
									  "none",
								  },
		State);

	EXPECT_EQ(Settled.m_Mode, ESettingsSkinThroughputControllerMode::IDLE_DRAIN);
	EXPECT_TRUE(Settled.m_BackgroundDrainActive);
	EXPECT_EQ(Settled.m_FinalizeBudgetLimit, 32);

	const SSettingsResourceFrameContext Scrolling = SettingsBuildFrameContext(true, false, 0);
	EXPECT_EQ(SettingsSkinFinalizeFrameBudget(Scrolling, true), 16);
	const SSettingsResourceFrameContext RecoveringStart = SettingsBuildFrameContext(false, false, 2);
	const SSettingsResourceFrameContext RecoveringEnd = SettingsBuildFrameContext(false, false, 1);
	EXPECT_LT(SettingsSkinFinalizeFrameBudget(Scrolling, true), SettingsSkinFinalizeFrameBudget(RecoveringStart, true));
	EXPECT_LT(SettingsSkinFinalizeFrameBudget(RecoveringStart, true), SettingsSkinFinalizeFrameBudget(RecoveringEnd, true));
}

TEST(SettingsResourceJobsSkinThroughput, ActiveTeeSkinFrameBudgetAllowsEightSourceUploadsPerFrame)
{
	SSettingsWarmupFrameBudget Budget;
	SettingsApplyActiveTeeSkinFrameBudget(Budget, true);

	SSettingsResourceMergeBudget UploadBudget;
	UploadBudget.m_MaxGpuUploads = 8;
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_FALSE(SettingsResourceConsumeGpuUpload(UploadBudget, &Budget));
	EXPECT_EQ(UploadBudget.m_StopReason, ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET);
}

TEST(SettingsResourceJobsSkinThroughput, TeeSkinGpuUploadLimiterBudgetTracksFrameContext)
{
	const SSettingsResourceFrameContext IdleVisible = SettingsBuildFrameContext(false, false, 0);
	const SSettingsResourceFrameContext RecoveryStart = SettingsBuildFrameContext(false, false, 2);
	const SSettingsResourceFrameContext RecoveryEnd = SettingsBuildFrameContext(false, false, 1);
	const SSettingsResourceFrameContext Scroll = SettingsBuildFrameContext(true, false, 0);
	SSettingsResourceFrameContext IdleDrain = SettingsBuildFrameContext(false, false, 0);
	IdleDrain.m_HighPrioritySettled = true;

	EXPECT_EQ(SettingsSkinGpuUploadLimiterUnits(IdleVisible, true), 192);
	EXPECT_EQ(SettingsSkinGpuUploadLimiterUnits(Scroll, true), 96);
	EXPECT_LT(SettingsSkinGpuUploadLimiterUnits(Scroll, true), SettingsSkinGpuUploadLimiterUnits(RecoveryStart, true));
	EXPECT_LT(SettingsSkinGpuUploadLimiterUnits(RecoveryStart, true), SettingsSkinGpuUploadLimiterUnits(RecoveryEnd, true));
	EXPECT_LT(SettingsSkinGpuUploadLimiterUnits(RecoveryEnd, true), SettingsSkinGpuUploadLimiterUnits(IdleVisible, true));
	EXPECT_EQ(SettingsSkinGpuUploadLimiterUnits(IdleDrain, true), 288);
}

TEST(SettingsResourceJobsSkinThroughput, SharedHeavyBudgetTransitionsContinuouslyAfterScroll)
{
	const SSettingsResourceFrameContext Scroll = SettingsBuildFrameContext(true, false, 0);
	const SSettingsResourceFrameContext RecoveryStart = SettingsBuildFrameContext(false, false, 2);
	const SSettingsResourceFrameContext RecoveryEnd = SettingsBuildFrameContext(false, false, 1);
	const SSettingsResourceFrameContext Idle = SettingsBuildFrameContext(false, false, 0);

	EXPECT_EQ(SettingsResourceSharedHeavyBudget(Scroll, 4, 1), 0);
	EXPECT_LT(SettingsResourceSharedHeavyBudget(Scroll, 4, 1), SettingsResourceSharedHeavyBudget(RecoveryStart, 4, 1));
	EXPECT_LE(SettingsResourceSharedHeavyBudget(RecoveryStart, 4, 1), SettingsResourceSharedHeavyBudget(RecoveryEnd, 4, 1));
	EXPECT_EQ(SettingsResourceSharedHeavyBudget(RecoveryEnd, 4, 1), 1);
	EXPECT_LT(SettingsResourceSharedHeavyBudget(RecoveryEnd, 4, 1), SettingsResourceSharedHeavyBudget(Idle, 4, 1));
}

TEST(SettingsResourceJobsSkinThroughput, ThroughputControllerKeepsVisibleBacklogOutOfIdleDrain)
{
	SSettingsSkinThroughputControllerState State;
	const auto VisibleBacklog = SettingsSkinThroughputControllerStep({
										 {false, false, 0, true},
										 true,
										 6.5f,
										 6.0f,
										 512,
										 28,
										 19,
										 9,
										 0,
										 9,
										 0,
										 3,
										 21,
										 29,
										 24,
										 0,
										 0,
										 0,
										 0,
										 192,
										 false,
										 "none",
										 "drain_inactive",
									 },
		State);
	EXPECT_EQ(VisibleBacklog.m_Mode, ESettingsSkinThroughputControllerMode::IDLE_VISIBLE);
	EXPECT_FALSE(VisibleBacklog.m_BackgroundDrainActive);
	EXPECT_EQ(VisibleBacklog.m_BackgroundRequestBudget, 6);

	const auto Settled = SettingsSkinThroughputControllerStep({
									  {false, false, 0, true},
									  true,
									  6.5f,
									  6.0f,
									  512,
									  28,
									  28,
									  0,
									  0,
									  0,
									  0,
									  0,
									  20,
									  40,
									  20,
									  12,
									  12,
									  0,
									  0,
									  288,
									  false,
									  "none",
									  "none",
								  },
		State);
	EXPECT_EQ(Settled.m_Mode, ESettingsSkinThroughputControllerMode::IDLE_DRAIN);
	EXPECT_TRUE(Settled.m_BackgroundDrainActive);
	EXPECT_EQ(Settled.m_BackgroundRequestBudget, 8);
}

TEST(SettingsResourceJobsSkinThroughput, ThroughputControllerRelaxesReserveAndExpandsWindowsWhenAdmissionUnderfed)
{
	SSettingsSkinThroughputControllerState State;
	State.m_Initialized = true;
	State.m_Mode = ESettingsSkinThroughputControllerMode::IDLE_VISIBLE;
	State.m_GpuUploadLimitUnits = 192;
	State.m_GpuUploadFrameBudget = 8;
	State.m_FinalizeBudgetLimit = 48;
	State.m_NormalLoadingWindow = 128;
	State.m_VisibleLoadingWindow = 192;
	State.m_VisibleReserve = 2;

	const auto Output = SettingsSkinThroughputControllerStep({
									 {false, 0, false},
									 true,
									 6.9f,
									 6.7f,
									 512,
									 28,
									 19,
									 9,
									 0,
									 9,
									 0,
									 3,
									 21,
									 29,
									 30,
									 0,
									 0,
									 0,
									 0,
									 192,
									 false,
									 "visible_reserve",
									 "drain_inactive",
								 },
		State);

	EXPECT_EQ(Output.m_Reason, ESettingsSkinThroughputControllerReason::ADMISSION);
	EXPECT_EQ(Output.m_VisibleReserve, 0);
	EXPECT_GT(Output.m_NormalLoadingWindow, 128);
	EXPECT_GT(Output.m_VisibleLoadingWindow, 192);
	EXPECT_TRUE(Output.m_AdmissionUnderfed);
	EXPECT_EQ(Output.m_UnderfedStreak, 1);
}

TEST(SettingsResourceJobsSkinThroughput, ThroughputControllerReducesOnlyUploadBudgetOnGpuPressure)
{
	SSettingsSkinThroughputControllerState State;
	State.m_Initialized = true;
	State.m_Mode = ESettingsSkinThroughputControllerMode::IDLE_VISIBLE;
	State.m_GpuUploadLimitUnits = 240;
	State.m_GpuUploadFrameBudget = 10;
	State.m_FinalizeBudgetLimit = 64;
	State.m_NormalLoadingWindow = 192;
	State.m_VisibleLoadingWindow = 224;
	State.m_VisibleReserve = 2;

	const auto Output = SettingsSkinThroughputControllerStep({
									 {false, 0, false},
									 true,
									 7.0f,
									 6.8f,
									 512,
									 28,
									 20,
									 8,
									 0,
									 8,
									 0,
									 2,
									 18,
									 30,
									 20,
									 0,
									 0,
									 0,
									 0,
									 0,
									 false,
									 "gpu_upload_budget",
									 "none",
								 },
		State);

	EXPECT_EQ(Output.m_Reason, ESettingsSkinThroughputControllerReason::GPU);
	EXPECT_LT(Output.m_GpuUploadLimitUnits, 240);
	EXPECT_EQ(Output.m_FinalizeBudgetLimit, 64);
	EXPECT_EQ(Output.m_NormalLoadingWindow, 192);
}

TEST(SettingsResourceJobsSkinThroughput, ThroughputControllerReducesOnlyFinalizeBudgetOnFinalizePressure)
{
	SSettingsSkinThroughputControllerState State;
	State.m_Initialized = true;
	State.m_Mode = ESettingsSkinThroughputControllerMode::IDLE_VISIBLE;
	State.m_GpuUploadLimitUnits = 240;
	State.m_GpuUploadFrameBudget = 10;
	State.m_FinalizeBudgetLimit = 64;
	State.m_NormalLoadingWindow = 192;
	State.m_VisibleLoadingWindow = 224;
	State.m_VisibleReserve = 2;

	const auto Output = SettingsSkinThroughputControllerStep({
									 {false, 0, false},
									 true,
									 7.0f,
									 6.8f,
									 512,
									 28,
									 20,
									 8,
									 0,
									 8,
									 0,
									 2,
									 18,
									 30,
									 20,
									 0,
									 0,
									 0,
									 0,
									 96,
									 false,
									 "max_per_frame",
									 "none",
								 },
		State);

	EXPECT_EQ(Output.m_Reason, ESettingsSkinThroughputControllerReason::FINALIZE);
	EXPECT_EQ(Output.m_GpuUploadLimitUnits, 240);
	EXPECT_LT(Output.m_FinalizeBudgetLimit, 64);
	EXPECT_EQ(Output.m_NormalLoadingWindow, 192);
}

TEST(SettingsResourceJobsSkinThroughput, NonTeeSkinFinalizeBudgetKeepsLegacyLimits)
{
	const SSettingsResourceFrameContext Scrolling = {true, false, 2};
	EXPECT_EQ(SettingsSkinFinalizeFrameBudget(Scrolling, false), SettingsSkinFinalizeMaxPerFrame(false));
	EXPECT_EQ(SettingsSkinGpuUploadFrameUnits(Scrolling, false), SettingsSkinGpuUploadUnits(false));
}

TEST(SettingsResourceJobsSkinThroughput, ImmediateScrollInputKeepsReducedTeeThroughputBeforePersistentStateCatchesUp)
{
	const SSettingsResourceFrameContext Idle = SettingsBuildFrameContext(false, false, 0);
	const SSettingsResourceFrameContext ImmediateScroll = SettingsBuildFrameContext(false, true, 0);
	const SSettingsResourceFrameContext PersistentScroll = SettingsBuildFrameContext(true, false, 0);

	EXPECT_FALSE(Idle.m_ScrollActive);
	EXPECT_TRUE(ImmediateScroll.m_ScrollActive);
	EXPECT_TRUE(PersistentScroll.m_ScrollActive);
	EXPECT_GT(SettingsSkinFinalizeFrameBudget(ImmediateScroll, true), 0);
	EXPECT_GT(SettingsSkinGpuUploadFrameUnits(ImmediateScroll, true), 0);
	EXPECT_LT(SettingsSkinFinalizeFrameBudget(ImmediateScroll, true), SettingsSkinFinalizeFrameBudget(Idle, true));
	EXPECT_LT(SettingsSkinGpuUploadFrameUnits(ImmediateScroll, true), SettingsSkinGpuUploadFrameUnits(Idle, true));
	EXPECT_EQ(SettingsSkinFinalizeFrameBudget(ImmediateScroll, true), SettingsSkinFinalizeFrameBudget(PersistentScroll, true));
	EXPECT_EQ(SettingsSkinGpuUploadFrameUnits(ImmediateScroll, true), SettingsSkinGpuUploadFrameUnits(PersistentScroll, true));
}

TEST(SettingsResourceJobsSkinThroughput, JumpScrollUsesSameHeavyBudgetGateAsImmediateScroll)
{
	const SSettingsResourceFrameContext Idle = SettingsBuildFrameContext(false, false, false, 0);
	const SSettingsResourceFrameContext JumpScroll = SettingsBuildFrameContext(false, false, true, 0);
	const SSettingsResourceFrameContext ImmediateScroll = SettingsBuildFrameContext(false, true, false, 0);

	EXPECT_FALSE(Idle.m_ScrollActive);
	EXPECT_FALSE(Idle.m_JumpScrollActive);
	EXPECT_TRUE(JumpScroll.m_JumpScrollActive);
	EXPECT_FALSE(JumpScroll.m_ScrollActive);
	EXPECT_TRUE(ImmediateScroll.m_ScrollActive);
	EXPECT_FALSE(ImmediateScroll.m_JumpScrollActive);
	EXPECT_EQ(SettingsResourceSharedHeavyBudget(JumpScroll, 4, 1), 0);
	EXPECT_EQ(SettingsResourceFrameStageBudget(JumpScroll, ESettingsResourcePriority::BACKGROUND, 4, 1), 0);
	EXPECT_EQ(SettingsResourceFrameStageBudget(JumpScroll, ESettingsResourcePriority::VISIBLE, 4, 1), 1);
	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(JumpScroll, false, ESettingsResourcePriority::VISIBLE, 0, 2 * 1024 * 1024, 1 * 1024 * 1024));
}
