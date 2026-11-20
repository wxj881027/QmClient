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

TEST(SettingsResourceJobsSkinPlan, CurrentViewportRequestsPrecedeOldVisibleAndPrefetch)
{
	const std::vector<int> Requests{0, 1, 2, 3, 4};
	const std::array<ESettingsResourcePriority, 5> Priorities{ESettingsResourcePriority::PREFETCH, ESettingsResourcePriority::VISIBLE, ESettingsResourcePriority::BACKGROUND, ESettingsResourcePriority::VISIBLE, ESettingsResourcePriority::VISIBLE};
	std::vector<int> Visited;
	SettingsVisitSkinLoadRequests(Requests, [&](int Index) { return Priorities[Index]; }, [](int Index) { return Index == 3 || Index == 4; }, [&](int Index) { Visited.push_back(Index); });
	EXPECT_EQ(Visited, (std::vector<int>{3, 4, 1, 0, 2}));
	Visited.clear();
	SettingsVisitSkinLoadRequests(Requests, [&](int Index) { return Priorities[Index]; }, [](int Index) { return Index == 1; }, [&](int Index) { Visited.push_back(Index); });
	EXPECT_EQ(Visited, (std::vector<int>{1, 3, 4, 0, 2}));
}

TEST(SettingsResourceJobsSkinPlan, BlockedPrefetchDoesNotEndRequestBatch)
{
	const std::vector<ESettingsResourcePriority> Requests{ESettingsResourcePriority::PREFETCH, ESettingsResourcePriority::VISIBLE};
	std::vector<ESettingsResourcePriority> Started;
	SettingsVisitSkinLoadRequests(Requests, [](auto Priority) { return Priority; }, [](auto) { return false; }, [&](auto Priority) {
		const auto Admission = SettingsSkinSourceAdmissionDecision({false, Priority, false, 2, 2, 4});
		if(Admission.m_PromoteAllowed)
			Started.push_back(Priority); });
	EXPECT_EQ(Started, (std::vector<ESettingsResourcePriority>{ESettingsResourcePriority::VISIBLE}));
}

TEST(SettingsResourceJobsSkinPlan, RequestBatchClassificationSurvivesVisitorChanges)
{
	const std::vector<int> Requests{0, 1};
	std::array<ESettingsResourcePriority, 2> Priorities{ESettingsResourcePriority::VISIBLE, ESettingsResourcePriority::PREFETCH};
	std::vector<int> Visited;
	SettingsVisitSkinLoadRequests(Requests, [&](int Index) { return Priorities[Index]; }, [](int) { return false; }, [&](int Index) {
		Visited.push_back(Index);
		Priorities[Index] = ESettingsResourcePriority::BACKGROUND; });
	EXPECT_EQ(Visited, (std::vector<int>{0, 1}));
}

TEST(SettingsResourceJobsSkinPlan, RejectedRequestDoesNotSkipLaterRequestOfSameRank)
{
	const std::vector<int> Requests{0, 1, 2};
	std::vector<int> Attempted;
	std::vector<int> Started;
	SettingsVisitSkinLoadRequests(Requests, [](int) { return ESettingsResourcePriority::VISIBLE; }, [](int) { return true; }, [&](int Index) {
		Attempted.push_back(Index);
		if(Index == 0)
			return false;
		Started.push_back(Index);
		return true; });
	EXPECT_EQ(Attempted, Requests);
	EXPECT_EQ(Started, (std::vector<int>{1, 2}));
}

TEST(SettingsResourceJobsSkinPlan, ViewportPriorityExpiresUnlessRepublished)
{
	CSettingsSkinLoadViewport Viewport;
	Viewport.BeginFrame();
	EXPECT_TRUE(Viewport.Publish({"cat", "default"}));
	EXPECT_TRUE(Viewport.Contains("cat"));
	Viewport.BeginFrame();
	EXPECT_FALSE(Viewport.Contains("cat"));
	EXPECT_FALSE(Viewport.Publish({"cat", "default"}));
	EXPECT_TRUE(Viewport.Contains("cat"));
	EXPECT_TRUE(Viewport.Publish({"dog"}));
	EXPECT_FALSE(Viewport.Contains("cat"));
	EXPECT_TRUE(Viewport.Contains("dog"));
	EXPECT_TRUE(Viewport.Publish({}));
	EXPECT_FALSE(Viewport.Contains("dog"));
}

TEST(SettingsResourceJobsSkinPlan, MeasurementDoesNotReplaceRealViewport)
{
	CSettingsSkinLoadViewport Viewport;
	Viewport.BeginFrame();
	EXPECT_TRUE(Viewport.Publish({"cat"}));
	EXPECT_FALSE(Viewport.Publish({"dog"}, true));
	EXPECT_TRUE(Viewport.Contains("cat"));
	EXPECT_FALSE(Viewport.Contains("dog"));
	Viewport.BeginFrame();
	EXPECT_FALSE(Viewport.Publish({"dog"}, true));
	EXPECT_FALSE(Viewport.Contains("cat"));
	EXPECT_FALSE(Viewport.Contains("dog"));
}

TEST(SettingsResourceJobsSkinPlan, ViewportPublishReportsOnlyRealChanges)
{
	CSettingsSkinLoadViewport Viewport;
	Viewport.BeginFrame();
	EXPECT_TRUE(Viewport.Publish({"cat"}));
	EXPECT_FALSE(Viewport.Publish({"cat"}));
	EXPECT_TRUE(Viewport.Publish({"cat", "dog"}));
	EXPECT_FALSE(Viewport.Publish({"cat", "dog"}));
	EXPECT_EQ(Viewport.PublishedNames(), (std::vector<std::string>{"cat", "dog"}));
}

