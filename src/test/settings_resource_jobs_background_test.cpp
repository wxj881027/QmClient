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

TEST(SettingsResourceJobsBackground, TeeSkinBackgroundRequestBudgetOnlyRunsOnIdleFrames)
{
	const SSettingsResourceFrameContext Idle = {false, false, 0};
	const SSettingsResourceFrameContext Scrolling = {true, false, 0};
	const SSettingsResourceFrameContext Recovering = {false, false, 2};

	EXPECT_GT(SettingsSkinBackgroundRequestFrameBudget(Idle, true), 0);
	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(Scrolling, true), 0);
	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(Recovering, true), 0);
	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(Idle, false), 0);
}

TEST(SettingsResourceJobsBackground, TeeSkinBackgroundDrainRaisesIdleThroughputBudgets)
{
	SSettingsResourceFrameContext IdleSettled = {false, false, 0};
	IdleSettled.m_HighPrioritySettled = true;
	SSettingsResourceFrameContext RecoveringSettled = {false, false, 2};
	RecoveringSettled.m_HighPrioritySettled = true;
	SSettingsResourceFrameContext ScrollingSettled = {true, false, 0};
	ScrollingSettled.m_HighPrioritySettled = true;

	EXPECT_TRUE(SettingsSkinBackgroundDrainActive(IdleSettled, true));
	EXPECT_FALSE(SettingsSkinBackgroundDrainActive(RecoveringSettled, true));
	EXPECT_FALSE(SettingsSkinBackgroundDrainActive(ScrollingSettled, true));
	EXPECT_FALSE(SettingsSkinBackgroundDrainActive(IdleSettled, false));

	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(IdleSettled, true), 8);
	EXPECT_EQ(SettingsSkinSourceLoadNormalWindow(IdleSettled, true, 64), 256);
	EXPECT_EQ(SettingsSkinSourceLoadVisibleWindow(IdleSettled, true, 64), 256);
	EXPECT_EQ(SettingsSkinSourceCountFuseLimit(IdleSettled, true, 64), 128);
}

TEST(SettingsResourceJobsBackground, TeeBackgroundRequestsStayBlockedThroughScrollCooldownAndRecovery)
{
	int CooldownFrames = 0;
	int RecoveryFrames = 0;

	CooldownFrames = SettingsScrollInteractionCooldown(true, CooldownFrames, 3);
	RecoveryFrames = SettingsScrollInteractionRecovery(true, 0, CooldownFrames, RecoveryFrames, 2);
	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(SettingsBuildFrameContext(true || CooldownFrames > 0, false, RecoveryFrames), true), 0);

	int PreviousCooldownFrames = CooldownFrames;
	CooldownFrames = SettingsScrollInteractionCooldown(false, CooldownFrames, 3);
	RecoveryFrames = SettingsScrollInteractionRecovery(false, PreviousCooldownFrames, CooldownFrames, RecoveryFrames, 2);
	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(SettingsBuildFrameContext(false || CooldownFrames > 0, false, RecoveryFrames), true), 0);

	PreviousCooldownFrames = CooldownFrames;
	CooldownFrames = SettingsScrollInteractionCooldown(false, CooldownFrames, 3);
	RecoveryFrames = SettingsScrollInteractionRecovery(false, PreviousCooldownFrames, CooldownFrames, RecoveryFrames, 2);
	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(SettingsBuildFrameContext(false || CooldownFrames > 0, false, RecoveryFrames), true), 0);

	PreviousCooldownFrames = CooldownFrames;
	CooldownFrames = SettingsScrollInteractionCooldown(false, CooldownFrames, 3);
	RecoveryFrames = SettingsScrollInteractionRecovery(false, PreviousCooldownFrames, CooldownFrames, RecoveryFrames, 2);
	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(SettingsBuildFrameContext(false || CooldownFrames > 0, false, RecoveryFrames), true), 0);

	PreviousCooldownFrames = CooldownFrames;
	CooldownFrames = SettingsScrollInteractionCooldown(false, CooldownFrames, 3);
	RecoveryFrames = SettingsScrollInteractionRecovery(false, PreviousCooldownFrames, CooldownFrames, RecoveryFrames, 2);
	EXPECT_EQ(SettingsSkinBackgroundRequestFrameBudget(SettingsBuildFrameContext(false || CooldownFrames > 0, false, RecoveryFrames), true), 0);

	PreviousCooldownFrames = CooldownFrames;
	CooldownFrames = SettingsScrollInteractionCooldown(false, CooldownFrames, 3);
	RecoveryFrames = SettingsScrollInteractionRecovery(false, PreviousCooldownFrames, CooldownFrames, RecoveryFrames, 2);
	EXPECT_GT(SettingsSkinBackgroundRequestFrameBudget(SettingsBuildFrameContext(false || CooldownFrames > 0, false, RecoveryFrames), true), 0);
}

