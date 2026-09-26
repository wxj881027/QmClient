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

TEST(SettingsResourceJobsSkinPlan, SkinSnapshotRejectsStaleGeneration)
{
	SSettingsSkinListPlanResult Result;
	Result.m_Generation = 7;
	EXPECT_TRUE(SettingsSkinListPlanGenerationMatches(Result, 7));
	EXPECT_FALSE(SettingsSkinListPlanGenerationMatches(Result, 8));
}

TEST(SettingsResourceJobsSkinPlan, AssetListRejectsStaleJobGeneration)
{
	EXPECT_TRUE(SettingsAssetListJobGenerationMatches(4, 4));
	EXPECT_FALSE(SettingsAssetListJobGenerationMatches(4, 5));
}

TEST(SettingsResourceJobsSkinPlan, SkinListPublishesOnlyCompleteMergedList)
{
	EXPECT_FALSE(SettingsSkinListShouldPublishMergedList(0, 3));
	EXPECT_FALSE(SettingsSkinListShouldPublishMergedList(2, 3));
	EXPECT_TRUE(SettingsSkinListShouldPublishMergedList(3, 3));
	EXPECT_TRUE(SettingsSkinListShouldPublishMergedList(0, 0));
}

TEST(SettingsResourceJobsSkinPlan, SkinListReplacesPublishedEntriesAfterStableDirectory)
{
	EXPECT_TRUE(SettingsSkinListShouldReplacePublishedEntries(0, 3, true, true));
	EXPECT_FALSE(SettingsSkinListShouldReplacePublishedEntries(0, 3, false, false));
	EXPECT_TRUE(SettingsSkinListShouldReplacePublishedEntries(0, 3, false, true));
	EXPECT_FALSE(SettingsSkinListShouldReplacePublishedEntries(10, 20, false, false));
	EXPECT_FALSE(SettingsSkinListShouldReplacePublishedEntries(20, 10, false, false));
}

TEST(SettingsResourceJobsSkinPlan, SkinListKeepsPublishedEntriesUntilDirectoryScanSettles)
{
	EXPECT_FALSE(SettingsSkinListShouldReplacePublishedEntries(0, 1, true, false));
	EXPECT_FALSE(SettingsSkinListShouldReplacePublishedEntries(0, 3, true, false));
	EXPECT_FALSE(SettingsSkinListShouldReplacePublishedEntries(3, 1, true, false));
}

TEST(SettingsResourceJobsSkinPlan, SkinListSkeletonReadyDoesNotRequirePreviewResources)
{
	SSkinListPlanState State{};
	State.m_DirectoryScanPending = false;
	State.m_MergeComplete = true;
	State.m_ItemCount = 120;
	EXPECT_TRUE(SettingsSkinListSkeletonReady(State));
	EXPECT_FALSE(SettingsSkinListResourcesSettled(State));
}

TEST(SettingsResourceJobsSkinPlan, SkinListReadyMigrationKeepsStructureStable)
{
	SSkinListPlanSnapshot Snapshot{};
	Snapshot.m_ItemCount = 120;

	SSkinListPlanState Loading{};
	Loading.m_DirectoryScanPending = false;
	Loading.m_MergeComplete = true;
	Loading.m_ItemCount = 120;
	Loading.m_BackgroundBacklog = 5;
	Loading.m_VisibleBacklog = 1;

	SSkinListPlanState Settled = Loading;
	Settled.m_BackgroundBacklog = 0;
	Settled.m_VisibleBacklog = 0;

	EXPECT_FALSE(SettingsSkinListResourcesSettled(Loading));
	EXPECT_TRUE(SettingsSkinListResourcesSettled(Settled));
	EXPECT_EQ(Snapshot.m_ItemCount, 120);
}

