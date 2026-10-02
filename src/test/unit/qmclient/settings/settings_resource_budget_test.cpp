#include <game/client/components/settings_resource_jobs.h>

#include <gtest/gtest.h>

TEST(SettingsResourceBudget, ScrollActiveStageBudgetBlocksBackgroundUploads)
{
	SSettingsResourceFrameContext ScrollActive{};
	ScrollActive.m_ScrollActive = true;

	EXPECT_EQ(SettingsResourceFrameStageBudget(ScrollActive, ESettingsResourcePriority::BACKGROUND, 4, 1), 0);
	EXPECT_EQ(SettingsResourceFrameStageBudget(ScrollActive, ESettingsResourcePriority::VISIBLE, 4, 1), 1);
}

TEST(SettingsResourceBudget, RecoveryDefersOversizedUploads)
{
	SSettingsResourceFrameContext Recovery{};
	Recovery.m_PostScrollRecoveryFrames = 3;

	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(Recovery, false, ESettingsResourcePriority::VISIBLE, 0, 2 * 1024 * 1024, 1 * 1024 * 1024));
}

TEST(SettingsResourceBudget, ImmediateScrollBlocksHeavyWorkBeforePersistentStateCatchesUp)
{
	const SSettingsResourceFrameContext Idle = SettingsBuildFrameContext(false, false, 0);
	const SSettingsResourceFrameContext ImmediateScroll = SettingsBuildFrameContext(false, true, 0);

	EXPECT_EQ(SettingsResourceSharedHeavyBudget(Idle, 4, 1), 4);
	EXPECT_EQ(SettingsResourceSharedHeavyBudget(ImmediateScroll, 4, 1), 0);
	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(ImmediateScroll, false, ESettingsResourcePriority::VISIBLE, 0, 2 * 1024 * 1024, 1 * 1024 * 1024));
}

TEST(SettingsResourceBudget, PostListScrollClampsStaleIdleHeavyBudget)
{
	const SSettingsResourceFrameContext PreListIdle = SettingsBuildFrameContext(false, false, 0);
	const int PreListHeavyBudget = SettingsResourceSharedHeavyBudget(PreListIdle, 4, 1);
	const SSettingsResourceFrameContext PostListScroll = SettingsBuildFrameContext(false, true, 0);

	EXPECT_EQ(PreListHeavyBudget, 4);
	EXPECT_EQ(SettingsResourceClampSharedHeavyBudget(PreListHeavyBudget, PostListScroll, 4, 1), 0);
}

TEST(SettingsResourceBudget, PostListRecoveryClampsStaleIdleHeavyBudget)
{
	const SSettingsResourceFrameContext PreListIdle = SettingsBuildFrameContext(false, false, 0);
	const int PreListHeavyBudget = SettingsResourceSharedHeavyBudget(PreListIdle, 4, 1);
	const SSettingsResourceFrameContext PostListRecovery = SettingsBuildFrameContext(false, false, 2);

	EXPECT_EQ(PreListHeavyBudget, 4);
	EXPECT_EQ(SettingsResourceClampSharedHeavyBudget(PreListHeavyBudget, PostListRecovery, 4, 1), 1);
}

TEST(SettingsResourceBudget, MergeAndUploadStopAtLocalLimits)
{
	SSettingsResourceMergeBudget Budget;
	Budget.m_MaxListEntries = 2;
	Budget.m_MaxGpuUploads = 1;
	EXPECT_TRUE(SettingsResourceConsumeMergeEntry(Budget));
	EXPECT_TRUE(SettingsResourceConsumeMergeEntry(Budget));
	EXPECT_FALSE(SettingsResourceConsumeMergeEntry(Budget));
	EXPECT_EQ(Budget.m_StopReason, ESettingsWarmupStopReason::MERGE_BUDGET);
	Budget.m_StopReason = ESettingsWarmupStopReason::NONE;
	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(Budget));
	EXPECT_FALSE(SettingsResourceConsumeGpuUpload(Budget));
	EXPECT_EQ(Budget.m_StopReason, ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET);
}