TEST(SettingsResourceJobsBackground, TeeBackgroundRequestBudgetTracksRealInflightHeadroom)
{
	SSettingsSkinBackgroundRequestBudgetInput Input;
	Input.m_DefaultBudget = 24;
	Input.m_Pending = 40;
	Input.m_Loading = 60;
	Input.m_BackgroundRequested = 0;
	Input.m_CountFuseLimit = 128;
	Input.m_VisibleReserve = 8;
	Input.m_RecentLoadedDelta = 2;
	Input.m_RecentAdmittedDelta = 2;
	Input.m_DrainActive = true;

	const auto Decision = SettingsSkinBackgroundRequestBudgetDecision(Input);
	EXPECT_EQ(Decision.m_RealInflight, 100);
	EXPECT_EQ(Decision.m_RequestBudget, 20);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinBackgroundRequestBlockReason::NONE);
}

TEST(SettingsResourceJobsBackground, TeeBackgroundRequestBudgetPausesWhenHeadroomIsReservedForVisible)
{
	SSettingsSkinBackgroundRequestBudgetInput Input;
	Input.m_DefaultBudget = 24;
	Input.m_Pending = 64;
	Input.m_Loading = 56;
	Input.m_BackgroundRequested = 0;
	Input.m_CountFuseLimit = 128;
	Input.m_VisibleReserve = 8;
	Input.m_RecentLoadedDelta = 1;
	Input.m_RecentAdmittedDelta = 1;
	Input.m_DrainActive = true;

	const auto Decision = SettingsSkinBackgroundRequestBudgetDecision(Input);
	EXPECT_EQ(Decision.m_RealInflight, 120);
	EXPECT_EQ(Decision.m_RequestBudget, 0);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinBackgroundRequestBlockReason::VISIBLE_RESERVE);
}

TEST(SettingsResourceJobsBackground, TeeBackgroundRequestBudgetSlowsStalledProducerWithLargeBacklog)
{
	SSettingsSkinBackgroundRequestBudgetInput Input;
	Input.m_DefaultBudget = 24;
	Input.m_Pending = 4;
	Input.m_Loading = 4;
	Input.m_BackgroundRequested = 160;
	Input.m_CountFuseLimit = 64;
	Input.m_VisibleReserve = 8;
	Input.m_RecentLoadedDelta = 0;
	Input.m_RecentAdmittedDelta = 0;
	Input.m_DrainActive = true;

	const auto Decision = SettingsSkinBackgroundRequestBudgetDecision(Input);
	EXPECT_EQ(Decision.m_RequestBudget, 0);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinBackgroundRequestBlockReason::STALL_BACKPRESSURE);
}

TEST(SettingsResourceJobsBackground, TeeBackgroundRequestBudgetAllowsAdmittedProgressBelowHardCap)
{
	SSettingsSkinBackgroundRequestBudgetInput Input;
	Input.m_DefaultBudget = 24;
	Input.m_Pending = 4;
	Input.m_Loading = 4;
	Input.m_BackgroundRequested = 160;
	Input.m_CountFuseLimit = 64;
	Input.m_VisibleReserve = 8;
	Input.m_RecentLoadedDelta = 0;
	Input.m_RecentAdmittedDelta = 3;
	Input.m_DrainActive = true;

	const auto Decision = SettingsSkinBackgroundRequestBudgetDecision(Input);
	EXPECT_EQ(Decision.m_RequestBudget, 24);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinBackgroundRequestBlockReason::NONE);
}