TEST(SettingsResourceJobsSkinPlan, StaleViewportRequestsReleaseOnlyTransientNonActiveLoads)
{
	constexpr bool PendingOrLoading = true;
	EXPECT_TRUE(SettingsSkinShouldReleaseViewportRequest(false, false, false, ESettingsResourcePriority::VISIBLE, PendingOrLoading));
	EXPECT_FALSE(SettingsSkinShouldReleaseViewportRequest(true, false, false, ESettingsResourcePriority::VISIBLE, PendingOrLoading));
	EXPECT_FALSE(SettingsSkinShouldReleaseViewportRequest(false, true, false, ESettingsResourcePriority::VISIBLE, PendingOrLoading));
	EXPECT_FALSE(SettingsSkinShouldReleaseViewportRequest(false, false, true, ESettingsResourcePriority::VISIBLE, PendingOrLoading));
	EXPECT_FALSE(SettingsSkinShouldReleaseViewportRequest(false, false, false, ESettingsResourcePriority::BACKGROUND, PendingOrLoading));
	EXPECT_FALSE(SettingsSkinShouldReleaseViewportRequest(false, false, false, ESettingsResourcePriority::VISIBLE, false));
}

TEST(SettingsResourceJobsSkinPlan, DownloadSessionLimitAndWaitLoggingAreBounded)
{
	EXPECT_TRUE(SettingsSkinCanStartDownloadSession(0, 48));
	EXPECT_TRUE(SettingsSkinCanStartDownloadSession(47, 48));
	EXPECT_FALSE(SettingsSkinCanStartDownloadSession(48, 48));
	EXPECT_TRUE(SettingsSkinCanStartDownloadSession(999, 0));

	EXPECT_TRUE(SettingsSkinWaitLogShouldEmit(10, 0, 10));
	EXPECT_FALSE(SettingsSkinWaitLogShouldEmit(19, 10, 10));
	EXPECT_TRUE(SettingsSkinWaitLogShouldEmit(20, 10, 10));
	EXPECT_TRUE(SettingsSkinWaitLogShouldEmit(11, 0, 10));
}

TEST(SettingsResourceJobsSkinPlan, LeavingSettingsUsesBoundedResidency)
{
	EXPECT_EQ(SettingsSkinInactiveResidencyLimit(512), (size_t)128);
	EXPECT_EQ(SettingsSkinInactiveResidencyLimit(64), (size_t)64);
	EXPECT_EQ(SettingsSkinInactiveResidencyLimit(0), (size_t)0);
}

TEST(SettingsResourceJobsSkinPlan, ActualViewportExcludesFullyClippedAndMeasurementRows)
{
	const CUIRect Clip{10.0f, 20.0f, 50.0f, 80.0f};
	EXPECT_TRUE(CSettingsSkinLoadViewport::RowVisible(false, {0.0f, 10.0f, 30.0f, 30.0f}, &Clip));
	EXPECT_FALSE(CSettingsSkinLoadViewport::RowVisible(false, {0.0f, 0.0f, 30.0f, 20.0f}, &Clip));
	EXPECT_FALSE(CSettingsSkinLoadViewport::RowVisible(false, {0.0f, 30.0f, 10.0f, 20.0f}, &Clip));
	EXPECT_FALSE(CSettingsSkinLoadViewport::RowVisible(true, Clip, &Clip));
	EXPECT_TRUE(CSettingsSkinLoadViewport::RowVisible(false, Clip, nullptr));
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

TEST(SettingsResourceJobsSkinPlan, VisibleRequestsCanUseBoundedOverflowAboveBackgroundWindow)
{
	const auto Decision = SettingsSkinSourceAdmissionDecision({
		true,
		ESettingsResourcePriority::VISIBLE,
		false,
		249,
		64,
		128,
		35,
		512,
	});

	EXPECT_TRUE(Decision.m_PromoteAllowed);
	EXPECT_EQ(Decision.m_PromotePriority, ESettingsResourcePriority::VISIBLE);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinSourceAdmissionBlockReason::NONE);
	EXPECT_FALSE(Decision.m_CountFuseApplies);
}

TEST(SettingsResourceJobsSkinPlan, VisibleRequestsRemainBoundedByCountFuse)
{
	const auto Decision = SettingsSkinSourceAdmissionDecision({
		true,
		ESettingsResourcePriority::VISIBLE,
		false,
		512,
		64,
		128,
		35,
		512,
	});

	EXPECT_FALSE(Decision.m_PromoteAllowed);
	EXPECT_EQ(Decision.m_BlockReason, ESettingsSkinSourceAdmissionBlockReason::VISIBLE_RESERVE);
	EXPECT_FALSE(Decision.m_CountFuseApplies);
}

TEST(SettingsResourceJobsSkinPlan, FallbackSweepOnlyRunsWithoutExplicitQueues)
{
	EXPECT_TRUE(SettingsSkinFallbackSweepShouldRun(false, false));
	EXPECT_FALSE(SettingsSkinFallbackSweepShouldRun(true, false));
	EXPECT_FALSE(SettingsSkinFallbackSweepShouldRun(false, true));
	EXPECT_FALSE(SettingsSkinFallbackSweepShouldRun(true, true));
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