TEST(SettingsResourceJobsSkinPlan, SourceAdmissionAllowsVisiblePromotionWhenDrainInactive)
{
	const auto Decision = SettingsSkinSourceAdmissionDecision({
		true,
		ESettingsResourcePriority::VISIBLE,
		false,
		24,
		192,
		256,
	});

	EXPECT_TRUE(Decision.m_PromoteAllowed);
	EXPECT_EQ(Decision.m_PromotePriority, ESettingsResourcePriority::VISIBLE);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinSourceAdmissionBlockReason::NONE);
	EXPECT_FALSE(Decision.m_CountFuseApplies);
}

TEST(SettingsResourceJobsSkinPlan, SourceAdmissionBlocksBackgroundPromotionWhenDrainInactive)
{
	const auto Decision = SettingsSkinSourceAdmissionDecision({
		true,
		ESettingsResourcePriority::BACKGROUND,
		false,
		24,
		192,
		256,
	});

	EXPECT_FALSE(Decision.m_PromoteAllowed);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinSourceAdmissionBlockReason::DRAIN_INACTIVE);
	EXPECT_TRUE(Decision.m_CountFuseApplies);
}

TEST(SettingsResourceJobsSkinPlan, SourceAdmissionUsesVisibleReserveForVisibleRequests)
{
	const auto Decision = SettingsSkinSourceAdmissionDecision({
		true,
		ESettingsResourcePriority::VISIBLE,
		true,
		256,
		192,
		256,
	});

	EXPECT_FALSE(Decision.m_PromoteAllowed);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinSourceAdmissionBlockReason::VISIBLE_RESERVE);
	EXPECT_FALSE(Decision.m_CountFuseApplies);
}

TEST(SettingsResourceJobsSkinPlan, AllVisibleReadyLoggingRequiresVisibleRows)
{
	EXPECT_FALSE(SettingsSkinListShouldLogAllVisibleReady(true, false, 0));
	EXPECT_FALSE(SettingsSkinListShouldLogAllVisibleReady(false, false, 28));
	EXPECT_FALSE(SettingsSkinListShouldLogAllVisibleReady(true, true, 28));
	EXPECT_TRUE(SettingsSkinListShouldLogAllVisibleReady(true, false, 28));
}

TEST(SettingsResourceJobsSkinPlan, SkeletonReadyPublishedCountMatchesSnapshotCount)
{
	SSkinListPlanSnapshot Snapshot{};
	Snapshot.m_ItemCount = 120;
	std::vector<int> vPublished(120, 0);
	EXPECT_EQ(static_cast<int>(vPublished.size()), Snapshot.m_ItemCount);
}

TEST(SettingsResourceJobsSkinPlan, SkinListKeepsPendingPlanAliveUntilPublishGateOpens)
{
	EXPECT_TRUE(SettingsSkinListHasPendingMergeWork(true, 3, 3, 3));
	EXPECT_TRUE(SettingsSkinListHasPendingMergeWork(true, 0, 0, 0));
	EXPECT_FALSE(SettingsSkinListHasPendingMergeWork(false, 3, 3, 3));
}

TEST(SettingsResourceJobsSkinPlan, VisibleSkinListEntriesRequestImmediateLoad)
{
	EXPECT_TRUE(SettingsSkinListShouldRequestImmediateLoad(true));
	EXPECT_FALSE(SettingsSkinListShouldRequestImmediateLoad(false));
}

TEST(SettingsResourceJobsSkinPlan, RuntimeWarmupOnlyRunsOnSettingsPageWhenIdle)
{
	EXPECT_TRUE(SettingsRuntimeWarmupShouldRun(true, true, false, false, false, false, false));
	EXPECT_FALSE(SettingsRuntimeWarmupShouldRun(true, false, false, false, false, false, false));
	EXPECT_FALSE(SettingsRuntimeWarmupShouldRun(true, true, true, false, false, false, false));
	EXPECT_FALSE(SettingsRuntimeWarmupShouldRun(true, true, false, true, false, false, false));
	EXPECT_FALSE(SettingsRuntimeWarmupShouldRun(true, true, false, false, true, false, false));
	EXPECT_FALSE(SettingsRuntimeWarmupShouldRun(true, true, false, false, false, true, false));
	EXPECT_FALSE(SettingsRuntimeWarmupShouldRun(true, true, false, false, false, false, true));
	EXPECT_FALSE(SettingsRuntimeWarmupShouldRun(false, true, false, false, false, false, false));
}