TEST(SettingsResourceBudget, MergeHonorsUnifiedFrameBudget)
{
	SSettingsResourceMergeBudget ResourceBudget;
	ResourceBudget.m_MaxListEntries = 8;
	SSettingsWarmupFrameBudget FrameBudget;
	FrameBudget.m_MaxJobResultMerges = 1;
	EXPECT_TRUE(SettingsResourceConsumeMergeEntry(ResourceBudget, &FrameBudget));
	EXPECT_TRUE(SettingsResourceConsumeMergeEntry(ResourceBudget, &FrameBudget));
	EXPECT_EQ(ResourceBudget.m_MaxListEntries, 6);
	EXPECT_EQ(ResourceBudget.m_StopReason, ESettingsWarmupStopReason::NONE);

	SSettingsResourceMergeBudget NextBatchBudget;
	NextBatchBudget.m_MaxListEntries = 8;
	EXPECT_FALSE(SettingsResourceConsumeMergeEntry(NextBatchBudget, &FrameBudget));
	EXPECT_EQ(NextBatchBudget.m_MaxListEntries, 8);
	EXPECT_EQ(NextBatchBudget.m_StopReason, ESettingsWarmupStopReason::MERGE_BUDGET);
	EXPECT_EQ(FrameBudget.m_StopReason, ESettingsWarmupStopReason::MERGE_BUDGET);
}

TEST(SettingsResourceBudget, GpuUploadHonorsUnifiedFrameBudget)
{
	SSettingsResourceMergeBudget ResourceBudget;
	ResourceBudget.m_MaxGpuUploads = 8;
	SSettingsWarmupFrameBudget FrameBudget;
	FrameBudget.m_MaxGpuUploads = 1;

	EXPECT_TRUE(SettingsResourceConsumeGpuUpload(ResourceBudget, &FrameBudget));
	EXPECT_FALSE(SettingsResourceConsumeGpuUpload(ResourceBudget, &FrameBudget));
	EXPECT_EQ(ResourceBudget.m_StopReason, ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET);
	EXPECT_EQ(FrameBudget.m_StopReason, ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET);
	EXPECT_EQ(ResourceBudget.m_MaxGpuUploads, 7);
}

TEST(SettingsResourceBudget, GpuUploadCanReserveMultipleUploads)
{
	SSettingsResourceMergeBudget ResourceBudget;
	ResourceBudget.m_MaxGpuUploads = 8;
	SSettingsWarmupFrameBudget FrameBudget;
	FrameBudget.m_MaxGpuUploads = 3;

	EXPECT_TRUE(SettingsResourceConsumeGpuUploads(ResourceBudget, &FrameBudget, 3));
	EXPECT_EQ(ResourceBudget.m_MaxGpuUploads, 5);
	EXPECT_FALSE(SettingsResourceConsumeGpuUploads(ResourceBudget, &FrameBudget, 1));
	EXPECT_EQ(ResourceBudget.m_MaxGpuUploads, 5);
	EXPECT_EQ(FrameBudget.m_StopReason, ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET);
}

TEST(SettingsResourceBudget, DefaultSettingsAllowsOneSkinUploadBatch)
{
	SSettingsResourceMergeBudget ResourceBudget;
	ResourceBudget.m_MaxGpuUploads = 42;
	SSettingsWarmupFrameBudget FrameBudget;

	EXPECT_TRUE(SettingsResourceConsumeGpuUploads(ResourceBudget, &FrameBudget, 14));
	EXPECT_FALSE(SettingsResourceConsumeGpuUploads(ResourceBudget, &FrameBudget, 14));
	EXPECT_EQ(ResourceBudget.m_MaxGpuUploads, 28);
	EXPECT_EQ(FrameBudget.m_StopReason, ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET);
}