TEST(SettingsResourceJobsBackground, TeeBackgroundRequestBudgetCapsHealthyBacklogBeforeQueueInflates)
{
	SSettingsSkinBackgroundRequestBudgetInput Input;
	Input.m_DefaultBudget = 24;
	Input.m_Pending = 8;
	Input.m_Loading = 8;
	Input.m_BackgroundRequested = 256;
	Input.m_CountFuseLimit = 128;
	Input.m_VisibleReserve = 0;
	Input.m_RecentLoadedDelta = 4;
	Input.m_RecentAdmittedDelta = 4;
	Input.m_DrainActive = true;

	const auto Decision = SettingsSkinBackgroundRequestBudgetDecision(Input);
	EXPECT_EQ(Decision.m_RequestBudget, 0);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinBackgroundRequestBlockReason::STALL_BACKPRESSURE);
}

TEST(SettingsResourceJobsBackground, TeeBackgroundWindowGrowsSlowlyWhenDrainIsHealthy)
{
	SSettingsSkinBackgroundWindowInput Input;
	Input.m_CurrentLimit = 64;
	Input.m_MinLimit = 32;
	Input.m_MaxLimit = 120;
	Input.m_HealthyFrames = 3;
	Input.m_HealthyFramesToGrow = 4;
	Input.m_DrainActive = true;
	Input.m_FrameStable = true;
	Input.m_VisibleWaiting = false;
	Input.m_GpuBudgetExhausted = false;
	Input.m_FinalizeBudgetExhausted = false;
	Input.m_DecodeJobsSaturated = false;
	Input.m_LoadedProgress = true;
	Input.m_ConsumerStalled = false;

	const auto Update = SettingsSkinBackgroundWindowUpdate(Input);
	EXPECT_EQ(Update.m_NextLimit, 65);
	EXPECT_EQ(Update.m_NextHealthyFrames, 0);
	EXPECT_EQ(Update.m_Decision, ESettingsSkinBackgroundWindowDecision::INCREASE);
}

TEST(SettingsResourceJobsBackground, TeeBackgroundWindowShrinksFastWhenVisibleNeedsHeadroom)
{
	SSettingsSkinBackgroundWindowInput Input;
	Input.m_CurrentLimit = 96;
	Input.m_MinLimit = 32;
	Input.m_MaxLimit = 120;
	Input.m_HealthyFrames = 2;
	Input.m_HealthyFramesToGrow = 4;
	Input.m_DrainActive = true;
	Input.m_FrameStable = true;
	Input.m_VisibleWaiting = true;
	Input.m_GpuBudgetExhausted = false;
	Input.m_FinalizeBudgetExhausted = false;
	Input.m_DecodeJobsSaturated = false;
	Input.m_LoadedProgress = false;
	Input.m_ConsumerStalled = false;

	const auto Update = SettingsSkinBackgroundWindowUpdate(Input);
	EXPECT_EQ(Update.m_NextLimit, 48);
	EXPECT_EQ(Update.m_NextHealthyFrames, 0);
	EXPECT_EQ(Update.m_Decision, ESettingsSkinBackgroundWindowDecision::DECREASE);
}

TEST(SettingsResourceJobsBackground, TeeBackgroundWindowShrinksWhenAdmittedWorkStopsCompleting)
{
	SSettingsSkinBackgroundWindowInput Input;
	Input.m_CurrentLimit = 80;
	Input.m_MinLimit = 32;
	Input.m_MaxLimit = 120;
	Input.m_HealthyFrames = 1;
	Input.m_HealthyFramesToGrow = 4;
	Input.m_DrainActive = true;
	Input.m_FrameStable = true;
	Input.m_VisibleWaiting = false;
	Input.m_GpuBudgetExhausted = false;
	Input.m_FinalizeBudgetExhausted = false;
	Input.m_DecodeJobsSaturated = false;
	Input.m_LoadedProgress = false;
	Input.m_ConsumerStalled = true;

	const auto Update = SettingsSkinBackgroundWindowUpdate(Input);
	EXPECT_EQ(Update.m_NextLimit, 40);
	EXPECT_EQ(Update.m_Decision, ESettingsSkinBackgroundWindowDecision::DECREASE);
}

TEST(SettingsResourceJobsBackground, TeeBackgroundWindowShrinksWhenDecodeJobsSaturate)
{
	SSettingsSkinBackgroundWindowInput Input;
	Input.m_CurrentLimit = 72;
	Input.m_MinLimit = 32;
	Input.m_MaxLimit = 120;
	Input.m_HealthyFrames = 2;
	Input.m_HealthyFramesToGrow = 4;
	Input.m_DrainActive = true;
	Input.m_FrameStable = true;
	Input.m_VisibleWaiting = false;
	Input.m_GpuBudgetExhausted = false;
	Input.m_FinalizeBudgetExhausted = false;
	Input.m_DecodeJobsSaturated = true;
	Input.m_LoadedProgress = false;
	Input.m_ConsumerStalled = false;

	const auto Update = SettingsSkinBackgroundWindowUpdate(Input);
	EXPECT_EQ(Update.m_NextLimit, 36);
	EXPECT_EQ(Update.m_NextHealthyFrames, 0);
	EXPECT_EQ(Update.m_Decision, ESettingsSkinBackgroundWindowDecision::DECREASE);
}