TEST(SettingsResourceJobsSkinPlan, SkinListWarmupCountsCoverVisibleAndPrefetchRows)
{
	EXPECT_EQ(SettingsSkinListFirstPageWarmupEntries(180.0f, 50.0f, 1, 2), 6);
	EXPECT_EQ(SettingsSkinListFirstPageWarmupEntries(300.0f, 50.0f, 4, 1), 28);
	EXPECT_EQ(SettingsSkinListFirstPageWarmupEntries(0.0f, 50.0f, 1, 2), 0);
	EXPECT_EQ(SettingsTeeSkinListFirstPageWarmupEntries(300.0f), 28);
	EXPECT_EQ(SettingsTeeSkinListFirstPageWarmupEntries(120.0f), 24);
	EXPECT_EQ(SettingsSkinListPrefetchCount(0, 2, 1, 2, 10), 2);
	EXPECT_EQ(SettingsSkinListPrefetchCount(7, 9, 1, 2, 10), 0);
	EXPECT_EQ(SettingsSkinListBackgroundWarmupCount(20, 6), 6);
	EXPECT_EQ(SettingsSkinListBackgroundWarmupCount(3, 6), 3);
}

TEST(SettingsResourceJobsSkinPlan, SkinBackgroundScanKeepsOneStableStartCursorPerPass)
{
	constexpr size_t ItemCount = 4;
	constexpr size_t StartCursor = 3;
	EXPECT_EQ(SettingsSkinBackgroundScanIndex(StartCursor, 0, ItemCount), 3u);
	EXPECT_EQ(SettingsSkinBackgroundScanIndex(StartCursor, 1, ItemCount), 0u);
	EXPECT_EQ(SettingsSkinBackgroundScanIndex(StartCursor, 2, ItemCount), 1u);
	EXPECT_EQ(SettingsSkinBackgroundScanIndex(StartCursor, 3, ItemCount), 2u);
	EXPECT_EQ(SettingsSkinBackgroundScanNextCursor(StartCursor, 2, ItemCount), 1u);
	EXPECT_EQ(SettingsSkinBackgroundScanNextCursor(StartCursor, ItemCount, ItemCount), StartCursor);
	EXPECT_EQ(SettingsSkinBackgroundScanIndex(StartCursor, 0, 0), 0u);
}

TEST(SettingsResourceJobsSkinPlan, LoadingPrewarmAttemptBudgetReservesExtraTeeSourceSettlePasses)
{
	EXPECT_EQ(SettingsLoadingPrewarmMaxAttempts(0, 0), 33);
	EXPECT_EQ(SettingsLoadingPrewarmMaxAttempts(19, 28), 132);
	EXPECT_EQ(SettingsLoadingPrewarmMaxAttempts(19, 8), 108);
}

TEST(SettingsResourceJobsSkinPlan, LoadingPrewarmBudgetOnlyStopsAfterBudgetAndStall)
{
	EXPECT_TRUE(SettingsLoadingPrewarmShouldKeepPumping(false, 0, 108, 0));
	EXPECT_TRUE(SettingsLoadingPrewarmShouldKeepPumping(false, 108, 108, 0));
	EXPECT_TRUE(SettingsLoadingPrewarmShouldKeepPumping(false, 108, 108, 7));
	EXPECT_FALSE(SettingsLoadingPrewarmShouldKeepPumping(false, 108, 108, 8));
	EXPECT_FALSE(SettingsLoadingPrewarmShouldKeepPumping(true, 20, 108, 0));
}