TEST(SettingsResourceJobsBackground, TeeOffscreenLifecycleWaitsForNonemptyValidSettledList)
{
	bool DrainSessionActive = true;
	const auto Advance = [&](int TotalEntries, int ValidEntries, int SettledEntries, bool PerfDebugEnabled) {
		const auto Decision = SettingsTeeOffscreenLifecycleDecision({
			TotalEntries,
			ValidEntries,
			SettledEntries,
			DrainSessionActive,
			PerfDebugEnabled,
		});
		if(Decision.m_CompleteDrainSession)
			DrainSessionActive = false;
		return Decision;
	};

	const auto Empty = Advance(0, 0, 0, false);
	EXPECT_FALSE(Empty.m_FullListReady);
	EXPECT_FALSE(Empty.m_CompleteDrainSession);
	EXPECT_TRUE(DrainSessionActive);

	const auto MissingContainer = Advance(3, 2, 2, false);
	EXPECT_FALSE(MissingContainer.m_FullListReady);
	EXPECT_FALSE(MissingContainer.m_CompleteDrainSession);
	EXPECT_TRUE(DrainSessionActive);

	const auto Loading = Advance(3, 3, 2, false);
	EXPECT_FALSE(Loading.m_FullListReady);
	EXPECT_FALSE(Loading.m_CompleteDrainSession);
	EXPECT_TRUE(DrainSessionActive);

	const auto Complete = Advance(3, 3, 3, false);
	EXPECT_TRUE(Complete.m_FullListReady);
	EXPECT_TRUE(Complete.m_CompleteDrainSession);
	EXPECT_FALSE(Complete.m_LogCompletion);
	EXPECT_FALSE(DrainSessionActive);
}

TEST(SettingsResourceJobsBackground, TeeOffscreenLifecycleLogsCompletionOnlyForActivePerfSession)
{
	const auto Inactive = SettingsTeeOffscreenLifecycleDecision({3, 3, 3, false, true});
	EXPECT_TRUE(Inactive.m_FullListReady);
	EXPECT_FALSE(Inactive.m_CompleteDrainSession);
	EXPECT_FALSE(Inactive.m_LogCompletion);

	const auto Active = SettingsTeeOffscreenLifecycleDecision({3, 3, 3, true, true});
	EXPECT_TRUE(Active.m_FullListReady);
	EXPECT_TRUE(Active.m_CompleteDrainSession);
	EXPECT_TRUE(Active.m_LogCompletion);
}

TEST(SettingsResourceJobsBackground, SourceBytesEstimateExceedsZeroForLoadedSkin)
{
	EXPECT_GT(SettingsSkinSourceBytesEstimate(256, 128, 2), 0u);
}

TEST(SettingsResourceJobsBackground, BytesBudgetCanTriggerReclaimBeforeCountFuse)
{
	EXPECT_TRUE(SettingsSkinResidencyShouldReclaim(true, false));
}

TEST(SettingsResourceJobsBackground, CountFuseStillAppliesWhenBytesBudgetIsWithinLimit)
{
	EXPECT_TRUE(SettingsSkinResidencyShouldReclaim(false, true));
}

TEST(SettingsResourceJobsBackground, WorkshopInstalledAssetCanUseWorkshopCatalogAndLocalBytes)
{
	EXPECT_STREQ(SettingsWorkshopCatalogSourceName(ESettingsWorkshopCatalogSource::WORKSHOP_CACHE), "workshop-cache");
	EXPECT_STREQ(SettingsWorkshopBytesSourceName(ESettingsWorkshopBytesSource::LOCAL_INSTALL), "local-install");
}

TEST(SettingsResourceJobsBackground, CountryFlagPlanHandlesEmptyInput)
{
	const std::vector<int> vPlan = BuildSettingsCountryFlagWarmupPlan({});
	EXPECT_TRUE(vPlan.empty());
}
